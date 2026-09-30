/**
 * @file    gtim.h
 * @brief   通用定时器驱动接口（定时中断 / PWM 输出 / 输入捕获 / 外部计数）
 * @note    硬件配置见 bsp/bsp_timer.h
 */
#ifndef __GTIM_H__
#define __GTIM_H__

#include "bsp_timer.h"

/* 通用定时器：定时中断 */
void gtim_timx_int_init(uint16_t arr, uint16_t psc);
/* 通用定时器：PWM 输出 */
void gtim_timx_pwm_chy_init(uint16_t arr, uint16_t psc);
void gtim_timx_pwm_setcompare(uint32_t compare);
/* 通用定时器：输入捕获（高电平脉宽测量） */
void gtim_timx_cap_chy_init(uint16_t arr, uint16_t psc);
uint8_t gtim_timx_cap_chy_get_sta(void);
uint16_t gtim_timx_cap_chy_get_val(void);
/* 通用定时器：外部计数 */
void gtim_timx_cnt_chy_init(uint16_t psc);
uint32_t gtim_timx_cnt_chy_get_count(void);
void gtim_timx_cnt_chy_restart(void);

#endif /* __GTIM_H__ */
