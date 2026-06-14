# DeepSeek-1.5B NPU 对话 CLI（C++ / RKLLM）

一个运行在 Orange Pi（RK3588）上的命令行对话程序，使用 C++ 编写，直接链接系统预装的 NPU 推理运行时 `librkllmrt.so`，加载 `DeepSeek-R1-Distill-Qwen-1.5B` 的 `.rkllm` 模型，利用 RK3588 的 NPU 进行硬件加速推理，支持**多轮、流式**对话，并内置**工具调用（ToolCall）**能力（如实时天气查询）。

## 环境依赖

- 硬件：RK3588（本项目在 Orange Pi / aarch64 上验证）
- NPU 运行时（系统已预装）：
  - `/usr/lib/librkllmrt.so`（**v1.1.4**）
  - `/usr/include/rkllm.h`
- 编译器：`g++`（支持 C++17）
- 下载工具：`curl`（同时也是天气工具的运行时依赖）
- 第三方库：`nlohmann/json` 单头文件（用 `scripts/fetch_deps.sh` 自动下载到 `third_party/`）

> 注意：本机自带的 `cmake` 安装不完整（缺少 `Modules` 目录），因此推荐使用 `build.sh`（直接调用 g++）来构建。若你的环境 `cmake` 正常，也可使用 `CMakeLists.txt`。

## 目录结构

```
chat_bot_orange_pi/
├── src/main.cpp            # CLI 主程序（对话 + 工具调用 agent 循环）
├── src/tools.h / tools.cpp # 工具框架 + 内置工具（天气）
├── scripts/download_model.sh  # 从 ModelScope 下载预编译 .rkllm 模型
├── scripts/fetch_deps.sh   # 下载 nlohmann/json 单头文件到 third_party/
├── third_party/nlohmann/   # 第三方头文件（下载得到）
├── models/                 # 模型存放目录（下载后约 2GB）
├── build.sh                # 直接用 g++ 构建（推荐，会自动补依赖）
├── CMakeLists.txt          # cmake 构建（环境 cmake 正常时可用）
└── README.md
```

## 第一步：下载模型

模型来自 ModelScope 仓库 [`radxa/DeepSeek-R1-Distill-Qwen-1.5B_RKLLM`](https://modelscope.cn/models/radxa/DeepSeek-R1-Distill-Qwen-1.5B_RKLLM)，文件为 `DeepSeek-R1-Distill-Qwen-1.5B_W8A8_RK3588.rkllm`（W8A8 量化，约 2.05GB，RK3588 专用，约 15 tok/s）。

```bash
bash scripts/download_model.sh
```

脚本使用 `curl -C -` 断点续传，下载中断后重复执行即可继续。

## 第二步：编译

推荐（g++）：

```bash
bash build.sh
# 产物：build/chat
# build.sh 会在缺少 third_party/nlohmann/json.hpp 时自动调用 scripts/fetch_deps.sh 下载
```

也可手动先拉依赖再编译：

```bash
bash scripts/fetch_deps.sh   # 下载 nlohmann/json 单头文件
bash build.sh
```

或（cmake，需要 cmake 安装完整）：

```bash
bash scripts/fetch_deps.sh
cmake -B build && cmake --build build -j
```

## 第三步：运行

```bash
./build/chat
```

可选参数：`./build/chat [模型路径] [max_new_tokens] [max_context_len]`，默认：

- 模型路径：`models/DeepSeek-R1-Distill-Qwen-1.5B_W8A8_RK3588.rkllm`
- `max_new_tokens`：2048
- `max_context_len`：4096

例如指定上下文长度：

```bash
./build/chat models/DeepSeek-R1-Distill-Qwen-1.5B_W8A8_RK3588.rkllm 2048 4096
```

## 使用说明

启动后进入交互式对话，直接输入问题回车即可。DeepSeek-R1 模型会先输出 `<think>...</think>` 推理过程，然后给出最终答案。

内置命令：

| 命令 | 说明 |
| --- | --- |
| `/help` | 显示帮助 |
| `/tools` | 列出当前可用工具 |
| `/clear` | 清空对话上下文（开始新会话） |
| `/exit` | 退出程序 |

快捷键：

- 生成回答过程中按 `Ctrl-C`：中断当前回答（程序继续）。
- 空闲时按 `Ctrl-C`：退出程序。

## 工具调用（ToolCall）

模型可在需要时调用工具获取实时信息。例如：

```
你> 北京今天天气怎么样？
助手> <think> ... </think>
<tool_call>{"name":"get_weather","arguments":{"location":"北京"}}</tool_call>
[调用工具] get_weather 参数={"location":"北京"}
[工具结果] 北京: ☀️ +27°C
助手> 北京今天晴，气温约 27°C ...
```

### 工作原理

1. system 提示词告诉模型有哪些工具、调用格式，以及一个 one-shot 示例。
2. 模型需要工具时输出一行 `<tool_call>{"name":...,"arguments":{...}}</tool_call>`。
3. 程序检测到该标记后**提前停止生成**，用 `nlohmann/json` 解析出工具名与参数。
4. 执行对应工具，把结果以 `<tool_result>...</tool_result>` 回填到同一轮 assistant 上下文中。
5. 再次生成，模型据此给出最终中文回答。单轮内最多循环 5 次工具调用。
6. 若模型未按格式输出或解析失败，自动降级为普通回答，不会崩溃。

### 内置工具

| 工具 | 说明 | 依赖 |
| --- | --- | --- |
| `get_weather` | 查询指定地点实时天气，调用 [wttr.in](https://wttr.in)（无需 API key） | 联网 + `curl` |

### 扩展新工具

在 [src/tools.cpp](src/tools.cpp) 里实现一个 `std::string my_tool(const json& args)` 函数，然后在 `register_builtin_tools()` 中注册：

```cpp
registry.add(Tool{
    "tool_name",                 // 工具名
    "工具用途说明",               // 给模型看的描述
    "{\"参数\": \"说明\"}",       // 参数示例
    my_tool,                     // 执行函数
});
```

注册后会自动出现在 system 提示词和 `/tools` 列表里，无需改动 agent 循环。

## 实现说明

- 通过 `rkllm_init` 加载模型，`rkllm_run`（同步）执行推理，结果经 `LLMResultCallback` 回调逐 token 流式打印；回调中检测到 `</tool_call>` 时调用 `rkllm_abort` 提前停止（best-effort）。
- 采样参数：`temperature=0.6`、`top_p=0.95`、`repeat_penalty=1.1`（贴合 DeepSeek-R1 推荐）。
- 多轮上下文：手动按 DeepSeek-R1 模板拼接（system 提示词置于 `<｜begin▁of▁sentence｜>` 之后、首个 `<｜User｜>` 之前）：
  ```
  <｜begin▁of▁sentence｜>{system}<｜User｜>问题1<｜Assistant｜>回答1<｜end▁of▁sentence｜><｜User｜>问题2<｜Assistant｜>...
  ```
  每轮把模型回答（含工具交互）追加进历史，实现上下文记忆；`/clear` 清空历史。

## 常见问题

- **`rkllm_init` 失败**：确认模型文件已完整下载，且与运行时版本（v1.1.4）兼容。
- **启动时打印 rknpu 驱动版本告警**：通常仍可正常推理；如需消除可升级板端 RKNPU 驱动。
- **首次加载较慢**：需要把约 2GB 模型读入内存，请耐心等待。
- **天气查询失败**：检查能否联网访问 `wttr.in`（`curl "https://wttr.in/Beijing?format=3"`）；无网络时工具会返回友好错误提示。
- **模型不调用工具 / 格式不对**：1.5B 蒸馏模型遵循指令能力有限，可多试几次或把问题问得更直接（如"查一下北京的天气"）。
