/**
 * @file    drv_iwdg.c
 * @brief   独立看门狗驱动实现（LSI 约 40kHz）
 * @note    使能后不可关闭：超时前必须周期性调用 drv_iwdg_feed()
 */
#include "drv_iwdg.h"
#include "stm32f1xx_hal.h"

static IWDG_HandleTypeDef s_iwdgHandle;

/**
 * @brief   初始化独立看门狗
 * @param   timeoutMs : 喂狗超时时间（毫秒，LSI 按标称 40kHz 计算）
 * @retval  STATUS_OK / STATUS_ERR_PARAM
 */
status_t drv_iwdg_init(uint32_t timeoutMs)
{
    if (timeoutMs == 0)
    {
        return STATUS_ERR_PARAM;
    }

    s_iwdgHandle.Instance = IWDG;
    s_iwdgHandle.Init.Prescaler = IWDG_PRESCALER_4;
    s_iwdgHandle.Init.Reload = timeoutMs * 10;      /* ÷4 后 10kHz，1 计数 = 0.1ms */

    /* 超时时间换算为计数，必要时加大分频保证 Reload 不超 0xFFF */
    if (s_iwdgHandle.Init.Reload > 0xFFF)
    {
        s_iwdgHandle.Init.Reload >>= 1;
        s_iwdgHandle.Init.Prescaler = IWDG_PRESCALER_8;
    }
    if (s_iwdgHandle.Init.Reload > 0xFFF)
    {
        s_iwdgHandle.Init.Reload >>= 1;
        s_iwdgHandle.Init.Prescaler = IWDG_PRESCALER_16;
    }
    if (s_iwdgHandle.Init.Reload > 0xFFF)
    {
        s_iwdgHandle.Init.Reload >>= 1;
        s_iwdgHandle.Init.Prescaler = IWDG_PRESCALER_32;
    }
    if (s_iwdgHandle.Init.Reload > 0xFFF)
    {
        s_iwdgHandle.Init.Reload >>= 1;
        s_iwdgHandle.Init.Prescaler = IWDG_PRESCALER_64;
    }
    if (s_iwdgHandle.Init.Reload > 0xFFF)
    {
        s_iwdgHandle.Init.Reload >>= 1;
        s_iwdgHandle.Init.Prescaler = IWDG_PRESCALER_128;
    }
    if (s_iwdgHandle.Init.Reload > 0xFFF)
    {
        s_iwdgHandle.Init.Reload >>= 1;
        s_iwdgHandle.Init.Prescaler = IWDG_PRESCALER_256;
    }

    if (s_iwdgHandle.Init.Reload == 0)
    {
        s_iwdgHandle.Init.Reload = 1;
    }

    return (HAL_IWDG_Init(&s_iwdgHandle) == HAL_OK) ? STATUS_OK : STATUS_ERR;
}

/**
 * @brief   喂狗（刷新看门狗计数器）
 */
void drv_iwdg_feed(void)
{
    HAL_IWDG_Refresh(&s_iwdgHandle);
}
