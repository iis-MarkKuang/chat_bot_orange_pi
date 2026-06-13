// DeepSeek-R1-Distill-Qwen-1.5B 对话 CLI
// 直接链接 RK3588 NPU 运行时 librkllmrt.so，使用 .rkllm 模型进行流式多轮对话。

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include "rkllm.h"

// DeepSeek-R1 对话模板标记
static const std::string BOS = "<｜begin▁of▁sentence｜>";
static const std::string EOS = "<｜end▁of▁sentence｜>";
static const std::string USER_TAG = "<｜User｜>";
static const std::string ASSISTANT_TAG = "<｜Assistant｜>";

static const char* DEFAULT_MODEL =
    "models/DeepSeek-R1-Distill-Qwen-1.5B_W8A8_RK3588.rkllm";

// 全局状态
static LLMHandle g_handle = nullptr;
static std::string g_answer;            // 当前回答累积，用于写入历史
static std::atomic<bool> g_generating{false};  // 是否正在生成
static std::atomic<bool> g_aborted{false};     // 当前生成是否被中断

// 流式输出回调：rkllm_run 同步执行，每生成一段就回调一次
void llm_callback(RKLLMResult* result, void* /*userdata*/, LLMCallState state) {
    switch (state) {
        case RKLLM_RUN_NORMAL:
            if (result != nullptr && result->text != nullptr) {
                std::cout << result->text << std::flush;
                g_answer += result->text;
            }
            break;
        case RKLLM_RUN_FINISH:
            std::cout << std::endl;
            g_generating = false;
            break;
        case RKLLM_RUN_ERROR:
            std::cerr << "\n[推理出错]" << std::endl;
            g_generating = false;
            break;
        default:
            break;
    }
}

// Ctrl-C：生成中则中断当前回答，否则退出程序
void handle_sigint(int /*sig*/) {
    if (g_generating.load()) {
        g_aborted = true;
        if (g_handle != nullptr) {
            rkllm_abort(g_handle);
        }
    } else {
        std::cout << "\n再见！" << std::endl;
        if (g_handle != nullptr) {
            rkllm_destroy(g_handle);
            g_handle = nullptr;
        }
        std::exit(0);
    }
}

void print_help() {
    std::cout << "可用命令：\n"
              << "  /help   显示帮助\n"
              << "  /clear  清空对话上下文\n"
              << "  /exit   退出程序\n"
              << "提示：生成回答时按 Ctrl-C 可中断当前回答；空闲时按 Ctrl-C 退出。\n"
              << std::endl;
}

int main(int argc, char** argv) {
    std::string model_path = (argc > 1) ? argv[1] : DEFAULT_MODEL;
    int max_new_tokens = (argc > 2) ? std::atoi(argv[2]) : 2048;
    int max_context_len = (argc > 3) ? std::atoi(argv[3]) : 4096;

    std::cout << "==> 加载模型: " << model_path << std::endl;
    std::cout << "    max_new_tokens=" << max_new_tokens
              << ", max_context_len=" << max_context_len << std::endl;

    RKLLMParam param = rkllm_createDefaultParam();
    param.model_path = model_path.c_str();
    param.max_context_len = max_context_len;
    param.max_new_tokens = max_new_tokens;
    param.top_k = 1;
    param.top_p = 0.95f;
    param.temperature = 0.6f;        // DeepSeek-R1 推荐温度
    param.repeat_penalty = 1.1f;
    param.frequency_penalty = 0.0f;
    param.presence_penalty = 0.0f;
    param.mirostat = 0;
    param.skip_special_token = true;
    param.is_async = false;

    int ret = rkllm_init(&g_handle, &param, llm_callback);
    if (ret != 0 || g_handle == nullptr) {
        std::cerr << "模型初始化失败（rkllm_init 返回 " << ret << "）。\n"
                  << "请确认模型文件存在且与运行时 v1.1.4 兼容。" << std::endl;
        return 1;
    }
    std::cout << "==> 模型加载完成。\n" << std::endl;

    std::signal(SIGINT, handle_sigint);

    print_help();

    RKLLMInferParam infer_param;
    std::memset(&infer_param, 0, sizeof(RKLLMInferParam));
    infer_param.mode = RKLLM_INFER_GENERATE;
    infer_param.lora_params = nullptr;
    infer_param.prompt_cache_params = nullptr;

    std::string history;  // 累积的多轮对话上下文（已套模板）

    while (true) {
        std::cout << "\n你> " << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) {
            std::cout << "\n再见！" << std::endl;
            break;
        }

        if (line.empty()) {
            continue;
        }
        if (line == "/exit") {
            std::cout << "再见！" << std::endl;
            break;
        }
        if (line == "/help") {
            print_help();
            continue;
        }
        if (line == "/clear") {
            history.clear();
            std::cout << "（已清空对话上下文）" << std::endl;
            continue;
        }

        // 构造本轮提示：首轮加 BOS，每轮 <｜User｜>问题<｜Assistant｜>
        std::string prompt;
        if (history.empty()) {
            prompt = BOS;
        }
        prompt += history;
        prompt += USER_TAG + line + ASSISTANT_TAG;

        g_answer.clear();
        g_aborted = false;
        g_generating = true;

        std::cout << "助手> " << std::flush;

        RKLLMInput input;
        std::memset(&input, 0, sizeof(RKLLMInput));
        input.input_type = RKLLM_INPUT_PROMPT;
        input.prompt_input = prompt.c_str();

        ret = rkllm_run(g_handle, &input, &infer_param, nullptr);
        g_generating = false;

        if (ret != 0 && !g_aborted) {
            std::cerr << "\n[rkllm_run 失败，返回 " << ret << "]" << std::endl;
            continue;
        }

        if (g_aborted) {
            std::cout << "\n（已中断本轮回答）" << std::endl;
            // 中断的回答仍写入上下文，保持模板完整
        }

        // 把本轮 user+assistant 追加进历史，实现多轮记忆
        history += USER_TAG + line + ASSISTANT_TAG + g_answer + EOS;
    }

    if (g_handle != nullptr) {
        rkllm_destroy(g_handle);
        g_handle = nullptr;
    }
    return 0;
}
