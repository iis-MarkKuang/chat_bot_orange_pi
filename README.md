# DeepSeek-1.5B NPU 对话 CLI（C++ / RKLLM）

一个运行在 Orange Pi（RK3588）上的命令行对话程序，使用 C++ 编写，直接链接系统预装的 NPU 推理运行时 `librkllmrt.so`，加载 `DeepSeek-R1-Distill-Qwen-1.5B` 的 `.rkllm` 模型，利用 RK3588 的 NPU 进行硬件加速推理，支持**多轮、流式**对话。

## 环境依赖

- 硬件：RK3588（本项目在 Orange Pi / aarch64 上验证）
- NPU 运行时（系统已预装）：
  - `/usr/lib/librkllmrt.so`（**v1.1.4**）
  - `/usr/include/rkllm.h`
- 编译器：`g++`（支持 C++17）
- 下载工具：`curl`

> 注意：本机自带的 `cmake` 安装不完整（缺少 `Modules` 目录），因此推荐使用 `build.sh`（直接调用 g++）来构建。若你的环境 `cmake` 正常，也可使用 `CMakeLists.txt`。

## 目录结构

```
chat_bot_orange_pi/
├── src/main.cpp            # CLI 主程序
├── scripts/download_model.sh  # 从 ModelScope 下载预编译 .rkllm 模型
├── models/                 # 模型存放目录（下载后约 2GB）
├── build.sh                # 直接用 g++ 构建（推荐）
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
```

或（cmake，需要 cmake 安装完整）：

```bash
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
| `/clear` | 清空对话上下文（开始新会话） |
| `/exit` | 退出程序 |

快捷键：

- 生成回答过程中按 `Ctrl-C`：中断当前回答（程序继续）。
- 空闲时按 `Ctrl-C`：退出程序。

## 实现说明

- 通过 `rkllm_init` 加载模型，`rkllm_run`（同步）执行推理，结果经 `LLMResultCallback` 回调逐 token 流式打印。
- 采样参数：`temperature=0.6`、`top_p=0.95`、`repeat_penalty=1.1`（贴合 DeepSeek-R1 推荐）。
- 多轮上下文：手动按 DeepSeek-R1 模板拼接：
  ```
  <｜begin▁of▁sentence｜><｜User｜>问题1<｜Assistant｜>回答1<｜end▁of▁sentence｜><｜User｜>问题2<｜Assistant｜>...
  ```
  每轮把模型回答追加进历史，实现上下文记忆；`/clear` 清空历史。

## 常见问题

- **`rkllm_init` 失败**：确认模型文件已完整下载，且与运行时版本（v1.1.4）兼容。
- **启动时打印 rknpu 驱动版本告警**：通常仍可正常推理；如需消除可升级板端 RKNPU 驱动。
- **首次加载较慢**：需要把约 2GB 模型读入内存，请耐心等待。
