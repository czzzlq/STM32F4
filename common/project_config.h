/**
  ******************************************************************************
  * @file       project_config.h
  * @author     czzzlq
  * @version    V1.0
  * @date       2025-12-26
  * @brief      项目统一配置头文件 - 集中管理所有公共头文件
  * @attention  仅供学习使用
  * @note       在编译器中添加以下包含路径:
  *             - common
  *             - bsp
  *             - driver/LED
  *             - driver/KEY
  *             - driver/TIMER
  *             - driver/delay
  *             - driver/usart
  *             - driver/LCD
  *             - driver/SRAM
  *             - driver/ADC
  *             - driver/IWDG
  *             - core/system
  *             - core/STM32F4xx_HAL_Driver/Inc
  *             - core/CMSIS/Device/ST/STM32F4xx/Include
  *             - core/CMSIS/Include
  *             - middleware/MALLOC
  *             - utility
  *             - os/os_wrapper
  *             - os/FreeRTOS/include
  *             - os/FreeRTOS/portable/RVDS/ARM_CM3
  ******************************************************************************
**/
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
#include "lcd.h"
#include "sram.h"
#include "btim.h"
#include "timer_callback.h"

/*======================= Middleware =======================*/
#include "malloc.h"

#ifdef __cplusplus
}
#endif

#endif /* __PROJECT_CONFIG_H__ */
