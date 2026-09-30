/**
 * @file    drv_adc.c
 * @brief   ADC 驱动实现
 * @note    1. 硬件配置见 bsp/bsp_adc.h
 *          2. 温度换算（STM32F4 数据手册）：V25=0.76V，Avg_Slope=2.5mV/°C；
 *             Vrefint 典型 1.21V，先由 Vrefint 反推实际 VDDA，再按 VDDA 换算传感器电压，
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
    /* ADC 时钟 = PCLK2 / 4（F4 的 ADC 最高 36MHz；PCLK2=84MHz 时 DIV2=42MHz 超限） */
    s_adcHandle.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    s_adcHandle.Init.Resolution = ADC_RESOLUTION_12B;
    s_adcHandle.Init.ScanConvMode = DISABLE;
    s_adcHandle.Init.ContinuousConvMode = DISABLE;
    s_adcHandle.Init.DiscontinuousConvMode = DISABLE;
    s_adcHandle.Init.NbrOfDiscConversion = 0;
    s_adcHandle.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    s_adcHandle.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    s_adcHandle.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    s_adcHandle.Init.NbrOfConversion = 1;
    s_adcHandle.Init.DMAContinuousRequests = DISABLE;
    s_adcHandle.Init.EOCSelection = ADC_EOC_SINGLE_CONV;

    if (HAL_ADC_Init(&s_adcHandle) != HAL_OK)
    {
        return STATUS_ERR;
    }

    /* 未做 ADC 自校准（ADC_CR2_CAL）：本工程使用的这份 F4 HAL 既没有
     * HAL_ADCEx_Calibration_Start()，其 HAL_ADC_Init() 内部也不做校准，
     * 而校准能把转换结果的固定偏置消掉。当前只用于内部温度/Vrefint 这类
     * 粗测量（数据手册本身给出的温度传感器绝对精度仅 ±45°C），偏置可忽略。
     * 若后续要用 ADC 做精密测量，需自行按 RM0090 §11.13 的时序补上：
     * ADON=1 -> 等 tSTAB -> CAL=1 -> 等 CAL 被硬件清 0 */

    /* 内部温度/Vrefint 通道内阻较大，采样时间取最大（480 周期 = 22.8us @21MHz） */
    ADC_ChannelConfTypeDef chanConfig = {0};
    chanConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;
    chanConfig.Rank = 1;                             /* 规则组序号从 1 开始 */

    chanConfig.Channel = BSP_ADC_TEMP_CHANNEL;
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
    chanConfig.Rank = 1;                             /* 规则组序号从 1 开始（本 HAL 无 ADC_REGULAR_RANK_x 宏） */
    chanConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;

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

    /* VDDA = 1.21V * 4095 / vrefVal；Vsense = VDDA * tsVal / 4095（mV） */
    uint32_t vddaMv = 1210U * 4095U / vrefVal;
    uint32_t vsenseMv = vddaMv * tsVal / 4095U;

    /* 温度 = 25 + (V25 - Vsense) / Avg_Slope；V25=760mV，Slope=2.5mV/°C
     * 换算为 0.1°C：(760 - Vsense)[mV] * 100 / 25[0.1°C] + 250 */
    int32_t tempTenth = (int32_t)((760 - (int32_t)vsenseMv) * 100 / 25) + 250;

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

    return (uint16_t)(1210U * 4095U / vrefVal);
}
