/**
 * @file    atim.c
 * @brief   高级定时器驱动（输出比较 PWM / 互补 PWM / PWM 输入测量）
 * @note    1. 硬件配置见 bsp/bsp_timer.h
 *          2. TIM1/TIM8 的更新中断与捕获比较中断相互独立
 */
#include "atim.h"

/* 输出比较模式句柄 */
static TIM_HandleTypeDef g_atimx_comp_pwm_handle;

/* 互补 PWM 句柄与死区配置 */
static TIM_HandleTypeDef g_atimx_cplm_pwm_handle;
static TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

/* PWM 输入测量状态
 * g_atimxchy_pwmin_sta : 0 未成功测量；1 测量成功 */
uint8_t  g_atimxchy_pwmin_sta  = 0;
uint16_t g_atimxchy_pwmin_psc  = 0;     /* PWM 输入预分频系数 */
uint32_t g_atimxchy_pwmin_hval = 0;     /* PWM 高电平时间 */
uint32_t g_atimxchy_pwmin_cval = 0;     /* PWM 周期 */

static TIM_HandleTypeDef g_timx_pwmin_chy_handle;

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
    TIM_OC_InitTypeDef sConfigOC = {0};
    GPIO_InitTypeDef gpio_init_struct = {0};

    ATIM_TIMX_COMP_CLK_ENABLE();
    ATIM_TIMX_COMP_CH1_GPIO_CLK_ENABLE();
    ATIM_TIMX_COMP_CH2_GPIO_CLK_ENABLE();
    ATIM_TIMX_COMP_CH3_GPIO_CLK_ENABLE();
    ATIM_TIMX_COMP_CH4_GPIO_CLK_ENABLE();

    /* TIM8 CH1~CH4 默认映射 PC6/PC7/PC8/PC9，无需重映射 */
    gpio_init_struct.Mode = GPIO_MODE_AF_PP;
    gpio_init_struct.Pull = GPIO_NOPULL;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;

    gpio_init_struct.Pin = ATIM_TIMX_COMP_CH1_GPIO_PIN;
    HAL_GPIO_Init(ATIM_TIMX_COMP_CH1_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = ATIM_TIMX_COMP_CH2_GPIO_PIN;
    HAL_GPIO_Init(ATIM_TIMX_COMP_CH2_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = ATIM_TIMX_COMP_CH3_GPIO_PIN;
    HAL_GPIO_Init(ATIM_TIMX_COMP_CH3_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = ATIM_TIMX_COMP_CH4_GPIO_PIN;
    HAL_GPIO_Init(ATIM_TIMX_COMP_CH4_GPIO_PORT, &gpio_init_struct);

    g_atimx_comp_pwm_handle.Instance = ATIM_TIMX_COMP;
    g_atimx_comp_pwm_handle.Init.Prescaler = psc;
    g_atimx_comp_pwm_handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_atimx_comp_pwm_handle.Init.Period = arr;
    g_atimx_comp_pwm_handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    g_atimx_comp_pwm_handle.Init.RepetitionCounter = 0;
    g_atimx_comp_pwm_handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_OC_Init(&g_atimx_comp_pwm_handle);

    sConfigOC.OCMode = TIM_OCMODE_TOGGLE;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;

    sConfigOC.Pulse = 250;
    HAL_TIM_OC_ConfigChannel(&g_atimx_comp_pwm_handle, &sConfigOC, TIM_CHANNEL_1);
    __HAL_TIM_ENABLE_OCxPRELOAD(&g_atimx_comp_pwm_handle, TIM_CHANNEL_1);

    sConfigOC.Pulse = 500;
    HAL_TIM_OC_ConfigChannel(&g_atimx_comp_pwm_handle, &sConfigOC, TIM_CHANNEL_2);
    __HAL_TIM_ENABLE_OCxPRELOAD(&g_atimx_comp_pwm_handle, TIM_CHANNEL_2);

    sConfigOC.Pulse = 750;
    HAL_TIM_OC_ConfigChannel(&g_atimx_comp_pwm_handle, &sConfigOC, TIM_CHANNEL_3);
    __HAL_TIM_ENABLE_OCxPRELOAD(&g_atimx_comp_pwm_handle, TIM_CHANNEL_3);

    sConfigOC.Pulse = 1000;
    sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
    HAL_TIM_OC_ConfigChannel(&g_atimx_comp_pwm_handle, &sConfigOC, TIM_CHANNEL_4);
    __HAL_TIM_ENABLE_OCxPRELOAD(&g_atimx_comp_pwm_handle, TIM_CHANNEL_4);

    HAL_TIM_OC_Start(&g_atimx_comp_pwm_handle, TIM_CHANNEL_1);
    HAL_TIM_OC_Start(&g_atimx_comp_pwm_handle, TIM_CHANNEL_2);
    HAL_TIM_OC_Start(&g_atimx_comp_pwm_handle, TIM_CHANNEL_3);
    HAL_TIM_OC_Start(&g_atimx_comp_pwm_handle, TIM_CHANNEL_4);
}

/* ==================== 互补 PWM（含死区） ==================== */

/**
 * @brief   高级定时器互补 PWM 初始化（PWM 模式 1，含刹车输入）
 * @param   arr : 自动重装值
 * @param   psc : 时钟预分频数
 */
void atim_timx_cplm_pwm_init(uint16_t arr, uint16_t psc)
{
    TIM_OC_InitTypeDef sConfigOC = {0};
    GPIO_InitTypeDef gpio_init_struct = {0};

    ATIM_TIMX_CPLM_CHY_GPIO_CLK_ENABLE();
    ATIM_TIMX_CPLM_CHYN_GPIO_CLK_ENABLE();
    ATIM_TIMX_CPLM_BKIN_GPIO_CLK_ENABLE();

    gpio_init_struct.Mode = GPIO_MODE_AF_PP;
    gpio_init_struct.Pull = GPIO_PULLUP;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;

    gpio_init_struct.Pin = ATIM_TIMX_CPLM_CHY_GPIO_PIN;
    HAL_GPIO_Init(ATIM_TIMX_CPLM_CHY_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = ATIM_TIMX_CPLM_CHYN_GPIO_PIN;
    HAL_GPIO_Init(ATIM_TIMX_CPLM_CHYN_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = ATIM_TIMX_CPLM_BKIN_GPIO_PIN;
    HAL_GPIO_Init(ATIM_TIMX_CPLM_BKIN_GPIO_PORT, &gpio_init_struct);

    ATIM_TIMX_CPLM_CHYN_GPIO_REMAP();       /* 完全重映射到 PE8/PE9/PE15 */
    ATIM_TIMX_CPLM_CLK_ENABLE();

    g_atimx_cplm_pwm_handle.Instance = ATIM_TIMX_CPLM;
    g_atimx_cplm_pwm_handle.Init.Prescaler = psc;
    g_atimx_cplm_pwm_handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_atimx_cplm_pwm_handle.Init.Period = arr;
    g_atimx_cplm_pwm_handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    g_atimx_cplm_pwm_handle.Init.RepetitionCounter = 0;
    g_atimx_cplm_pwm_handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&g_atimx_cplm_pwm_handle);

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_LOW;      /* OCy 低电平有效 */
    sConfigOC.OCNPolarity = TIM_OCNPOLARITY_LOW;    /* OCyN 低电平有效 */
    HAL_TIM_PWM_ConfigChannel(&g_atimx_cplm_pwm_handle, &sConfigOC, ATIM_TIMX_CPLM_CHY);

    /* 刹车与死区配置：BKIN 高电平有效，AOE 自动恢复输出 */
    sBreakDeadTimeConfig.BreakState = TIM_BREAK_ENABLE;
    sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;             /* 可修改配置 */
    sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
    sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_ENABLE;
    HAL_TIMEx_ConfigBreakDeadTime(&g_atimx_cplm_pwm_handle, &sBreakDeadTimeConfig);

    HAL_TIM_PWM_Start(&g_atimx_cplm_pwm_handle, ATIM_TIMX_CPLM_CHY);
    HAL_TIMEx_PWMN_Start(&g_atimx_cplm_pwm_handle, ATIM_TIMX_CPLM_CHY);
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
    sBreakDeadTimeConfig.DeadTime = dtg;
    HAL_TIMEx_ConfigBreakDeadTime(&g_atimx_cplm_pwm_handle, &sBreakDeadTimeConfig);

    __HAL_TIM_MOE_ENABLE(&g_atimx_cplm_pwm_handle);     /* 使能主输出 */
    ATIM_TIMX_CPLM_CHY_CCRY = ccr;                      /* 设置比较值 */
}

/* ==================== PWM 输入模式（测量周期/占空比） ==================== */

/**
 * @brief   高级定时器 PWM 输入模式初始化
 * @note    使用 psc=0、arr=65535，计数频率 72MHz（约 13.9ns 分辨率）；
 *          CH1 上升沿捕获周期，CH2 下降沿捕获高电平时间
 */
void atim_timx_pwmin_chy_init(void)
{
    TIM_SlaveConfigTypeDef sSlaveConfig = {0};
    TIM_IC_InitTypeDef sConfigIC = {0};
    GPIO_InitTypeDef gpio_init_struct = {0};

    ATIM_TIMX_PWMIN_CHY_GPIO_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();
    AFIO->EVCR = ATIM_TIMX_PWMIN_EVCR_VALUE;    /* TIM8_CH1 重映射到 PC6 */

    gpio_init_struct.Pin = ATIM_TIMX_PWMIN_CHY_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_AF_PP;
    gpio_init_struct.Pull = GPIO_PULLDOWN;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(ATIM_TIMX_PWMIN_CHY_GPIO_PORT, &gpio_init_struct);

    ATIM_TIMX_PWMIN_CHY_CLK_ENABLE();

    g_timx_pwmin_chy_handle.Instance = ATIM_TIMX_PWMIN;
    g_timx_pwmin_chy_handle.Init.Prescaler = 0;
    g_timx_pwmin_chy_handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_timx_pwmin_chy_handle.Init.Period = 65535;
    g_timx_pwmin_chy_handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    g_timx_pwmin_chy_handle.Init.RepetitionCounter = 0;
    g_timx_pwmin_chy_handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_IC_Init(&g_timx_pwmin_chy_handle);

    /* 从模式：复位模式，TI1FP1 上升沿复位计数器 */
    sSlaveConfig.SlaveMode = TIM_SLAVEMODE_RESET;
    sSlaveConfig.InputTrigger = TIM_TS_TI1FP1;
    sSlaveConfig.TriggerPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
    sSlaveConfig.TriggerFilter = 0;
    HAL_TIM_SlaveConfigSynchro(&g_timx_pwmin_chy_handle, &sSlaveConfig);

    /* IC1：上升沿捕获（周期） */
    sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
    sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
    sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
    sConfigIC.ICFilter = 0;
    HAL_TIM_IC_ConfigChannel(&g_timx_pwmin_chy_handle, &sConfigIC, TIM_CHANNEL_1);

    /* IC2：下降沿捕获（高电平时间），映射到 TI1FP2 */
    sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_FALLING;
    sConfigIC.ICSelection = TIM_ICSELECTION_INDIRECTTI;
    HAL_TIM_IC_ConfigChannel(&g_timx_pwmin_chy_handle, &sConfigIC, TIM_CHANNEL_2);

    HAL_NVIC_SetPriority(ATIM_TIMX_PWMIN_IRQn, 1, 3);
    HAL_NVIC_EnableIRQ(ATIM_TIMX_PWMIN_IRQn);

    /* TIM1/TIM8 的捕获比较中断与更新中断分开 */
    if (ATIM_TIMX_PWMIN == TIM1 || ATIM_TIMX_PWMIN == TIM8)
    {
        HAL_NVIC_SetPriority(ATIM_TIMX_PWMIN_CC_IRQn, 1, 3);
        HAL_NVIC_EnableIRQ(ATIM_TIMX_PWMIN_CC_IRQn);
    }

    HAL_TIM_IC_Start_IT(&g_timx_pwmin_chy_handle, TIM_CHANNEL_1);
    HAL_TIM_IC_Start_IT(&g_timx_pwmin_chy_handle, TIM_CHANNEL_2);
}

/**
 * @brief   PWM 输入更新中断服务函数
 * @note    TIM1/TIM8 的更新与捕获比较中断分开，此处只处理更新中断
 */
void ATIM_TIMX_PWMIN_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&g_timx_pwmin_chy_handle);
}

/**
 * @brief   PWM 输入捕获比较中断服务函数
 */
void ATIM_TIMX_PWMIN_CC_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&g_timx_pwmin_chy_handle);
}

/**
 * @brief   重置 PWM 输入测量状态，重新开始测量
 */
void atim_timx_pwmin_chy_restart(void)
{
    __disable_irq();    /* 关闭中断，保护测量变量 */

    g_atimxchy_pwmin_sta = 0;
    g_atimxchy_pwmin_hval = 0;
    g_atimxchy_pwmin_cval = 0;

    __enable_irq();
}

/**
 * @brief   定时器输入捕获中断回调（PWM 输入测量结果提取）
 * @param   htim : 定时器句柄指针
 * @note    由 HAL_TIM_IRQHandler 调用；+2 为误差补偿系数
 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (g_atimxchy_pwmin_sta == 0)              /* 未成功测量 */
    {
        if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
        {
            g_atimxchy_pwmin_hval = HAL_TIM_ReadCapturedValue(&g_timx_pwmin_chy_handle, TIM_CHANNEL_2) + 2;
            g_atimxchy_pwmin_cval = HAL_TIM_ReadCapturedValue(&g_timx_pwmin_chy_handle, TIM_CHANNEL_1) + 2;
            g_atimxchy_pwmin_sta = 1;           /* 标记测量成功 */
        }
    }
}
