#!/usr/bin/env bash
# 下载预编译好的 DeepSeek-R1-Distill-Qwen-1.5B RKLLM 模型（RK3588 / W8A8）
# 来源：ModelScope radxa/DeepSeek-R1-Distill-Qwen-1.5B_RKLLM
# 支持断点续传（curl -C -）。约 2.05GB。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
MODEL_DIR="${PROJECT_DIR}/models"

MODEL_FILE="DeepSeek-R1-Distill-Qwen-1.5B_W8A8_RK3588.rkllm"
REPO="radxa/DeepSeek-R1-Distill-Qwen-1.5B_RKLLM"
URL="https://modelscope.cn/models/${REPO}/resolve/master/${MODEL_FILE}"

mkdir -p "${MODEL_DIR}"
DEST="${MODEL_DIR}/${MODEL_FILE}"

echo "==> 模型目标路径: ${DEST}"
echo "==> 下载地址: ${URL}"
echo "==> 文件较大（约 2GB），如中断可重复执行本脚本断点续传。"
echo

curl -L -C - --fail --retry 5 --retry-delay 3 \
     -o "${DEST}" \
     "${URL}"

echo
echo "==> 下载完成: ${DEST}"
ls -lh "${DEST}"
