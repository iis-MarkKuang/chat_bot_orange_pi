#!/usr/bin/env bash
# 下载第三方依赖：nlohmann/json 单头文件到 third_party/nlohmann/json.hpp
# github raw 优先，失败回退 jsdelivr。

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEST_DIR="${PROJECT_DIR}/third_party/nlohmann"
DEST="${DEST_DIR}/json.hpp"

mkdir -p "${DEST_DIR}"

if [ -s "${DEST}" ]; then
  echo "==> 已存在: ${DEST}（跳过下载）"
  exit 0
fi

URLS=(
  "https://raw.githubusercontent.com/nlohmann/json/develop/single_include/nlohmann/json.hpp"
  "https://cdn.jsdelivr.net/gh/nlohmann/json/single_include/nlohmann/json.hpp"
)

for url in "${URLS[@]}"; do
  echo "==> 尝试下载: ${url}"
  if curl -L --fail --max-time 60 -o "${DEST}.tmp" "${url}"; then
    mv "${DEST}.tmp" "${DEST}"
    echo "==> 完成: ${DEST}"
    ls -lh "${DEST}"
    exit 0
  fi
  echo "    失败，尝试下一个源..."
done

rm -f "${DEST}.tmp"
echo "!! 下载 nlohmann/json 失败。请检查网络，或手动放置到 ${DEST}" >&2
exit 1
