/**
 * @file    bsp_init.c
 * @brief   板级初始化实现 - 统一硬件初始化入口
 */
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
#include "drv_adc.h"
#include "malloc.h"

/**
 * @brief   板级硬件初始化（上电后最先调用）
 * @note    初始化顺序：系统时钟 -> 延时 -> 串口 -> LCD -> SRAM -> 定时器 -> LED -> 按键
 * @retval  STATUS_OK
 */
status_t bsp_init(void)
{
    systemCoreInit();       /* 1. 系统时钟（72MHz） */

    systickDelayInit(72);   /* 2. SysTick 延时（含 FreeRTOS 心跳） */

#if BSP_USE_USART
    usart1Init(115200);     /* 3. 串口1（须在 LCD 前，lcd_init 会 printf） */
#endif

#if BSP_USE_LCD
    lcd_init();             /* 4. LCD */
#endif

#if BSP_USE_SRAM
    sram_init();            /* 5. 外部 SRAM */
    my_mem_init(SRAMIN);    /*    内部 SRAM 内存池 */
    my_mem_init(SRAMEX);    /*    外部 SRAM 内存池 */
#endif

#if BSP_USE_TIMER
    timer_callback_init();              /* 6. 定时器回调管理器 */
    btim_tim5_init(10000 - 1, 72 - 1);  /* 7. TIM5 定时中断（10ms 周期） */
#endif

#if BSP_USE_LED
    drv_led_init();         /* 8. LED */
#endif

#if BSP_USE_KEY
    drv_key_init();         /* 9. 按键（注册 TIM5 扫描回调） */
#endif

#if BSP_USE_ADC
    drv_adc_init();         /* 10. ADC（内部温度/Vrefint） */
#endif

    return STATUS_OK;
}

/**
 * @brief   板级外设初始化（在 RTOS 任务中调用）
 * @note    用于初始化需要 RTOS 支持的外设
 * @retval  STATUS_OK
 */
status_t bsp_peripheral_init(void)
{
    /* 预留：需要 RTOS 支持的外设初始化 */
    return STATUS_OK;
}
