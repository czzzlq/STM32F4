#test：主机侧单元测试（PC 上运行，无需硬件）
- run_host_tests.sh  一键编译并运行全部测试（依赖任意主机 gcc）
- test_malloc.c      分块内存分配器回归（随机对拍 + magic 越界检测）
- test_ringbuf.c     环形缓冲区回归（回绕 / 满空 / 连续段 / DMA 写指针）
- test_key.c         按键状态机回归（短按 / 消抖 / 长按 / 连发）
- test_stubs.c       OS 临界区空实现桩（malloc / key 共用）
- test_key_stubs.c   可控 tick + GPIO 端口对象 + HAL 空实现（key 专用）
- stubs/             屏蔽真实 HAL / BSP 的最小桩头文件
