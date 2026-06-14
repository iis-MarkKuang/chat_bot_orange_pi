// DeepSeek-R1-Distill-Qwen-1.5B 对话 CLI（带工具调用 / ToolCall）
// 直接链接 RK3588 NPU 运行时 librkllmrt.so，使用 .rkllm 模型进行流式多轮对话，
// 并通过提示词协议 + agent 循环让模型可以调用内置工具（如天气查询）。

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include "rkllm.h"
#include "tools.h"

// DeepSeek-R1 对话模板标记
static const std::string BOS = "<｜begin▁of▁sentence｜>";
static const std::string EOS = "<｜end▁of▁sentence｜>";
static const std::string USER_TAG = "<｜User｜>";
static const std::string ASSISTANT_TAG = "<｜Assistant｜>";

// 工具调用协议标记
static const std::string TOOL_CALL_OPEN = "<tool_call>";
static const std::string TOOL_CALL_CLOSE = "</tool_call>";

static const char* DEFAULT_MODEL =
    "models/DeepSeek-R1-Distill-Qwen-1.5B_W8A8_RK3588.rkllm";

static const int MAX_TOOL_ITERS = 5;  // 单轮内最多工具调用次数

// 全局状态
static LLMHandle g_handle = nullptr;
static std::string g_answer;                  // 当前生成累积
static std::atomic<bool> g_generating{false}; // 是否正在生成
static std::atomic<bool> g_aborted{false};    // 被用户 Ctrl-C 中断
static std::atomic<bool> g_tool_stop{false};  // 因检测到工具调用而提前停止

static ToolRegistry g_registry;

// ---------------------------------------------------------------------------
// 小工具函数
// ---------------------------------------------------------------------------

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

struct ParsedToolCall {
    bool ok = false;
    std::string name;
    json args;
    size_t match_end = 0;  // 工具调用在原文中结束的位置（用于截断后续幻觉内容）
};

// 尝试把一段文本当作工具调用 JSON 对象解析
static ParsedToolCall try_parse_tool_obj(const std::string& body) {
    ParsedToolCall tc;
    try {
        json j = json::parse(body);
        if (j.is_object() && j.contains("name")) {
            tc.name = j.value("name", std::string());
            tc.args = j.contains("arguments") ? j["arguments"] : json::object();
            tc.ok = !tc.name.empty();
        }
    } catch (...) {
        tc.ok = false;
    }
    return tc;
}

// 从模型输出里提取工具调用。
// 优先匹配 <tool_call>{...}</tool_call>；1.5B 模型常常不带标签，故再降级为
// 扫描第一个包含 "name" 字段、且大括号配平的 JSON 对象（容忍反引号/前后噪声）。
static ParsedToolCall extract_tool_call(const std::string& text) {
    // 1) 严格匹配标签
    size_t a = text.find(TOOL_CALL_OPEN);
    if (a != std::string::npos) {
        size_t content_begin = a + TOOL_CALL_OPEN.size();
        size_t b = text.find(TOOL_CALL_CLOSE, content_begin);
        std::string body = (b == std::string::npos)
                               ? text.substr(content_begin)
                               : text.substr(content_begin, b - content_begin);
        ParsedToolCall tc = try_parse_tool_obj(trim(body));
        if (tc.ok) {
            tc.match_end = (b == std::string::npos)
                               ? text.size()
                               : b + TOOL_CALL_CLOSE.size();
            return tc;
        }
    }

    // 2) 降级：扫描配平的 {...}，找到含 "name" 的 JSON 对象
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '{') continue;
        int depth = 0;
        bool in_str = false;
        bool esc = false;
        for (size_t j = i; j < text.size(); ++j) {
            char c = text[j];
            if (esc) {
                esc = false;
                continue;
            }
            if (c == '\\') {
                esc = true;
                continue;
            }
            if (c == '"') {
                in_str = !in_str;
                continue;
            }
            if (in_str) continue;
            if (c == '{') {
                ++depth;
            } else if (c == '}') {
                --depth;
                if (depth == 0) {
                    std::string body = text.substr(i, j - i + 1);
                    if (body.find("\"name\"") != std::string::npos) {
                        ParsedToolCall tc = try_parse_tool_obj(body);
                        if (tc.ok) {
                            tc.match_end = j + 1;
                            return tc;
                        }
                    }
                    break;  // 从下一个 '{' 继续找
                }
            }
        }
    }
    return ParsedToolCall{};
}

// ---------------------------------------------------------------------------
// 流式输出回调
// ---------------------------------------------------------------------------

void llm_callback(RKLLMResult* result, void* /*userdata*/, LLMCallState state) {
    switch (state) {
        case RKLLM_RUN_NORMAL:
            if (result != nullptr && result->text != nullptr) {
                std::cout << result->text << std::flush;
                g_answer += result->text;
                // 检测到完整工具调用时提前停止生成（best-effort）
                if (!g_tool_stop.load() &&
                    g_answer.find(TOOL_CALL_CLOSE) != std::string::npos) {
                    g_tool_stop = true;
                    if (g_handle != nullptr) {
                        rkllm_abort(g_handle);
                    }
                }
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

// ---------------------------------------------------------------------------
// 提示词构造
// ---------------------------------------------------------------------------

static std::string build_system_prompt() {
    std::string s =
        "你是一个有用的中文智能助手，可以调用以下工具来获取实时信息：\n";
    s += g_registry.prompt_spec();
    s +=
        "\n使用规则：\n"
        "1. 当需要工具时，先简要思考，然后只输出一行工具调用，格式严格为：\n"
        "   " + TOOL_CALL_OPEN +
        "{\"name\":\"工具名\",\"arguments\":{...}}" + TOOL_CALL_CLOSE + "\n"
        "2. 直接输出工具调用 JSON，不要用反引号或代码块包裹，输出后立即停止，不要自己编造结果。\n"
        "3. 系统会以 <tool_result>...</tool_result> 把结果返回给你，"
        "你再根据真实结果用中文回答用户。\n"
        "4. 如果问题不需要工具，直接用中文回答。\n"
        "示例：\n"
        "用户：上海天气怎么样？\n"
        "助手：" + TOOL_CALL_OPEN +
        "{\"name\":\"get_weather\",\"arguments\":{\"location\":\"上海\"}}" +
        TOOL_CALL_CLOSE + "\n\n";
    return s;
}

void print_help() {
    std::cout << "可用命令：\n"
              << "  /help   显示帮助\n"
              << "  /tools  列出可用工具\n"
              << "  /clear  清空对话上下文\n"
              << "  /exit   退出程序\n"
              << "提示：生成回答时按 Ctrl-C 可中断当前回答；空闲时按 Ctrl-C 退出。\n"
              << std::endl;
}

// ---------------------------------------------------------------------------
// 单次生成：返回模型本次输出文本
// ---------------------------------------------------------------------------

static std::string generate(const std::string& prompt,
                            RKLLMInferParam* infer_param) {
    g_answer.clear();
    g_aborted = false;
    g_tool_stop = false;
    g_generating = true;

    RKLLMInput input;
    std::memset(&input, 0, sizeof(RKLLMInput));
    input.input_type = RKLLM_INPUT_PROMPT;
    input.prompt_input = prompt.c_str();

    int ret = rkllm_run(g_handle, &input, infer_param, nullptr);
    g_generating = false;

    if (ret != 0 && !g_aborted && !g_tool_stop) {
        std::cerr << "\n[rkllm_run 失败，返回 " << ret << "]" << std::endl;
    }
    return g_answer;
}

// ---------------------------------------------------------------------------
// 一次用户提问的完整处理（含工具调用 agent 循环）
// history 在调用前不含本轮内容；处理完后把本轮（含工具交互）追加进 history。
// ---------------------------------------------------------------------------

static void run_agent_turn(std::string& history, const std::string& system_prompt,
                           const std::string& user_line,
                           RKLLMInferParam* infer_param) {
    // conv 保存本轮新增的会话内容（可能含多个 assistant/synthetic-user 回合）
    std::string conv = USER_TAG + user_line + ASSISTANT_TAG;

    for (int iter = 0; iter < MAX_TOOL_ITERS; ++iter) {
        std::string prompt = BOS + system_prompt + history + conv;

        std::cout << "助手> " << std::flush;
        std::string out = generate(prompt, infer_param);

        if (g_aborted.load()) {
            conv += out;
            std::cout << "\n（已中断本轮回答）" << std::endl;
            break;
        }

        ParsedToolCall tc = extract_tool_call(out);
        if (!tc.ok) {
            // 没有工具调用 → 这就是最终回答
            conv += out;
            break;
        }

        // 只保留到工具调用结束，丢弃模型在其后可能编造的"假结果"
        conv += out.substr(0, tc.match_end);

        // 执行工具
        std::cout << "\n[调用工具] " << tc.name << " 参数=" << tc.args.dump()
                  << std::endl;
        std::string result;
        if (g_registry.has(tc.name)) {
            result = g_registry.call(tc.name, tc.args);
        } else {
            result = "[未知工具] " + tc.name + "（可用工具见 /tools）";
        }
        std::cout << "[工具结果] " << result << "\n" << std::endl;

        if (iter == MAX_TOOL_ITERS - 1) {
            std::cout << "（已达到工具调用次数上限，停止）" << std::endl;
            break;
        }

        // 结束当前 assistant 回合，把工具结果作为一条 user 反馈注入，
        // 再开启新的 assistant 回合，明确要求模型据结果作答。
        conv += EOS;
        conv += USER_TAG + "工具 " + tc.name + " 的返回结果是：" + result +
                "\n请根据该结果用中文简洁地回答我之前的问题，不要再调用工具。" +
                ASSISTANT_TAG;
    }

    // 把本轮（含工具交互）写入历史，保持模板完整
    history += conv + EOS;
}

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    std::string model_path = (argc > 1) ? argv[1] : DEFAULT_MODEL;
    int max_new_tokens = (argc > 2) ? std::atoi(argv[2]) : 2048;
    int max_context_len = (argc > 3) ? std::atoi(argv[3]) : 4096;

    register_builtin_tools(g_registry);

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
    std::cout << "已加载工具：\n" << g_registry.list_for_user() << std::endl;

    RKLLMInferParam infer_param;
    std::memset(&infer_param, 0, sizeof(RKLLMInferParam));
    infer_param.mode = RKLLM_INFER_GENERATE;
    infer_param.lora_params = nullptr;
    infer_param.prompt_cache_params = nullptr;

    const std::string system_prompt = build_system_prompt();
    std::string history;  // 累积的多轮对话上下文（已套模板，不含 system）

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
        if (line == "/tools") {
            std::cout << "可用工具：\n" << g_registry.list_for_user() << std::flush;
            continue;
        }
        if (line == "/clear") {
            history.clear();
            std::cout << "（已清空对话上下文）" << std::endl;
            continue;
        }

        run_agent_turn(history, system_prompt, line, &infer_param);
    }

    if (g_handle != nullptr) {
        rkllm_destroy(g_handle);
        g_handle = nullptr;
    }
    return 0;
}
