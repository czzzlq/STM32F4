/**
 * @file    bsp_timer.h
 * @brief   定时器（TIM1/TIM2/TIM3/TIM5/TIM8）板级硬件配置
 * @note    软硬件分离：驱动层不感知定时器实例/引脚，换板/换外设只需修改本文件
 */
#ifndef __BSP_TIMER_H__
#define __BSP_TIMER_H__

#include "stm32f1xx.h"

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
/* TIM3_CH2 经部分重映射输出到 PB5 */
#define GTIM_TIMX_PWM_CHY_GPIO_PORT         GPIOB
#define GTIM_TIMX_PWM_CHY_GPIO_PIN          GPIO_PIN_5
#define GTIM_TIMX_PWM_CHY_GPIO_CLK_ENABLE() do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)

#define GTIM_TIMX_PWM_CHY_GPIO_REMAP()      do{ __HAL_RCC_AFIO_CLK_ENABLE(); \
                                                __HAL_AFIO_REMAP_TIM3_PARTIAL(); \
                                             }while(0)

#define GTIM_TIMX_PWM                       TIM3
#define GTIM_TIMX_PWM_CHY                   TIM_CHANNEL_2
#define GTIM_TIMX_PWM_CHY_CCRX              TIM3->CCR2
#define GTIM_TIMX_PWM_CHY_CLK_ENABLE()      do{ __HAL_RCC_TIM3_CLK_ENABLE(); }while(0)

/*======================= 通用定时器：输入捕获 =======================*/
/* 捕获中断 ISR 开关：GTIM 的 TIM5 捕获与 btim.c 的 TIM5 定时中断互斥，
 * 使用捕获功能时置 1 并禁用 btim 的 TIM5 */
#define GTIM_TIMX_CAP_IRQ_ENABLE            0

/* TIM5_CH1，默认 PA0（与 WKUP 复用） */
#define GTIM_TIMX_CAP_CHY_GPIO_PORT         GPIOA
#define GTIM_TIMX_CAP_CHY_GPIO_PIN          GPIO_PIN_0
#define GTIM_TIMX_CAP_CHY_GPIO_CLK_ENABLE() do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)

#define GTIM_TIMX_CAP                       TIM5
#define GTIM_TIMX_CAP_IRQn                  TIM5_IRQn
#define GTIM_TIMX_CAP_IRQHandler            TIM5_IRQHandler
#define GTIM_TIMX_CAP_CHY                   TIM_CHANNEL_1
#define GTIM_TIMX_CAP_CHY_CCRX              TIM5->CCR1
#define GTIM_TIMX_CAP_CHY_CLK_ENABLE()      do{ __HAL_RCC_TIM5_CLK_ENABLE(); }while(0)

/*======================= 通用定时器：外部计数 =======================*/
/* TIM2_CH1 外部时钟模式1，默认 PA0（与 WKUP 复用） */
#define GTIM_TIMX_CNT_CHY_GPIO_PORT         GPIOA
#define GTIM_TIMX_CNT_CHY_GPIO_PIN          GPIO_PIN_0
#define GTIM_TIMX_CNT_CHY_GPIO_CLK_ENABLE() do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)

#define GTIM_TIMX_CNT                       TIM2
#define GTIM_TIMX_CNT_IRQn                  TIM2_IRQn
#define GTIM_TIMX_CNT_IRQHandler            TIM2_IRQHandler
#define GTIM_TIMX_CNT_CHY                   1u                      /* 仅 CH1/CH2 支持外部计数 */
#define GTIM_TIMX_CNT_CHY_CLK_ENABLE()      do{ __HAL_RCC_TIM2_CLK_ENABLE(); }while(0)

/*======================= 高级定时器：N 个脉冲 PWM（atim.c） =======================*/
/* TIM8_CH1，默认 PC6 */
#define ATIM_TIMX_NPWM_CHY_GPIO_PORT        GPIOC
#define ATIM_TIMX_NPWM_CHY_GPIO_PIN         GPIO_PIN_6
#define ATIM_TIMX_NPWM_CHY_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)

#define ATIM_TIMX_NPWM                      TIM8
#define ATIM_TIMX_NPWM_IRQn                 TIM8_UP_IRQn
#define ATIM_TIMX_NPWM_IRQHandler           TIM8_UP_IRQHandler
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

#define ATIM_TIMX_COMP                      TIM8
#define ATIM_TIMX_COMP_CH1_CCRX             ATIM_TIMX_COMP->CCR1
#define ATIM_TIMX_COMP_CH2_CCRX             ATIM_TIMX_COMP->CCR2
#define ATIM_TIMX_COMP_CH3_CCRX             ATIM_TIMX_COMP->CCR3
#define ATIM_TIMX_COMP_CH4_CCRX             ATIM_TIMX_COMP->CCR4
#define ATIM_TIMX_COMP_CLK_ENABLE()         do{ __HAL_RCC_TIM8_CLK_ENABLE(); }while(0)

/*======================= 高级定时器：互补 PWM =======================*/
/* TIM1_CH1/CH1N/BKIN 经完全重映射到 PE9/PE8/PE15 */
#define ATIM_TIMX_CPLM_CHY_GPIO_PORT        GPIOE
#define ATIM_TIMX_CPLM_CHY_GPIO_PIN         GPIO_PIN_9
#define ATIM_TIMX_CPLM_CHY_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOE_CLK_ENABLE(); }while(0)

#define ATIM_TIMX_CPLM_CHYN_GPIO_PORT       GPIOE
#define ATIM_TIMX_CPLM_CHYN_GPIO_PIN        GPIO_PIN_8
#define ATIM_TIMX_CPLM_CHYN_GPIO_CLK_ENABLE()   do{ __HAL_RCC_GPIOE_CLK_ENABLE(); }while(0)

#define ATIM_TIMX_CPLM_BKIN_GPIO_PORT       GPIOE
#define ATIM_TIMX_CPLM_BKIN_GPIO_PIN        GPIO_PIN_15
#define ATIM_TIMX_CPLM_BKIN_GPIO_CLK_ENABLE()   do{ __HAL_RCC_GPIOE_CLK_ENABLE(); }while(0)

#define ATIM_TIMX_CPLM_CHYN_GPIO_REMAP()    do{ __HAL_RCC_AFIO_CLK_ENABLE(); \
                                                __HAL_AFIO_REMAP_TIM1_ENABLE(); \
                                             }while(0)

#define ATIM_TIMX_CPLM                      TIM1
#define ATIM_TIMX_CPLM_CHY                  TIM_CHANNEL_1
#define ATIM_TIMX_CPLM_CHY_CCRY             ATIM_TIMX_CPLM->CCR1
#define ATIM_TIMX_CPLM_CLK_ENABLE()         do{ __HAL_RCC_TIM1_CLK_ENABLE(); }while(0)

/*======================= 高级定时器：PWM 输入模式 =======================*/
/* TIM8_CH1 经 EVCR 重映射到 PC6 */
#define ATIM_TIMX_PWMIN_CHY_GPIO_PORT       GPIOC
#define ATIM_TIMX_PWMIN_CHY_GPIO_PIN        GPIO_PIN_6
#define ATIM_TIMX_PWMIN_CHY_GPIO_CLK_ENABLE()   do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)

/* EVCR 寄存器值：TIM8_CH1 重映射到 PC6（PORT=PC, PIN=PX6） */
#define ATIM_TIMX_PWMIN_EVCR_VALUE          ((uint32_t)(AFIO_EVCR_PORT_PC | AFIO_EVCR_PIN_PX6))

#define ATIM_TIMX_PWMIN                     TIM8
#define ATIM_TIMX_PWMIN_IRQn                TIM8_UP_IRQn
#define ATIM_TIMX_PWMIN_IRQHandler          TIM8_UP_IRQHandler
#define ATIM_TIMX_PWMIN_CHY                 TIM_CHANNEL_1
#define ATIM_TIMX_PWMIN_CHY_CLK_ENABLE()    do{ __HAL_RCC_TIM8_CLK_ENABLE(); }while(0)

/* TIM1/TIM8 的捕获比较中断与更新中断分开 */
#define ATIM_TIMX_PWMIN_CC_IRQn             TIM8_CC_IRQn
#define ATIM_TIMX_PWMIN_CC_IRQHandler       TIM8_CC_IRQHandler

#endif /* __BSP_TIMER_H__ */
