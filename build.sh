#!/usr/bin/env bash
# 直接用 g++ 编译（无需 cmake）。
# 本机 cmake 安装不完整（缺少 Modules 目录），故提供此脚本作为主用构建方式。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

mkdir -p build

echo "==> 编译 chat ..."
g++ -std=c++17 -O2 -I/usr/include \
    src/main.cpp \
    -o build/chat \
    /usr/lib/librkllmrt.so -lpthread

echo "==> 完成: build/chat"
