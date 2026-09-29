/**
 * @file    bsp_adc.h
 * @brief   ADC 板级硬件配置
 * @note    软硬件分离：驱动层不感知外设实例，换板/换外设只需修改本文件
 */
#ifndef __BSP_ADC_H__
#define __BSP_ADC_H__

#include "stm32f4xx.h"

/* ADC 外设实例 */
#define BSP_ADC_PERIPH                   ADC1
#define BSP_ADC_CLK_ENABLE()             do{ __HAL_RCC_ADC1_CLK_ENABLE(); }while(0)

/* 内部通道（无需外部接线，用于自检与电源监测） */
#define BSP_ADC_TEMP_CHANNEL             ADC_CHANNEL_TEMPSENSOR   /* 内部温度传感器（CH16） */
#define BSP_ADC_VREF_CHANNEL             ADC_CHANNEL_VREFINT      /* 内部参考电压（CH17） */

#endif /* __BSP_ADC_H__ */
