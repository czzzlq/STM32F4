#!/bin/bash
# 纯逻辑模块的主机侧回归测试（PC 上运行，无需硬件）
# 依赖：mingw gcc（或任意主机 gcc）
set -e
cd "$(dirname "$0")/.."

GCC=${GCC:-gcc}
CFLAGS="-std=c99 -O2 -Wall -Wextra"
BUILD_DIR=$(mktemp -d)
trap 'rm -rf "$BUILD_DIR"' EXIT

# 1. malloc 分块内存分配器（分配对拍 + 越界检测）
"$GCC" $CFLAGS \
    -Itest/stubs -Icommon -Ios/os_wrapper -Imiddleware/MALLOC \
    middleware/MALLOC/malloc.c test/test_malloc.c test/test_stubs.c \
    -o "$BUILD_DIR/test_malloc"
"$BUILD_DIR/test_malloc"

# 2. ringbuf 环形缓冲区（回绕 / 满空 / 连续段）
"$GCC" $CFLAGS \
    -Iutility \
    utility/ringbuf.c test/test_ringbuf.c \
    -o "$BUILD_DIR/test_ringbuf"
"$BUILD_DIR/test_ringbuf"

# 3. 按键状态机（短按 / 消抖 / 长按 / 连发）
"$GCC" $CFLAGS \
    -Itest/stubs -Icommon -Idriver/KEY -Idriver/TIMER -Ios/os_wrapper \
    driver/KEY/drv_key.c driver/TIMER/timer_callback.c \
    test/test_key.c test/test_stubs.c test/test_key_stubs.c \
    -o "$BUILD_DIR/test_key"
"$BUILD_DIR/test_key"

echo "ALL HOST TESTS PASSED"
