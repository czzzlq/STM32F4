#!/bin/bash
# malloc 等纯逻辑模块的主机侧回归测试（PC 上运行，无需硬件）
# 依赖：mingw gcc（或任意主机 gcc）
set -e
cd "$(dirname "$0")/.."

GCC=${GCC:-gcc}
BUILD_DIR=$(mktemp -d)
trap 'rm -rf "$BUILD_DIR"' EXIT

"$GCC" -std=c99 -O2 -Wall -Wextra \
    -Itest/stubs -Icommon -Ios/os_wrapper -Imiddleware/MALLOC \
    middleware/MALLOC/malloc.c test/test_malloc.c test/test_stubs.c \
    -o "$BUILD_DIR/test_malloc"

"$BUILD_DIR/test_malloc"
