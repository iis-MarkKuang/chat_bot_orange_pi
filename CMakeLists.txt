cmake_minimum_required(VERSION 3.10)
project(deepseek_chat_cli CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

# RK3588 NPU 运行时（系统已预装）
set(RKLLM_INCLUDE_DIR "/usr/include")
set(RKLLM_LIB "/usr/lib/librkllmrt.so")

if(NOT EXISTS "${RKLLM_INCLUDE_DIR}/rkllm.h")
  message(FATAL_ERROR "未找到 ${RKLLM_INCLUDE_DIR}/rkllm.h，请确认 RKLLM 运行时已安装。")
endif()
if(NOT EXISTS "${RKLLM_LIB}")
  message(FATAL_ERROR "未找到 ${RKLLM_LIB}，请确认 RKLLM 运行时已安装。")
endif()

# 第三方依赖：nlohmann/json 单头文件（用 scripts/fetch_deps.sh 下载到 third_party/）
set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
if(NOT EXISTS "${THIRD_PARTY_DIR}/nlohmann/json.hpp")
  message(FATAL_ERROR "未找到 ${THIRD_PARTY_DIR}/nlohmann/json.hpp，请先运行 bash scripts/fetch_deps.sh")
endif()

add_executable(chat src/main.cpp src/tools.cpp)
target_include_directories(chat PRIVATE "${RKLLM_INCLUDE_DIR}" "${THIRD_PARTY_DIR}")
target_link_libraries(chat "${RKLLM_LIB}" pthread)
