/**
 * @file    atim.h
 * @brief   高级定时器驱动接口（输出比较 PWM / 互补 PWM / PWM 输入测量）
 * @note    硬件配置见 bsp/bsp_timer.h
 */
#ifndef __ATIM_H__
#define __ATIM_H__

#include "bsp_timer.h"

/* 高级定时器：PWM 输出（通用接口，N 脉冲 PWM 未实现） */
void atim_timx_pwm_chy_init(uint16_t arr, uint16_t psc);
void atim_timx_npwm_chy_init(uint16_t arr, uint16_t psc);
void atim_timx_npwm_chy_set(uint32_t npwm);

/* 高级定时器：输出比较模式 4 路 PWM（50% 占空比，相位差 25%） */
void atim_timx_comp_pwm_init(uint16_t arr, uint16_t psc);

/* 高级定时器：互补 PWM（含死区） */
void atim_timx_cplm_pwm_init(uint16_t arr, uint16_t psc);
void atim_timx_cplm_pwm_set(uint16_t ccr, uint8_t dtg);

/* 高级定时器：PWM 输入模式（测量周期与占空比） */
void atim_timx_pwmin_chy_init(void);
void atim_timx_pwmin_chy_restart(void);

#endif /* __ATIM_H__ */
