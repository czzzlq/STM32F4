/**
 * @file    btim.h
 * @brief   基本定时器定时中断驱动接口
 * @note    硬件配置见 bsp/bsp_timer.h
 */
#ifndef __BTIM_H__
#define __BTIM_H__

#include "bsp_timer.h"

void btim_tim3_init(uint16_t arr, uint16_t psc);
void btim_tim5_init(uint16_t arr, uint16_t psc);

#endif /* __BTIM_H__ */
