/**
 * @file    bsp_gpio.h（主机测试桩）
 * @brief   三个按键的 GPIO 映射，指向测试可控的端口对象
 * @note    KEY0/KEY1 低有效（active=0），WKUP 高有效（active=1），
 *          与真实 bsp_gpio.h 一致，测试据此驱动 IDR 模拟电平
 */
#ifndef __BSP_GPIO_H__
#define __BSP_GPIO_H__

#include "stm32f4xx_hal.h"

extern GPIO_TypeDef g_test_key0_port;
extern GPIO_TypeDef g_test_key1_port;
extern GPIO_TypeDef g_test_wkup_port;

#define BSP_KEY0_PORT           (&g_test_key0_port)
#define BSP_KEY0_PIN            0x0001u
#define BSP_KEY0_CLK_ENABLE()   do{}while(0)
#define BSP_KEY0_ACTIVE_LEVEL   0

#define BSP_KEY1_PORT           (&g_test_key1_port)
#define BSP_KEY1_PIN            0x0001u
#define BSP_KEY1_CLK_ENABLE()   do{}while(0)
#define BSP_KEY1_ACTIVE_LEVEL   0

#define BSP_WKUP_PORT           (&g_test_wkup_port)
#define BSP_WKUP_PIN            0x0001u
#define BSP_WKUP_CLK_ENABLE()   do{}while(0)
#define BSP_WKUP_ACTIVE_LEVEL   1

#endif /* __BSP_GPIO_H__ */
