/**
 * @file    project_config.h
 * @brief   项目统一配置头文件 - 集中管理所有公共头文件
 * @note    在编译器中添加以下包含路径（与 EIDE/.eide/eide.yml 的 incList 保持一致）：
 *          - common
 *          - bsp
 *          - driver/LED  driver/KEY  driver/TIMER  driver/delay  driver/usart
 *          - driver/LCD  driver/SRAM driver/ADC    driver/IWDG
 *          - driver/IIC  driver/24CXX  driver/TOUCH
 *          - core/system
 *          - core/STM32F4xx_HAL_Driver/Inc
 *          - core/CMSIS/Device/ST/STM32F4xx/Include
 *          - core/CMSIS/Include
 *          - middleware/MALLOC
 *          - middleware/LVGL/GUI/lvgl
 *          - middleware/LVGL/GUI/lvgl/src
 *          - middleware/LVGL/GUI/lvgl/examples/porting
 *          - middleware/LVGL/GUI_APP/demos
 *          - utility
 *          - os/os_wrapper
 *          - os/FreeRTOS/include
 *          - os/FreeRTOS/portable/RVDS/ARM_CM3
 * @note    LVGL 的头文件故意不在此处汇总：它是第三方代码，只有需要用到图形库
 *          的编译单元才 include <lvgl.h>，避免把它扩散到整个工程
 */
#ifndef __PROJECT_CONFIG_H__
#define __PROJECT_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif

/*======================= Version Info =======================*/
#define PROJECT_VERSION_MAJOR   1
#define PROJECT_VERSION_MINOR   0
#define PROJECT_VERSION_PATCH   0

/*======================= HAL & CMSIS =======================*/
#include "stm32f4xx_hal.h"
#include "stm32f4xx.h"

/*======================= Common =======================*/
#include "common_types.h"

/*======================= OS =======================*/
#include "os_wrapper.h"

/*======================= BSP =======================*/
#include "bsp_gpio.h"
#include "bsp_init.h"

/*======================= System =======================*/
#include "system_hal.h"

/*======================= Drivers =======================*/
#include "delay.h"
#include "usart.h"
#include "drv_led.h"
#include "drv_key.h"
#include "drv_adc.h"
#include "lcd.h"
#include "sram.h"
#include "btim.h"
#include "timer_callback.h"

/*======================= Middleware / Utility =======================*/
#include "malloc.h"
#include "ringbuf.h"

#ifdef __cplusplus
}
#endif

#endif /* __PROJECT_CONFIG_H__ */
