/**
  ******************************************************************************
  * @file       bsp_init.c
  * @author     czzzlq
  * @version    V1.0
  * @date       2025-12-26
  * @brief      板级初始化实现 - 统一硬件初始化入口
  * @attention  仅供学习使用
  ******************************************************************************
**/
#include "bsp_init.h"
#include "bsp_gpio.h"
#include "system_hal.h"
#include "delay.h"
#include "usart.h"
#include "drv_led.h"
#include "drv_key.h"
#include "lcd.h"
#include "sram.h"
#include "btim.h"
#include "timer_callback.h"
#include "malloc.h"

/**
 * @brief 板级硬件初始化
 * @note  初始化顺序：系统时钟 -> 延时 -> 串口 -> 外设
 * @return status_t
 */
status_t bsp_init(void)
{
    /* 1. 系统时钟初始化 (72MHz) */
    systemCoreInit();
    
    /* 2. 延时函数初始化 */
    systickDelayInit(72);
    
#if BSP_USE_USART
    /* 3. 串口初始化 */
    usart1Init(115200);
#endif

#if BSP_USE_LCD
    /* 4. LCD初始化 */
    lcd_init();
#endif

#if BSP_USE_SRAM
    /* 5. SRAM初始化 */
    sram_init();
    my_mem_init(SRAMIN);    /* 初始化内部SRAM内存池 */
    my_mem_init(SRAMEX);    /* 初始化外部SRAM内存池 */
#endif

#if BSP_USE_TIMER
    /* 6. 定时器回调管理器初始化 */
    timer_callback_init();
    
    /* 7. 定时器初始化 (10ms周期) */
    btim_tim5_init(10000 - 1, 72 - 1);
#endif

#if BSP_USE_LED
    /* 8. LED初始化 */
    drv_led_init();
#endif

#if BSP_USE_KEY
    /* 9. 按键初始化 (会注册定时器回调) */
    drv_key_init();
#endif

    return STATUS_OK;
}

/**
 * @brief 板级外设初始化(在RTOS任务中调用)
 * @note  用于初始化需要RTOS支持的外设
 * @return status_t
 */
status_t bsp_peripheral_init(void)
{
    /* 预留：初始化需要RTOS支持的外设 */
    return STATUS_OK;
}
