/**
 * @file    test_key_stubs.c
 * @brief   按键测试桩实现：可控 tick + GPIO 端口对象 + HAL 空实现
 */
#include "stm32f4xx_hal.h"
#include "bsp_gpio.h"

uint32_t g_test_tick = 0;           /* 可控系统 tick，测试推进时间 */

GPIO_TypeDef g_test_key0_port;      /* KEY0 端口对象（IDR 模拟电平） */
GPIO_TypeDef g_test_key1_port;      /* KEY1 端口对象 */
GPIO_TypeDef g_test_wkup_port;      /* WKUP 端口对象 */

uint32_t HAL_GetTick(void)
{
    return g_test_tick;
}

void HAL_GPIO_Init(GPIO_TypeDef *GPIOx, GPIO_InitTypeDef *GPIO_Init)
{
    (void)GPIOx;
    (void)GPIO_Init;
}
