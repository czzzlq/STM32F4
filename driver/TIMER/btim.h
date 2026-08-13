#ifndef __BTIM_H
#define __BTIM_H

// #include "../../core/sys/sys.h"
#include "../../driver/usart/usart.h"

/* 基本定时器 定义 */

/* TIMX 中断定义 
 * 默认是针对TIM2~TIM5, TIM12~TIM17.
 * 注意: 通过修改这4个宏定义,可以支持TIM1~TIM17任意一个定时器.
 */
 
#define BTIM_TIM3_INT                       TIM3
#define BTIM_TIM3_INT_IRQn                  TIM3_IRQn
#define BTIM_TIM3_INT_IRQHandler            TIM3_IRQHandler
#define BTIM_TIM3_INT_CLK_ENABLE()          do{ __HAL_RCC_TIM3_CLK_ENABLE(); }while(0)  /* TIM3 时钟使能 */

#define BTIM_TIM5_INT                       TIM5
#define BTIM_TIM5_INT_IRQn                  TIM5_IRQn
#define BTIM_TIM5_INT_IRQHandler            TIM5_IRQHandler
#define BTIM_TIM5_INT_CLK_ENABLE()          do{ __HAL_RCC_TIM5_CLK_ENABLE(); }while(0)  /* TIM5 时钟使能 */

void btim_tim3_init(uint16_t arr, uint16_t psc);
void btim_tim5_init(uint16_t arr, uint16_t psc);

#endif
