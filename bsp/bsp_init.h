/**
 * @file    bsp_init.h
 * @brief   板级初始化接口 - 统一硬件初始化入口
 */
#ifndef __BSP_INIT_H__
#define __BSP_INIT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "common_types.h"

/*======================= Board Configuration =======================*/
/* 板级功能使能开关 */
#define BSP_USE_LED         1   /* LED */
#define BSP_USE_KEY         1   /* 按键 */
#define BSP_USE_LCD         1   /* LCD */
#define BSP_USE_USART       1   /* 串口 */
#define BSP_USE_SRAM        1   /* 外部 SRAM */
#define BSP_USE_TIMER       1   /* 定时器 */

/*======================= Board Init Functions =======================*/
/**
 * @brief   板级硬件初始化
 * @note    初始化顺序：系统时钟 -> 延时 -> 串口 -> 外设
 * @retval  STATUS_OK
 */
status_t bsp_init(void);

/**
 * @brief   板级外设初始化（在 RTOS 任务中调用）
 * @note    用于初始化需要 RTOS 支持的外设
 * @retval  STATUS_OK
 */
status_t bsp_peripheral_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_INIT_H__ */
