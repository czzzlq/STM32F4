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
TIM_HandleTypeDef g_timx_handle;

/* PWM 输出句柄与配置 */
TIM_HandleTypeDef g_timx_pwm_chy_handle;
TIM_OC_InitTypeDef g_timx_oc_pwm_chy_handle;

/* 输入捕获状态与捕获值：
 * g_timxchy_cap_sta: [7] 捕获成功标志；[6] 已捕获高电平标志；[5:0] 溢出次数 */
uint8_t g_timxchy_cap_sta = 0;
uint16_t g_timxchy_cap_val = 0;

/* 输入捕获句柄与配置 */
TIM_HandleTypeDef g_timx_cap_chy_handler;
TIM_IC_InitTypeDef g_timx_ic_cap_chy_handler;

/* 外部计数句柄与溢出累计 */
TIM_HandleTypeDef g_timx_cnt_chy_handler;
uint32_t g_timxchy_cnt_ofcnt = 0;   /* 溢出次数 */

/**
 * @brief   通用定时器定时中断初始化
 * @param   arr : 自动重装值
 * @param   psc : 时钟预分频数
 * @note    溢出周期 Tout = ((arr+1) * (psc+1)) / 72MHz
 */
void gtim_timx_int_init(uint16_t arr, uint16_t psc)
{
    GTIM_TIMX_INT_CLK_ENABLE();

    g_timx_handle.Instance = GTIM_TIMX_INT;
    g_timx_handle.Init.Prescaler = psc;
    g_timx_handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_timx_handle.Init.Period = arr;
    g_timx_handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_Base_Init(&g_timx_handle);

    HAL_NVIC_SetPriority(GTIM_TIMX_INT_IRQn, 1, 3);
    HAL_NVIC_EnableIRQ(GTIM_TIMX_INT_IRQn);

    HAL_TIM_Base_Start_IT(&g_timx_handle);
}

/**
 * @brief   通用定时器定时中断服务函数（仅清标志，业务由用户扩展）
 */
void GTIM_TIMX_INT_IRQHandler(void)
{
    if (GTIM_TIMX_INT->SR & TIM_SR_UIF)
    {
        GTIM_TIMX_INT->SR = ~TIM_SR_UIF;
    }
}

/* ==================== PWM 输出 ==================== */

/**
 * @brief   通用定时器 PWM 输出初始化（PWM 模式 1，默认 50% 占空比）
 * @param   arr : 自动重装值
 * @param   psc : 时钟预分频数
 */
void gtim_timx_pwm_chy_init(uint16_t arr, uint16_t psc)
{
    GPIO_InitTypeDef gpio_init_struct = {0};

    /* 输出引脚（含重映射） */
    GTIM_TIMX_PWM_CHY_GPIO_CLK_ENABLE();
    GTIM_TIMX_PWM_CHY_CLK_ENABLE();
    GTIM_TIMX_PWM_CHY_GPIO_REMAP();

    gpio_init_struct.Pin = GTIM_TIMX_PWM_CHY_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_AF_PP;
    gpio_init_struct.Pull = GPIO_PULLUP;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GTIM_TIMX_PWM_CHY_GPIO_PORT, &gpio_init_struct);

    g_timx_pwm_chy_handle.Instance = GTIM_TIMX_PWM;
    g_timx_pwm_chy_handle.Init.Prescaler = psc;
    g_timx_pwm_chy_handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_timx_pwm_chy_handle.Init.Period = arr;
    g_timx_pwm_chy_handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_PWM_Init(&g_timx_pwm_chy_handle);

    g_timx_oc_pwm_chy_handle.OCMode = TIM_OCMODE_PWM1;
    g_timx_oc_pwm_chy_handle.Pulse = arr / 2;       /* 50% 占空比 */
    g_timx_oc_pwm_chy_handle.OCPolarity = TIM_OCPOLARITY_LOW;
    HAL_TIM_PWM_ConfigChannel(&g_timx_pwm_chy_handle, &g_timx_oc_pwm_chy_handle, GTIM_TIMX_PWM_CHY);
    HAL_TIM_PWM_Start(&g_timx_pwm_chy_handle, GTIM_TIMX_PWM_CHY);
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
    return HAL_TIM_ReadCapturedValue(&g_timx_pwm_chy_handle, GTIM_TIMX_PWM_CHY);
}

/* ==================== 输入捕获（高电平脉宽测量） ==================== */

/**
 * @brief   通用定时器输入捕获初始化（上升沿捕获，配合溢出计数测高电平时间）
 * @param   arr : 自动重装值
 * @param   psc : 时钟预分频数（1us 计数时 psc=71）
 */
void gtim_timx_cap_chy_init(uint16_t arr, uint16_t psc)
{
    GPIO_InitTypeDef gpio_init_struct = {0};

    GTIM_TIMX_CAP_CHY_CLK_ENABLE();
    GTIM_TIMX_CAP_CHY_GPIO_CLK_ENABLE();

    gpio_init_struct.Pin = GTIM_TIMX_CAP_CHY_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_AF_PP;
    gpio_init_struct.Pull = GPIO_PULLDOWN;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GTIM_TIMX_CAP_CHY_GPIO_PORT, &gpio_init_struct);

    g_timx_cap_chy_handler.Instance = GTIM_TIMX_CAP;
    g_timx_cap_chy_handler.Init.Prescaler = psc;
    g_timx_cap_chy_handler.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_timx_cap_chy_handler.Init.Period = arr;
    g_timx_cap_chy_handler.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_IC_Init(&g_timx_cap_chy_handler);

    g_timx_ic_cap_chy_handler.ICPolarity = TIM_ICPOLARITY_RISING;
    g_timx_ic_cap_chy_handler.ICSelection = TIM_ICSELECTION_DIRECTTI;
    g_timx_ic_cap_chy_handler.ICPrescaler = TIM_ICPSC_DIV1;
    g_timx_ic_cap_chy_handler.ICFilter = 0;
    HAL_TIM_IC_ConfigChannel(&g_timx_cap_chy_handler, &g_timx_ic_cap_chy_handler, GTIM_TIMX_CAP_CHY);
    HAL_TIM_IC_Start_IT(&g_timx_cap_chy_handler, GTIM_TIMX_CAP_CHY);
    __HAL_TIM_ENABLE_IT(&g_timx_cap_chy_handler, TIM_IT_UPDATE);

    HAL_NVIC_SetPriority(GTIM_TIMX_CAP_IRQn, 1, 3);
    HAL_NVIC_EnableIRQ(GTIM_TIMX_CAP_IRQn);
}

/**
 * @brief   输入捕获中断服务函数
 * @note    更新中断用于统计高电平期间的溢出次数（cap_sta[5:0]，最大 63 次，
 *          最长测量 63*65536+65535 ≈ 4.19s @1us 计数）
 */
void GTIM_TIMX_CAP_IRQHandler(void)
{
    uint32_t sr = GTIM_TIMX_CAP->SR;

    if (sr & TIM_SR_UIF)
    {
        GTIM_TIMX_CAP->SR = ~TIM_SR_UIF;

        if ((g_timxchy_cap_sta & 0X80) == 0)        /* 未成功捕获 */
        {
            if (g_timxchy_cap_sta & 0X40)           /* 已捕获到高电平 */
            {
                if ((g_timxchy_cap_sta & 0X3F) == 0X3F)
                {
                    g_timxchy_cap_sta |= 0X80;      /* 高电平超时，标记捕获成功 */
                    g_timxchy_cap_val = 0XFFFF;
                }
                else
                {
                    g_timxchy_cap_sta++;
                }
            }
        }
    }

    if (sr & TIM_SR_CC1IF)
    {
        GTIM_TIMX_CAP->SR = ~TIM_SR_CC1IF;          /* 清捕获标志 */
    }
}

/* ==================== 外部计数 ==================== */

/**
 * @brief   通用定时器外部计数初始化（外部时钟模式 1，上升沿计数）
 * @param   psc : 时钟预分频数（一般取 0，每来一个脉冲计数一次）
 * @note    计数源为 TIMx_CH1/TIMx_CH2 引脚输入（仅 CH1/CH2 支持）
 */
void gtim_timx_cnt_chy_init(uint16_t psc)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    TIM_SlaveConfigTypeDef tim_slave_config_handle = {0};
    TIM_IC_InitTypeDef timx_ic_cnt_chy_handler = {0};

    GTIM_TIMX_CNT_CHY_CLK_ENABLE();
    GTIM_TIMX_CNT_CHY_GPIO_CLK_ENABLE();

    gpio_init_struct.Pin = GTIM_TIMX_CNT_CHY_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_AF_PP;
    gpio_init_struct.Pull = GPIO_PULLDOWN;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GTIM_TIMX_CNT_CHY_GPIO_PORT, &gpio_init_struct);

    g_timx_cnt_chy_handler.Instance = GTIM_TIMX_CNT;
    g_timx_cnt_chy_handler.Init.Prescaler = psc;
    g_timx_cnt_chy_handler.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_timx_cnt_chy_handler.Init.Period = 65535;
    g_timx_cnt_chy_handler.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    g_timx_cnt_chy_handler.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_IC_Init(&g_timx_cnt_chy_handler);

    /* 从模式：外部时钟模式 1，TI1FP1（CH1）作为计数时钟源 */
    tim_slave_config_handle.SlaveMode = TIM_SLAVEMODE_EXTERNAL1;
    tim_slave_config_handle.InputTrigger = TIM_TS_TI1FP1;
    tim_slave_config_handle.TriggerPolarity = TIM_TRIGGERPOLARITY_RISING;
    tim_slave_config_handle.TriggerPrescaler = TIM_TRIGGERPRESCALER_DIV1;
    tim_slave_config_handle.TriggerFilter = 0x0;
    HAL_TIM_SlaveConfigSynchro(&g_timx_cnt_chy_handler, &tim_slave_config_handle);

    timx_ic_cnt_chy_handler.ICPolarity = TIM_ICPOLARITY_RISING;
    timx_ic_cnt_chy_handler.ICSelection = TIM_ICSELECTION_DIRECTTI;
    timx_ic_cnt_chy_handler.ICPrescaler = TIM_ICPSC_DIV1;
    timx_ic_cnt_chy_handler.ICFilter = 0;
    HAL_TIM_IC_ConfigChannel(&g_timx_cnt_chy_handler, &timx_ic_cnt_chy_handler, GTIM_TIMX_CNT_CHY);
    HAL_TIM_IC_Start_IT(&g_timx_cnt_chy_handler, GTIM_TIMX_CNT_CHY);

    HAL_NVIC_SetPriority(GTIM_TIMX_CNT_IRQn, 1, 3);
    HAL_NVIC_EnableIRQ(GTIM_TIMX_CNT_IRQn);
}

/**
 * @brief   获取当前计数值（含溢出累计）
 * @retval  计数值 = 溢出次数 * 65536 + CNT
 */
uint32_t gtim_timx_cnt_chy_get_count(void)
{
    return g_timxchy_cnt_ofcnt * 65536 + __HAL_TIM_GET_COUNTER(&g_timx_cnt_chy_handler);
}

/**
 * @brief   计数器清零并重新开始
 */
void gtim_timx_cnt_chy_restart(void)
{
    __HAL_TIM_DISABLE(&g_timx_cnt_chy_handler);
    g_timxchy_cnt_ofcnt = 0;
    __HAL_TIM_SET_COUNTER(&g_timx_cnt_chy_handler, 0);
    __HAL_TIM_ENABLE(&g_timx_cnt_chy_handler);
}

/**
 * @brief   外部计数中断服务函数（更新中断：溢出累计）
 */
void GTIM_TIMX_CNT_IRQHandler(void)
{
    if (GTIM_TIMX_CNT->SR & TIM_SR_UIF)
    {
        GTIM_TIMX_CNT->SR = ~TIM_SR_UIF;
        g_timxchy_cnt_ofcnt++;
    }
}
