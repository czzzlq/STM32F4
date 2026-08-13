/**
 * @file    drv_adc.h
 * @brief   ADC 驱动接口（单次转换 / 内部温度 / 电源电压监测）
 * @note    硬件配置见 bsp/bsp_adc.h
 */
#ifndef __DRV_ADC_H__
#define __DRV_ADC_H__

#include "common_types.h"
#include "stm32f1xx_hal.h"

/* ADC 驱动初始化（配置温度/Vrefint 内部通道） */
status_t drv_adc_init(void);
/* 指定通道单次转换（阻塞，约 240 采样周期 + 12.5 转换周期） */
uint16_t drv_adc_read(uint32_t channel);
/* 读取内部温度传感器，返回 0.1°C 单位温度 */
int16_t drv_adc_get_temp_tenth(void);
/* 由 Vrefint 推算 VDDA 电源电压，返回 mV */
uint16_t drv_adc_get_vdda_mv(void);

#endif /* __DRV_ADC_H__ */
