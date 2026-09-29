/**
 * @file    bsp_timer.h
 * @brief   定时器（TIM1/TIM2/TIM3/TIM5/TIM8）板级硬件配置
 * @note    软硬件分离：驱动层不感知定时器实例/引脚，换板/换外设只需修改本文件
 */
#ifndef __BSP_TIMER_H__
#define __BSP_TIMER_H__

#include "stm32f4xx.h"

/*======================= 基本定时器（btim.c） =======================*/
#define BTIM_TIM3_INT                       TIM3
#define BTIM_TIM3_INT_IRQn                  TIM3_IRQn
#define BTIM_TIM3_INT_IRQHandler            TIM3_IRQHandler
#define BTIM_TIM3_INT_CLK_ENABLE()          do{ __HAL_RCC_TIM3_CLK_ENABLE(); }while(0)

#define BTIM_TIM5_INT                       TIM5
#define BTIM_TIM5_INT_IRQn                  TIM5_IRQn
#define BTIM_TIM5_INT_IRQHandler            TIM5_IRQHandler
#define BTIM_TIM5_INT_CLK_ENABLE()          do{ __HAL_RCC_TIM5_CLK_ENABLE(); }while(0)

/*======================= 通用定时器：定时中断（gtim.c） =======================*/
/* 定时中断 ISR 开关：GTIM 的 TIM3 与 btim.c 的 TIM3 定时中断互斥，
 * 同时编译时本开关保持 0（gtim 的 PWM/捕获/计数功能不受影响） */
#define GTIM_TIMX_INT_IRQ_ENABLE            0

#define GTIM_TIMX_INT                       TIM3
#define GTIM_TIMX_INT_IRQn                  TIM3_IRQn
#define GTIM_TIMX_INT_IRQHandler            TIM3_IRQHandler
#define GTIM_TIMX_INT_CLK_ENABLE()          do{ __HAL_RCC_TIM3_CLK_ENABLE(); }while(0)

/*======================= 通用定时器：PWM 输出 =======================*/
/* TIM3_CH2 输出到 PB5（STM32F4 无 AFIO 重映射寄存器，由引脚复用功能 AF2 直接提供） */
#define GTIM_TIMX_PWM_CHY_GPIO_PORT         GPIOB
#define GTIM_TIMX_PWM_CHY_GPIO_PIN          GPIO_PIN_5
#define GTIM_TIMX_PWM_CHY_GPIO_CLK_ENABLE() do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)
#define GTIM_TIMX_PWM_CHY_GPIO_AF           GPIO_AF2_TIM3

#define GTIM_TIMX_PWM_CHY_GPIO_REMAP()      do{ }while(0)   /* F4 无需重映射配置 */

#define GTIM_TIMX_PWM                       TIM3
#define GTIM_TIMX_PWM_CHY                   TIM_CHANNEL_2
#define GTIM_TIMX_PWM_CHY_CCRX              TIM3->CCR2
#define GTIM_TIMX_PWM_CHY_CLK_ENABLE()      do{ __HAL_RCC_TIM3_CLK_ENABLE(); }while(0)

/*======================= 通用定时器：输入捕获 =======================*/
/* 捕获中断 ISR 开关：GTIM 的 TIM5 捕获与 btim.c 的 TIM5 定时中断互斥，
 * 使用捕获功能时置 1 并禁用 btim 的 TIM5 */
#define GTIM_TIMX_CAP_IRQ_ENABLE            0

/* TIM5_CH1，默认 PA0（与 WKUP 复用），复用功能 AF2 */
#define GTIM_TIMX_CAP_CHY_GPIO_PORT         GPIOA
#define GTIM_TIMX_CAP_CHY_GPIO_PIN          GPIO_PIN_0
#define GTIM_TIMX_CAP_CHY_GPIO_CLK_ENABLE() do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)
#define GTIM_TIMX_CAP_CHY_GPIO_AF           GPIO_AF2_TIM5

#define GTIM_TIMX_CAP                       TIM5
#define GTIM_TIMX_CAP_IRQn                  TIM5_IRQn
#define GTIM_TIMX_CAP_IRQHandler            TIM5_IRQHandler
#define GTIM_TIMX_CAP_CHY                   TIM_CHANNEL_1
#define GTIM_TIMX_CAP_CHY_CCRX              TIM5->CCR1
#define GTIM_TIMX_CAP_CHY_CLK_ENABLE()      do{ __HAL_RCC_TIM5_CLK_ENABLE(); }while(0)

/*======================= 通用定时器：外部计数 =======================*/
/* TIM2_CH1 外部时钟模式1，默认 PA0（与 WKUP 复用），复用功能 AF1 */
#define GTIM_TIMX_CNT_CHY_GPIO_PORT         GPIOA
#define GTIM_TIMX_CNT_CHY_GPIO_PIN          GPIO_PIN_0
#define GTIM_TIMX_CNT_CHY_GPIO_CLK_ENABLE() do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)
#define GTIM_TIMX_CNT_CHY_GPIO_AF           GPIO_AF1_TIM2

#define GTIM_TIMX_CNT                       TIM2
#define GTIM_TIMX_CNT_IRQn                  TIM2_IRQn
#define GTIM_TIMX_CNT_IRQHandler            TIM2_IRQHandler
#define GTIM_TIMX_CNT_CHY                   1u                      /* 仅 CH1/CH2 支持外部计数 */
#define GTIM_TIMX_CNT_CHY_CLK_ENABLE()      do{ __HAL_RCC_TIM2_CLK_ENABLE(); }while(0)

/*======================= 高级定时器：N 个脉冲 PWM（atim.c） =======================*/
/* TIM8_CH1 输出到 PC6（F4 无 EVCR/AFIO，PC6 由复用功能 AF3 直接提供） */
#define ATIM_TIMX_NPWM_CHY_GPIO_PORT        GPIOC
#define ATIM_TIMX_NPWM_CHY_GPIO_PIN         GPIO_PIN_6
#define ATIM_TIMX_NPWM_CHY_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)
#define ATIM_TIMX_NPWM_CHY_GPIO_AF          GPIO_AF3_TIM8

#define ATIM_TIMX_NPWM                      TIM8
/* F4 上 TIM8 更新中断与 TIM13 共用同一个向量（F1 是独立的 TIM8_UP） */
#define ATIM_TIMX_NPWM_IRQn                 TIM8_UP_TIM13_IRQn
#define ATIM_TIMX_NPWM_IRQHandler           TIM8_UP_TIM13_IRQHandler
#define ATIM_TIMX_NPWM_CHY                  TIM_CHANNEL_1
#define ATIM_TIMX_NPWM_CHY_CCRX             TIM8->CCR1
#define ATIM_TIMX_NPWM_CHY_CLK_ENABLE()     do{ __HAL_RCC_TIM8_CLK_ENABLE(); }while(0)

/*======================= 高级定时器：输出比较四路 PWM =======================*/
/* TIM8 CH1~CH4，默认 PC6/PC7/PC8/PC9 */
#define ATIM_TIMX_COMP_CH1_GPIO_PORT        GPIOC
#define ATIM_TIMX_COMP_CH1_GPIO_PIN         GPIO_PIN_6
#define ATIM_TIMX_COMP_CH1_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)

#define ATIM_TIMX_COMP_CH2_GPIO_PORT        GPIOC
#define ATIM_TIMX_COMP_CH2_GPIO_PIN         GPIO_PIN_7
#define ATIM_TIMX_COMP_CH2_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)

#define ATIM_TIMX_COMP_CH3_GPIO_PORT        GPIOC
#define ATIM_TIMX_COMP_CH3_GPIO_PIN         GPIO_PIN_8
#define ATIM_TIMX_COMP_CH3_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)

#define ATIM_TIMX_COMP_CH4_GPIO_PORT        GPIOC
#define ATIM_TIMX_COMP_CH4_GPIO_PIN         GPIO_PIN_9
#define ATIM_TIMX_COMP_CH4_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)

/* TIM8_CH1~CH4 = PC6~PC9，复用功能 AF3 */
#define ATIM_TIMX_COMP_GPIO_AF              GPIO_AF3_TIM8

#define ATIM_TIMX_COMP                      TIM8
#define ATIM_TIMX_COMP_CH1_CCRX             ATIM_TIMX_COMP->CCR1
#define ATIM_TIMX_COMP_CH2_CCRX             ATIM_TIMX_COMP->CCR2
#define ATIM_TIMX_COMP_CH3_CCRX             ATIM_TIMX_COMP->CCR3
#define ATIM_TIMX_COMP_CH4_CCRX             ATIM_TIMX_COMP->CCR4
#define ATIM_TIMX_COMP_CLK_ENABLE()         do{ __HAL_RCC_TIM8_CLK_ENABLE(); }while(0)

/*======================= 高级定时器：互补 PWM =======================*/
/* TIM1_CH1/CH1N/BKIN 输出到 PE9/PE8/PE15（F4 由复用功能 AF1 直接提供，无 AFIO 重映射） */
#define ATIM_TIMX_CPLM_CHY_GPIO_PORT        GPIOE
#define ATIM_TIMX_CPLM_CHY_GPIO_PIN         GPIO_PIN_9
#define ATIM_TIMX_CPLM_CHY_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOE_CLK_ENABLE(); }while(0)

#define ATIM_TIMX_CPLM_CHYN_GPIO_PORT       GPIOE
#define ATIM_TIMX_CPLM_CHYN_GPIO_PIN        GPIO_PIN_8
#define ATIM_TIMX_CPLM_CHYN_GPIO_CLK_ENABLE()   do{ __HAL_RCC_GPIOE_CLK_ENABLE(); }while(0)

#define ATIM_TIMX_CPLM_BKIN_GPIO_PORT       GPIOE
#define ATIM_TIMX_CPLM_BKIN_GPIO_PIN        GPIO_PIN_15
#define ATIM_TIMX_CPLM_BKIN_GPIO_CLK_ENABLE()   do{ __HAL_RCC_GPIOE_CLK_ENABLE(); }while(0)

#define ATIM_TIMX_CPLM_GPIO_AF              GPIO_AF1_TIM1
#define ATIM_TIMX_CPLM_CHYN_GPIO_REMAP()    do{ }while(0)   /* F4 无需重映射配置 */

#define ATIM_TIMX_CPLM                      TIM1
#define ATIM_TIMX_CPLM_CHY                  TIM_CHANNEL_1
#define ATIM_TIMX_CPLM_CHY_CCRY             ATIM_TIMX_CPLM->CCR1
#define ATIM_TIMX_CPLM_CLK_ENABLE()         do{ __HAL_RCC_TIM1_CLK_ENABLE(); }while(0)

/*======================= 高级定时器：PWM 输入模式 =======================*/
/* TIM8_CH1 输入，PC6（F4 由复用功能 AF3 直接提供，无 EVCR 寄存器） */
#define ATIM_TIMX_PWMIN_CHY_GPIO_PORT       GPIOC
#define ATIM_TIMX_PWMIN_CHY_GPIO_PIN        GPIO_PIN_6
#define ATIM_TIMX_PWMIN_CHY_GPIO_CLK_ENABLE()   do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)
#define ATIM_TIMX_PWMIN_CHY_GPIO_AF         GPIO_AF3_TIM8

#define ATIM_TIMX_PWMIN                     TIM8
/* TIM8 更新中断在 F4 上与 TIM13 共用向量，捕获比较中断独立 */
#define ATIM_TIMX_PWMIN_IRQn                TIM8_UP_TIM13_IRQn
#define ATIM_TIMX_PWMIN_IRQHandler          TIM8_UP_TIM13_IRQHandler
#define ATIM_TIMX_PWMIN_CHY                 TIM_CHANNEL_1
#define ATIM_TIMX_PWMIN_CHY_CLK_ENABLE()    do{ __HAL_RCC_TIM8_CLK_ENABLE(); }while(0)

/* TIM1/TIM8 的捕获比较中断与更新中断分开 */
#define ATIM_TIMX_PWMIN_CC_IRQn             TIM8_CC_IRQn
#define ATIM_TIMX_PWMIN_CC_IRQHandler       TIM8_CC_IRQHandler

#endif /* __BSP_TIMER_H__ */
