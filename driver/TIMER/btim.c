/**
 * @file    btim.c
 * @brief   基本定时器定时中断驱动
 * @note    1. 定时器时钟：APB1 预分频 ≥2 时定时器时钟 = APB1 时钟 × 2，即 72MHz
 *          2. 溢出周期 Tout = ((arr+1) * (psc+1)) / 72MHz
 *          3. 中断服务函数直读寄存器（绕过 HAL 回调分发），更新事件经
 *             timer_callback_execute 分发给注册的回调
 */
#include "btim.h"
#include "timer_callback.h"

static TIM_HandleTypeDef s_tim3Handle;
static TIM_HandleTypeDef s_tim5Handle;

/**
 * @brief   TIM3 定时中断初始化
 * @param   arr : 自动重装值
 * @param   psc : 时钟预分频数
 * @note    中断优先级 4/0：不受 FreeRTOS 管理（FreeRTOS 管理范围为 5~15）
 */
void btim_tim3_init(uint16_t arr, uint16_t psc)
{
    BTIM_TIM3_INT_CLK_ENABLE();

    s_tim3Handle.Instance = BTIM_TIM3_INT;
    s_tim3Handle.Init.Prescaler = psc;
    s_tim3Handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_tim3Handle.Init.Period = arr;
    s_tim3Handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_Base_Init(&s_tim3Handle);

    HAL_NVIC_SetPriority(BTIM_TIM3_INT_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(BTIM_TIM3_INT_IRQn);

    HAL_TIM_Base_Start_IT(&s_tim3Handle);
}

/**
 * @brief   TIM5 定时中断初始化
 * @param   arr : 自动重装值
 * @param   psc : 时钟预分频数
 * @note    中断优先级 6/0：受 FreeRTOS 管理，ISR 内可调用 FreeRTOS API
 */
void btim_tim5_init(uint16_t arr, uint16_t psc)
{
    BTIM_TIM5_INT_CLK_ENABLE();

    s_tim5Handle.Instance = BTIM_TIM5_INT;
    s_tim5Handle.Init.Prescaler = psc;
    s_tim5Handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_tim5Handle.Init.Period = arr;
    s_tim5Handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_Base_Init(&s_tim5Handle);

    HAL_NVIC_SetPriority(BTIM_TIM5_INT_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(BTIM_TIM5_INT_IRQn);

    HAL_TIM_Base_Start_IT(&s_tim5Handle);
}

/**
 * @brief   TIM3 中断服务函数（寄存器直读，开销低于 HAL 分发）
 */
void BTIM_TIM3_INT_IRQHandler(void)
{
    if (BTIM_TIM3_INT->SR & TIM_SR_UIF)
    {
        BTIM_TIM3_INT->SR = ~TIM_SR_UIF;    /* 写 0 清 UIF */
        timer_callback_execute(TIMER_ID_TIM3);
    }
}

/**
 * @brief   TIM5 中断服务函数（寄存器直读，开销低于 HAL 分发）
 */
void BTIM_TIM5_INT_IRQHandler(void)
{
    if (BTIM_TIM5_INT->SR & TIM_SR_UIF)
    {
        BTIM_TIM5_INT->SR = ~TIM_SR_UIF;
        timer_callback_execute(TIMER_ID_TIM5);
    }
}
