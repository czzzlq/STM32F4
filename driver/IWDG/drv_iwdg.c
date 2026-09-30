/**
 * @file    drv_iwdg.c
 * @brief   独立看门狗驱动实现
 * @note    LSI 时钟在 STM32F407 数据手册中的范围是 17~47kHz、典型 32kHz。
 *          原实现按 40kHz 计算，所有超时都会比请求值短 20%
 *          （40k/32k = 1.25，请求 1000ms 实际约 800ms 就复位），
 *          这里改用典型值 32kHz，并显式拒绝超出 IWDG 量程的请求
 * @note    使能后不可关闭：超时前必须周期性调用 drv_iwdg_feed()
 */
#include "drv_iwdg.h"
#include "stm32f4xx_hal.h"

#define IWDG_LSI_FREQ_HZ    32000U      /* LSI 典型频率（Hz） */

/* IWDG 最大可表达的超时：Reload 上限 0xFFF、最大分频 256 */
#define IWDG_MAX_TIMEOUT_MS ((0xFFFUL * 1000UL * 256UL) / IWDG_LSI_FREQ_HZ)

static IWDG_HandleTypeDef s_iwdgHandle;

/**
 * @brief   初始化独立看门狗
 * @param   timeoutMs : 喂狗超时时间（毫秒）
 * @retval  STATUS_OK / STATUS_ERR_PARAM（0 或超出最大量程）/ STATUS_ERR
 */
status_t drv_iwdg_init(uint32_t timeoutMs)
{
    static const uint16_t s_prescalerDiv[6] = {4, 8, 16, 32, 64, 256};
    static const uint32_t s_prescalerVal[6] =
    {
        IWDG_PRESCALER_4, IWDG_PRESCALER_8, IWDG_PRESCALER_16,
        IWDG_PRESCALER_32, IWDG_PRESCALER_64, IWDG_PRESCALER_256
    };

    uint32_t reload = 0;
    uint32_t i;

    if ((timeoutMs == 0) || (timeoutMs > IWDG_MAX_TIMEOUT_MS))
    {
        /* 超量程时不能靠移位硬凑：原实现移位 6 次后若仍大于 0xFFF 就交给
         * HAL 截断成 12 位，实际超时与请求值完全不符且无法察觉 */
        return STATUS_ERR_PARAM;
    }

    /* 取最小的分频使 Reload 落进 12 位：Reload = timeout(ms) * LSI / (1000 * div) */
    for (i = 0; i < ARRAY_SIZE(s_prescalerDiv); i++)
    {
        reload = (timeoutMs * IWDG_LSI_FREQ_HZ) / (1000U * s_prescalerDiv[i]);

        if (reload <= 0xFFF)
        {
            break;
        }
    }

    if (reload == 0)
    {
        reload = 1;                     /* 极短超时至少给 1 个计数 */
    }

    s_iwdgHandle.Instance = IWDG;
    s_iwdgHandle.Init.Prescaler = s_prescalerVal[i];
    s_iwdgHandle.Init.Reload = reload;

    return (HAL_IWDG_Init(&s_iwdgHandle) == HAL_OK) ? STATUS_OK : STATUS_ERR;
}

/**
 * @brief   喂狗（刷新看门狗计数器）
 */
void drv_iwdg_feed(void)
{
    HAL_IWDG_Refresh(&s_iwdgHandle);
}
