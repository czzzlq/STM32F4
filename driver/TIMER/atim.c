/**
 * @file    atim.c
 * @brief   高级定时器驱动（输出比较 PWM / 互补 PWM / PWM 输入测量）
 * @note    1. 硬件配置见 bsp/bsp_timer.h
 *          2. TIM1/TIM8 的更新中断与捕获比较中断相互独立
 */
#include "atim.h"

/* 输出比较模式句柄 */
static TIM_HandleTypeDef s_compPwmHandle;

/* 互补 PWM 句柄与死区配置 */
static TIM_HandleTypeDef s_cplmPwmHandle;
static TIM_BreakDeadTimeConfigTypeDef s_breakDeadTimeConfig = {0};

/* PWM 输入测量状态
 * s_pwminSta : 0 未成功测量；1 测量成功 */
static uint8_t  s_pwminSta  = 0;
static uint16_t s_pwminPsc  = 0;    /* PWM 输入预分频系数 */
static uint32_t s_pwminHighVal = 0; /* PWM 高电平时间 */
static uint32_t s_pwminCycleVal = 0;/* PWM 周期 */

static TIM_HandleTypeDef s_pwminHandle;

/* ==================== 输出比较模式 4 路 PWM ==================== */

/**
 * @brief   高级定时器输出比较模式 4 路 PWM 初始化
 * @param   arr : 自动重装值
 * @param   psc : 时钟预分频数
 * @note    输出比较翻转模式，4 路 50% 占空比，比较值 250/500/750/1000
 *          实现每路 25% 相位差；修改 CCRx 可调相位
 */
void atim_timx_comp_pwm_init(uint16_t arr, uint16_t psc)
{
    TIM_OC_InitTypeDef ocConfig = {0};
    GPIO_InitTypeDef gpioInitStruct = {0};

    ATIM_TIMX_COMP_CLK_ENABLE();
    ATIM_TIMX_COMP_CH1_GPIO_CLK_ENABLE();
    ATIM_TIMX_COMP_CH2_GPIO_CLK_ENABLE();
    ATIM_TIMX_COMP_CH3_GPIO_CLK_ENABLE();
    ATIM_TIMX_COMP_CH4_GPIO_CLK_ENABLE();

    /* TIM8 CH1~CH4 默认映射 PC6/PC7/PC8/PC9，无需重映射 */
    gpioInitStruct.Mode = GPIO_MODE_AF_PP;
    gpioInitStruct.Pull = GPIO_NOPULL;
    gpioInitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    gpioInitStruct.Pin = ATIM_TIMX_COMP_CH1_GPIO_PIN;
    HAL_GPIO_Init(ATIM_TIMX_COMP_CH1_GPIO_PORT, &gpioInitStruct);

    gpioInitStruct.Pin = ATIM_TIMX_COMP_CH2_GPIO_PIN;
    HAL_GPIO_Init(ATIM_TIMX_COMP_CH2_GPIO_PORT, &gpioInitStruct);

    gpioInitStruct.Pin = ATIM_TIMX_COMP_CH3_GPIO_PIN;
    HAL_GPIO_Init(ATIM_TIMX_COMP_CH3_GPIO_PORT, &gpioInitStruct);

    gpioInitStruct.Pin = ATIM_TIMX_COMP_CH4_GPIO_PIN;
    HAL_GPIO_Init(ATIM_TIMX_COMP_CH4_GPIO_PORT, &gpioInitStruct);

    s_compPwmHandle.Instance = ATIM_TIMX_COMP;
    s_compPwmHandle.Init.Prescaler = psc;
    s_compPwmHandle.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_compPwmHandle.Init.Period = arr;
    s_compPwmHandle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_compPwmHandle.Init.RepetitionCounter = 0;
    s_compPwmHandle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_OC_Init(&s_compPwmHandle);

    ocConfig.OCMode = TIM_OCMODE_TOGGLE;
    ocConfig.OCPolarity = TIM_OCPOLARITY_HIGH;

    ocConfig.Pulse = 250;
    HAL_TIM_OC_ConfigChannel(&s_compPwmHandle, &ocConfig, TIM_CHANNEL_1);
    __HAL_TIM_ENABLE_OCxPRELOAD(&s_compPwmHandle, TIM_CHANNEL_1);

    ocConfig.Pulse = 500;
    HAL_TIM_OC_ConfigChannel(&s_compPwmHandle, &ocConfig, TIM_CHANNEL_2);
    __HAL_TIM_ENABLE_OCxPRELOAD(&s_compPwmHandle, TIM_CHANNEL_2);

    ocConfig.Pulse = 750;
    HAL_TIM_OC_ConfigChannel(&s_compPwmHandle, &ocConfig, TIM_CHANNEL_3);
    __HAL_TIM_ENABLE_OCxPRELOAD(&s_compPwmHandle, TIM_CHANNEL_3);

    ocConfig.Pulse = 1000;
    ocConfig.OCIdleState = TIM_OCIDLESTATE_RESET;
    HAL_TIM_OC_ConfigChannel(&s_compPwmHandle, &ocConfig, TIM_CHANNEL_4);
    __HAL_TIM_ENABLE_OCxPRELOAD(&s_compPwmHandle, TIM_CHANNEL_4);

    HAL_TIM_OC_Start(&s_compPwmHandle, TIM_CHANNEL_1);
    HAL_TIM_OC_Start(&s_compPwmHandle, TIM_CHANNEL_2);
    HAL_TIM_OC_Start(&s_compPwmHandle, TIM_CHANNEL_3);
    HAL_TIM_OC_Start(&s_compPwmHandle, TIM_CHANNEL_4);
}

/* ==================== 互补 PWM（含死区） ==================== */

/**
 * @brief   高级定时器互补 PWM 初始化（PWM 模式 1，含刹车输入）
 * @param   arr : 自动重装值
 * @param   psc : 时钟预分频数
 */
void atim_timx_cplm_pwm_init(uint16_t arr, uint16_t psc)
{
    TIM_OC_InitTypeDef ocConfig = {0};
    GPIO_InitTypeDef gpioInitStruct = {0};

    ATIM_TIMX_CPLM_CHY_GPIO_CLK_ENABLE();
    ATIM_TIMX_CPLM_CHYN_GPIO_CLK_ENABLE();
    ATIM_TIMX_CPLM_BKIN_GPIO_CLK_ENABLE();

    gpioInitStruct.Mode = GPIO_MODE_AF_PP;
    gpioInitStruct.Pull = GPIO_PULLUP;
    gpioInitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    gpioInitStruct.Pin = ATIM_TIMX_CPLM_CHY_GPIO_PIN;
    HAL_GPIO_Init(ATIM_TIMX_CPLM_CHY_GPIO_PORT, &gpioInitStruct);

    gpioInitStruct.Pin = ATIM_TIMX_CPLM_CHYN_GPIO_PIN;
    HAL_GPIO_Init(ATIM_TIMX_CPLM_CHYN_GPIO_PORT, &gpioInitStruct);

    gpioInitStruct.Pin = ATIM_TIMX_CPLM_BKIN_GPIO_PIN;
    HAL_GPIO_Init(ATIM_TIMX_CPLM_BKIN_GPIO_PORT, &gpioInitStruct);

    ATIM_TIMX_CPLM_CHYN_GPIO_REMAP();       /* 完全重映射到 PE8/PE9/PE15 */
    ATIM_TIMX_CPLM_CLK_ENABLE();

    s_cplmPwmHandle.Instance = ATIM_TIMX_CPLM;
    s_cplmPwmHandle.Init.Prescaler = psc;
    s_cplmPwmHandle.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_cplmPwmHandle.Init.Period = arr;
    s_cplmPwmHandle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_cplmPwmHandle.Init.RepetitionCounter = 0;
    s_cplmPwmHandle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&s_cplmPwmHandle);

    ocConfig.OCMode = TIM_OCMODE_PWM1;
    ocConfig.OCPolarity = TIM_OCPOLARITY_LOW;       /* OCy 低电平有效 */
    ocConfig.OCNPolarity = TIM_OCNPOLARITY_LOW;     /* OCyN 低电平有效 */
    HAL_TIM_PWM_ConfigChannel(&s_cplmPwmHandle, &ocConfig, ATIM_TIMX_CPLM_CHY);

    /* 刹车与死区配置：BKIN 高电平有效，AOE 自动恢复输出 */
    s_breakDeadTimeConfig.BreakState = TIM_BREAK_ENABLE;
    s_breakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;             /* 可修改配置 */
    s_breakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
    s_breakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_ENABLE;
    HAL_TIMEx_ConfigBreakDeadTime(&s_cplmPwmHandle, &s_breakDeadTimeConfig);

    HAL_TIM_PWM_Start(&s_cplmPwmHandle, ATIM_TIMX_CPLM_CHY);
    HAL_TIMEx_PWMN_Start(&s_cplmPwmHandle, ATIM_TIMX_CPLM_CHY);
}

/**
 * @brief   设置互补 PWM 比较值与死区时间
 * @param   ccr : 比较值（决定占空比）
 * @param   dtg : 死区时间
 *   @arg   dtg[7:5]=0xx  : 死区 = dtg[7:0] * tDTS
 *   @arg   dtg[7:5]=10x  : 死区 = (64 + dtg[6:0]) * 2 * tDTS
 *   @arg   dtg[7:5]=110  : 死区 = (32 + dtg[5:0]) * 8 * tDTS
 *   @arg   dtg[7:5]=111  : 死区 = (32 + dtg[5:0]) * 16 * tDTS
 */
void atim_timx_cplm_pwm_set(uint16_t ccr, uint8_t dtg)
{
    s_breakDeadTimeConfig.DeadTime = dtg;
    HAL_TIMEx_ConfigBreakDeadTime(&s_cplmPwmHandle, &s_breakDeadTimeConfig);

    __HAL_TIM_MOE_ENABLE(&s_cplmPwmHandle);     /* 使能主输出 */
    ATIM_TIMX_CPLM_CHY_CCRY = ccr;              /* 设置比较值 */
}

/* ==================== PWM 输入模式（测量周期/占空比） ==================== */

/**
 * @brief   高级定时器 PWM 输入模式初始化
 * @note    使用 psc=0、arr=65535，计数频率 72MHz（约 13.9ns 分辨率）；
 *          CH1 上升沿捕获周期，CH2 下降沿捕获高电平时间
 */
void atim_timx_pwmin_chy_init(void)
{
    TIM_SlaveConfigTypeDef slaveConfig = {0};
    TIM_IC_InitTypeDef icConfig = {0};
    GPIO_InitTypeDef gpioInitStruct = {0};

    ATIM_TIMX_PWMIN_CHY_GPIO_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();
    AFIO->EVCR = ATIM_TIMX_PWMIN_EVCR_VALUE;    /* TIM8_CH1 重映射到 PC6 */

    gpioInitStruct.Pin = ATIM_TIMX_PWMIN_CHY_GPIO_PIN;
    gpioInitStruct.Mode = GPIO_MODE_AF_PP;
    gpioInitStruct.Pull = GPIO_PULLDOWN;
    gpioInitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATIM_TIMX_PWMIN_CHY_GPIO_PORT, &gpioInitStruct);

    ATIM_TIMX_PWMIN_CHY_CLK_ENABLE();

    s_pwminHandle.Instance = ATIM_TIMX_PWMIN;
    s_pwminHandle.Init.Prescaler = 0;
    s_pwminHandle.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_pwminHandle.Init.Period = 65535;
    s_pwminHandle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_pwminHandle.Init.RepetitionCounter = 0;
    s_pwminHandle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_IC_Init(&s_pwminHandle);

    /* 从模式：复位模式，TI1FP1 上升沿复位计数器 */
    slaveConfig.SlaveMode = TIM_SLAVEMODE_RESET;
    slaveConfig.InputTrigger = TIM_TS_TI1FP1;
    slaveConfig.TriggerPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
    slaveConfig.TriggerFilter = 0;
    HAL_TIM_SlaveConfigSynchro(&s_pwminHandle, &slaveConfig);

    /* IC1：上升沿捕获（周期） */
    icConfig.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
    icConfig.ICSelection = TIM_ICSELECTION_DIRECTTI;
    icConfig.ICPrescaler = TIM_ICPSC_DIV1;
    icConfig.ICFilter = 0;
    HAL_TIM_IC_ConfigChannel(&s_pwminHandle, &icConfig, TIM_CHANNEL_1);

    /* IC2：下降沿捕获（高电平时间），映射到 TI1FP2 */
    icConfig.ICPolarity = TIM_INPUTCHANNELPOLARITY_FALLING;
    icConfig.ICSelection = TIM_ICSELECTION_INDIRECTTI;
    HAL_TIM_IC_ConfigChannel(&s_pwminHandle, &icConfig, TIM_CHANNEL_2);

    HAL_NVIC_SetPriority(ATIM_TIMX_PWMIN_IRQn, 1, 3);
    HAL_NVIC_EnableIRQ(ATIM_TIMX_PWMIN_IRQn);

    /* TIM1/TIM8 的捕获比较中断与更新中断分开 */
    if (ATIM_TIMX_PWMIN == TIM1 || ATIM_TIMX_PWMIN == TIM8)
    {
        HAL_NVIC_SetPriority(ATIM_TIMX_PWMIN_CC_IRQn, 1, 3);
        HAL_NVIC_EnableIRQ(ATIM_TIMX_PWMIN_CC_IRQn);
    }

    HAL_TIM_IC_Start_IT(&s_pwminHandle, TIM_CHANNEL_1);
    HAL_TIM_IC_Start_IT(&s_pwminHandle, TIM_CHANNEL_2);
}

/**
 * @brief   获取 PWM 输入测量状态
 * @retval  0 未成功测量；1 测量成功
 */
uint8_t atim_timx_pwmin_chy_get_sta(void)
{
    return s_pwminSta;
}

/**
 * @brief   获取 PWM 输入测量结果：高电平时间
 */
uint32_t atim_timx_pwmin_chy_get_hval(void)
{
    return s_pwminHighVal;
}

/**
 * @brief   获取 PWM 输入测量结果：周期
 */
uint32_t atim_timx_pwmin_chy_get_cval(void)
{
    return s_pwminCycleVal;
}

/**
 * @brief   PWM 输入更新中断服务函数
 * @note    TIM1/TIM8 的更新与捕获比较中断分开，此处只处理更新中断
 */
void ATIM_TIMX_PWMIN_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&s_pwminHandle);
}

/**
 * @brief   PWM 输入捕获比较中断服务函数
 */
void ATIM_TIMX_PWMIN_CC_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&s_pwminHandle);
}

/**
 * @brief   重置 PWM 输入测量状态，重新开始测量
 */
void atim_timx_pwmin_chy_restart(void)
{
    __disable_irq();    /* 关闭中断，保护测量变量 */

    s_pwminSta = 0;
    s_pwminHighVal = 0;
    s_pwminCycleVal = 0;

    __enable_irq();
}

/**
 * @brief   定时器输入捕获中断回调（PWM 输入测量结果提取）
 * @param   htim : 定时器句柄指针
 * @note    由 HAL_TIM_IRQHandler 调用；+2 为误差补偿系数
 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (s_pwminSta == 0)                /* 未成功测量 */
    {
        if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
        {
            s_pwminHighVal = HAL_TIM_ReadCapturedValue(&s_pwminHandle, TIM_CHANNEL_2) + 2;
            s_pwminCycleVal = HAL_TIM_ReadCapturedValue(&s_pwminHandle, TIM_CHANNEL_1) + 2;
            s_pwminSta = 1;             /* 标记测量成功 */
        }
    }
}
