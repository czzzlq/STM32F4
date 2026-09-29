/**
 * @file    stm32f4xx.h（主机测试桩）
 * @brief   提供 malloc.c 主机编译所需的最小类型与编译特性宏
 */
#ifndef __STM32F4XX_H__
#define __STM32F4XX_H__

#include <stdint.h>

/* AC5 关键字在 GCC 下的等价（仅测试用） */
#define __align(x)      __attribute__((aligned(x)))
#define __ALIGNED(x)    __attribute__((aligned(x)))

#endif /* __STM32F4XX_H__ */
