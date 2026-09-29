/**
 * @file    stm32f4xx_hal.h（主机测试桩）
 * @brief   提供 drv_key.c 主机编译所需的最小 HAL GPIO 类型与接口
 * @note    GPIO_TypeDef 仅含 IDR（按键扫描直读），ODR 占位保持结构体可扩展
 */
#ifndef __STM32F4XX_HAL_H__
#define __STM32F4XX_HAL_H__

#include <stdint.h>

typedef struct
{
    volatile uint32_t IDR;      /* 输入数据寄存器（测试中模拟引脚电平） */
    volatile uint32_t ODR;      /* 输出数据寄存器（未用，占位） */
} GPIO_TypeDef;

typedef struct
{
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
} GPIO_InitTypeDef;

/* 与 HAL 枚举值一致的占位宏（仅需能被 key_msp_init 引用） */
#define GPIO_MODE_INPUT        0x00000000U
#define GPIO_PULLUP            0x00000001U
#define GPIO_PULLDOWN          0x00000002U
#define GPIO_SPEED_FREQ_HIGH   0x00000003U

void HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init);
uint32_t HAL_GetTick(void);

#endif /* __STM32F4XX_HAL_H__ */
