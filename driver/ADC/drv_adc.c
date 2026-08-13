/**
 * @file    drv_adc.c
 * @brief   ADC 驱动实现
 * @note    1. 硬件配置见 bsp/bsp_adc.h
 *          2. 温度换算：F103 内部传感器典型值 V25=1.43V，Avg_Slope=4.3mV/°C；
 *             先由 Vrefint（典型 1.20V）反推实际 VDDA，再按 VDDA 换算传感器电压，
 *             消除电源波动引入的误差
 */
#include "drv_adc.h"
#include "bsp_adc.h"

static ADC_HandleTypeDef s_adcHandle;

/**
 * @brief   ADC 初始化（配置内部温度与 Vrefint 通道）
 * @retval  STATUS_OK
 */
status_t drv_adc_init(void)
{
    BSP_ADC_CLK_ENABLE();

    s_adcHandle.Instance = BSP_ADC_PERIPH;
    s_adcHandle.Init.ScanConvMode = ADC_SCAN_DISABLE;
    s_adcHandle.Init.ContinuousConvMode = DISABLE;
    s_adcHandle.Init.DiscontinuousConvMode = DISABLE;
    s_adcHandle.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    s_adcHandle.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    s_adcHandle.Init.NbrOfConversion = 1;

    if (HAL_ADC_Init(&s_adcHandle) != HAL_OK)
    {
        return STATUS_ERR;
    }

    /* 内部温度传感器采样时间取最大（239.5 周期，内阻较大） */
    ADC_ChannelConfTypeDef chanConfig = {0};
    chanConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;

    chanConfig.Channel = BSP_ADC_TEMP_CHANNEL;
    chanConfig.Rank = 1;
    if (HAL_ADC_ConfigChannel(&s_adcHandle, &chanConfig) != HAL_OK)
    {
        return STATUS_ERR;
    }

    chanConfig.Channel = BSP_ADC_VREF_CHANNEL;
    if (HAL_ADC_ConfigChannel(&s_adcHandle, &chanConfig) != HAL_OK)
    {
        return STATUS_ERR;
    }

    return STATUS_OK;
}

/**
 * @brief   指定通道单次转换（阻塞）
 * @param   channel : ADC 通道
 * @retval  12 位转换结果（0~4095）；超时返回 0
 */
uint16_t drv_adc_read(uint32_t channel)
{
    ADC_ChannelConfTypeDef chanConfig = {0};

    chanConfig.Channel = channel;
    chanConfig.Rank = 1;
    chanConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;

    if (HAL_ADC_ConfigChannel(&s_adcHandle, &chanConfig) != HAL_OK)
    {
        return 0;
    }

    if (HAL_ADC_Start(&s_adcHandle) != HAL_OK)
    {
        return 0;
    }

    if (HAL_ADC_PollForConversion(&s_adcHandle, 10) != HAL_OK)
    {
        HAL_ADC_Stop(&s_adcHandle);
        return 0;
    }

    uint16_t value = (uint16_t)HAL_ADC_GetValue(&s_adcHandle);
    HAL_ADC_Stop(&s_adcHandle);

    return value;
}

/**
 * @brief   读取内部温度传感器温度
 * @retval  温度（0.1°C 单位，如 253 = 25.3°C）
 */
int16_t drv_adc_get_temp_tenth(void)
{
    uint16_t tsVal = drv_adc_read(BSP_ADC_TEMP_CHANNEL);
    uint16_t vrefVal = drv_adc_read(BSP_ADC_VREF_CHANNEL);

    if (vrefVal == 0 || tsVal == 0)
    {
        return 0;       /* 转换失败保护 */
    }

    /* VDDA = 1.20V * 4095 / vrefVal；Vsense = VDDA * tsVal / 4095（mV） */
    uint32_t vddaMv = 1200U * 4095U / vrefVal;
    uint32_t vsenseMv = vddaMv * tsVal / 4095U;

    /* 温度 = (V25 - Vsense) / Avg_Slope + 25，V25=1430mV，Slope=4.3mV/°C */
    int32_t tempTenth = (int32_t)((1430 - (int32_t)vsenseMv) * 1000 / 43) + 250;

    return (int16_t)tempTenth;
}

/**
 * @brief   由 Vrefint 推算电源电压
 * @retval  VDDA 电压（mV）
 */
uint16_t drv_adc_get_vdda_mv(void)
{
    uint16_t vrefVal = drv_adc_read(BSP_ADC_VREF_CHANNEL);

    if (vrefVal == 0)
    {
        return 0;
    }

    return (uint16_t)(1200U * 4095U / vrefVal);
}
