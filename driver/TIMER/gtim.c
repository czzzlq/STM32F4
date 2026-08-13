/**
 * @file    gtim.c
 * @brief   通用定时器驱动（定时中断 / PWM 输出 / 输入捕获 / 外部计数）
 * @note    1. 硬件配置见 bsp/bsp_timer.h
 *          2. 中断服务函数直读寄存器，避免 HAL 回调分发开销
 *          3. 注意：GTIM_TIMX_INT 默认使用 TIM3，与 btim.c 的 TIM3 定时中断
 *             互斥，二者请勿同时启用
 */
#include "gtim.h"

/* 定时中断句柄 */
static TIM_HandleTypeDef s_timIntHandle;

/* PWM 输出句柄与配置 */
static TIM_HandleTypeDef s_timPwmHandle;
static TIM_OC_InitTypeDef s_ocPwmConfig;

/* 输入捕获状态与捕获值：
 * s_capSta: [7] 捕获成功标志；[6] 已捕获高电平标志；[5:0] 溢出次数 */
static uint8_t s_capSta = 0;
static uint16_t s_capVal = 0;

/* 输入捕获句柄与配置 */
static TIM_HandleTypeDef s_timCapHandle;
static TIM_IC_InitTypeDef s_icCapConfig;

/* 外部计数句柄与溢出累计 */
static TIM_HandleTypeDef s_timCntHandle;
static uint32_t s_cntOverflow = 0;  /* 溢出次数 */

/**
 * @brief   通用定时器定时中断初始化
 * @param   arr : 自动重装值
 * @param   psc : 时钟预分频数
 * @note    溢出周期 Tout = ((arr+1) * (psc+1)) / 72MHz
 */
void gtim_timx_int_init(uint16_t arr, uint16_t psc)
{
    GTIM_TIMX_INT_CLK_ENABLE();

    s_timIntHandle.Instance = GTIM_TIMX_INT;
    s_timIntHandle.Init.Prescaler = psc;
    s_timIntHandle.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_timIntHandle.Init.Period = arr;
    s_timIntHandle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_Base_Init(&s_timIntHandle);

    HAL_NVIC_SetPriority(GTIM_TIMX_INT_IRQn, 1, 3);
    HAL_NVIC_EnableIRQ(GTIM_TIMX_INT_IRQn);

    HAL_TIM_Base_Start_IT(&s_timIntHandle);
}

/**
 * @brief   通用定时器定时中断服务函数（仅清标志，业务由用户扩展）
 * @note    与 btim.c 的 TIM3 中断互斥，由 GTIM_TIMX_INT_IRQ_ENABLE 控制
 */
#if GTIM_TIMX_INT_IRQ_ENABLE
void GTIM_TIMX_INT_IRQHandler(void)
{
    if (GTIM_TIMX_INT->SR & TIM_SR_UIF)
    {
        GTIM_TIMX_INT->SR = ~TIM_SR_UIF;
    }
}
#endif /* GTIM_TIMX_INT_IRQ_ENABLE */

/* ==================== PWM 输出 ==================== */

/**
 * @brief   通用定时器 PWM 输出初始化（PWM 模式 1，默认 50% 占空比）
 * @param   arr : 自动重装值
 * @param   psc : 时钟预分频数
 */
void gtim_timx_pwm_chy_init(uint16_t arr, uint16_t psc)
{
    GPIO_InitTypeDef gpioInitStruct = {0};

    /* 输出引脚（含重映射） */
    GTIM_TIMX_PWM_CHY_GPIO_CLK_ENABLE();
    GTIM_TIMX_PWM_CHY_CLK_ENABLE();
    GTIM_TIMX_PWM_CHY_GPIO_REMAP();

    gpioInitStruct.Pin = GTIM_TIMX_PWM_CHY_GPIO_PIN;
    gpioInitStruct.Mode = GPIO_MODE_AF_PP;
    gpioInitStruct.Pull = GPIO_PULLUP;
    gpioInitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GTIM_TIMX_PWM_CHY_GPIO_PORT, &gpioInitStruct);

    s_timPwmHandle.Instance = GTIM_TIMX_PWM;
    s_timPwmHandle.Init.Prescaler = psc;
    s_timPwmHandle.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_timPwmHandle.Init.Period = arr;
    s_timPwmHandle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_PWM_Init(&s_timPwmHandle);

    s_ocPwmConfig.OCMode = TIM_OCMODE_PWM1;
    s_ocPwmConfig.Pulse = arr / 2;         /* 50% 占空比 */
    s_ocPwmConfig.OCPolarity = TIM_OCPOLARITY_LOW;
    HAL_TIM_PWM_ConfigChannel(&s_timPwmHandle, &s_ocPwmConfig, GTIM_TIMX_PWM_CHY);
    HAL_TIM_PWM_Start(&s_timPwmHandle, GTIM_TIMX_PWM_CHY);
}

/**
 * @brief   设置 PWM 通道比较值（占空比）
 * @param   compare : 比较值
 */
void gtim_timx_pwm_setcompare(uint32_t compare)
{
    GTIM_TIMX_PWM_CHY_CCRX = compare;
}

/**
 * @brief   读取 PWM 通道捕获值
 */
uint32_t get_timx_capture(void)
{
    return HAL_TIM_ReadCapturedValue(&s_timPwmHandle, GTIM_TIMX_PWM_CHY);
}

/* ==================== 输入捕获（高电平脉宽测量） ==================== */

/**
 * @brief   通用定时器输入捕获初始化（上升沿捕获，配合溢出计数测高电平时间）
 * @param   arr : 自动重装值
 * @param   psc : 时钟预分频数（1us 计数时 psc=71）
 */
void gtim_timx_cap_chy_init(uint16_t arr, uint16_t psc)
{
    GPIO_InitTypeDef gpioInitStruct = {0};

    GTIM_TIMX_CAP_CHY_CLK_ENABLE();
    GTIM_TIMX_CAP_CHY_GPIO_CLK_ENABLE();

    gpioInitStruct.Pin = GTIM_TIMX_CAP_CHY_GPIO_PIN;
    gpioInitStruct.Mode = GPIO_MODE_AF_PP;
    gpioInitStruct.Pull = GPIO_PULLDOWN;
    gpioInitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GTIM_TIMX_CAP_CHY_GPIO_PORT, &gpioInitStruct);

    s_timCapHandle.Instance = GTIM_TIMX_CAP;
    s_timCapHandle.Init.Prescaler = psc;
    s_timCapHandle.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_timCapHandle.Init.Period = arr;
    s_timCapHandle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_IC_Init(&s_timCapHandle);

    s_icCapConfig.ICPolarity = TIM_ICPOLARITY_RISING;
    s_icCapConfig.ICSelection = TIM_ICSELECTION_DIRECTTI;
    s_icCapConfig.ICPrescaler = TIM_ICPSC_DIV1;
    s_icCapConfig.ICFilter = 0;
    HAL_TIM_IC_ConfigChannel(&s_timCapHandle, &s_icCapConfig, GTIM_TIMX_CAP_CHY);
    HAL_TIM_IC_Start_IT(&s_timCapHandle, GTIM_TIMX_CAP_CHY);
    __HAL_TIM_ENABLE_IT(&s_timCapHandle, TIM_IT_UPDATE);

    HAL_NVIC_SetPriority(GTIM_TIMX_CAP_IRQn, 1, 3);
    HAL_NVIC_EnableIRQ(GTIM_TIMX_CAP_IRQn);
}

/**
 * @brief   获取输入捕获状态
 * @retval  [7] 捕获成功；[6] 已捕获高电平；[5:0] 溢出次数
 */
uint8_t gtim_timx_cap_chy_get_sta(void)
{
    return s_capSta;
}

/**
 * @brief   获取输入捕获值
 */
uint16_t gtim_timx_cap_chy_get_val(void)
{
    return s_capVal;
}

/**
 * @brief   输入捕获中断服务函数
 * @note    更新中断用于统计高电平期间的溢出次数（s_capSta[5:0]，最大 63 次，
 *          最长测量 63*65536+65535 ≈ 4.19s @1us 计数）；
 *          与 btim.c 的 TIM5 中断互斥，由 GTIM_TIMX_CAP_IRQ_ENABLE 控制
 */
#if GTIM_TIMX_CAP_IRQ_ENABLE
void GTIM_TIMX_CAP_IRQHandler(void)
{
    uint32_t sr = GTIM_TIMX_CAP->SR;

    if (sr & TIM_SR_UIF)
    {
        GTIM_TIMX_CAP->SR = ~TIM_SR_UIF;

        if ((s_capSta & 0X80) == 0)                 /* 未成功捕获 */
        {
            if (s_capSta & 0X40)                    /* 已捕获到高电平 */
            {
                if ((s_capSta & 0X3F) == 0X3F)
                {
                    s_capSta |= 0X80;               /* 高电平超时，标记捕获成功 */
                    s_capVal = 0XFFFF;
                }
                else
                {
                    s_capSta++;
                }
            }
        }
    }

    if (sr & TIM_SR_CC1IF)
    {
        GTIM_TIMX_CAP->SR = ~TIM_SR_CC1IF;          /* 清捕获标志 */
    }
}
#endif /* GTIM_TIMX_CAP_IRQ_ENABLE */

/* ==================== 外部计数 ==================== */

/**
 * @brief   通用定时器外部计数初始化（外部时钟模式 1，上升沿计数）
 * @param   psc : 时钟预分频数（一般取 0，每来一个脉冲计数一次）
 * @note    计数源为 TIMx_CH1/TIMx_CH2 引脚输入（仅 CH1/CH2 支持）
 */
void gtim_timx_cnt_chy_init(uint16_t psc)
{
    GPIO_InitTypeDef gpioInitStruct = {0};
    TIM_SlaveConfigTypeDef slaveConfig = {0};
    TIM_IC_InitTypeDef icConfig = {0};

    GTIM_TIMX_CNT_CHY_CLK_ENABLE();
    GTIM_TIMX_CNT_CHY_GPIO_CLK_ENABLE();

    gpioInitStruct.Pin = GTIM_TIMX_CNT_CHY_GPIO_PIN;
    gpioInitStruct.Mode = GPIO_MODE_AF_PP;
    gpioInitStruct.Pull = GPIO_PULLDOWN;
    gpioInitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GTIM_TIMX_CNT_CHY_GPIO_PORT, &gpioInitStruct);

    s_timCntHandle.Instance = GTIM_TIMX_CNT;
    s_timCntHandle.Init.Prescaler = psc;
    s_timCntHandle.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_timCntHandle.Init.Period = 65535;
    s_timCntHandle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_timCntHandle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_IC_Init(&s_timCntHandle);

    /* 从模式：外部时钟模式 1，TI1FP1（CH1）作为计数时钟源 */
    slaveConfig.SlaveMode = TIM_SLAVEMODE_EXTERNAL1;
    slaveConfig.InputTrigger = TIM_TS_TI1FP1;
    slaveConfig.TriggerPolarity = TIM_TRIGGERPOLARITY_RISING;
    slaveConfig.TriggerPrescaler = TIM_TRIGGERPRESCALER_DIV1;
    slaveConfig.TriggerFilter = 0x0;
    HAL_TIM_SlaveConfigSynchro(&s_timCntHandle, &slaveConfig);

    icConfig.ICPolarity = TIM_ICPOLARITY_RISING;
    icConfig.ICSelection = TIM_ICSELECTION_DIRECTTI;
    icConfig.ICPrescaler = TIM_ICPSC_DIV1;
    icConfig.ICFilter = 0;
    HAL_TIM_IC_ConfigChannel(&s_timCntHandle, &icConfig, GTIM_TIMX_CNT_CHY);
    HAL_TIM_IC_Start_IT(&s_timCntHandle, GTIM_TIMX_CNT_CHY);

    HAL_NVIC_SetPriority(GTIM_TIMX_CNT_IRQn, 1, 3);
    HAL_NVIC_EnableIRQ(GTIM_TIMX_CNT_IRQn);
}

/**
 * @brief   获取当前计数值（含溢出累计）
 * @retval  计数值 = 溢出次数 * 65536 + CNT
 */
uint32_t gtim_timx_cnt_chy_get_count(void)
{
    return s_cntOverflow * 65536 + __HAL_TIM_GET_COUNTER(&s_timCntHandle);
}

/**
 * @brief   计数器清零并重新开始
 */
void gtim_timx_cnt_chy_restart(void)
{
    __HAL_TIM_DISABLE(&s_timCntHandle);
    s_cntOverflow = 0;
    __HAL_TIM_SET_COUNTER(&s_timCntHandle, 0);
    __HAL_TIM_ENABLE(&s_timCntHandle);
}

/**
 * @brief   外部计数中断服务函数（更新中断：溢出累计）
 */
void GTIM_TIMX_CNT_IRQHandler(void)
{
    if (GTIM_TIMX_CNT->SR & TIM_SR_UIF)
    {
        GTIM_TIMX_CNT->SR = ~TIM_SR_UIF;
        s_cntOverflow++;
    }
}
