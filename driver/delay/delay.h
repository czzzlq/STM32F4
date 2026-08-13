/**
 * @file    delay.h
 * @brief   SysTick 延时驱动接口
 */
#ifndef __DELAY_H__
#define __DELAY_H__

#include "system_hal.h"

void systickDelayInit(uint16_t systemclkMHz);
void systickDelay_us(uint32_t us);
void systickDelay_ms(uint16_t ms);

#if (!SYS_SUPPORT_OS)
void HAL_Delay(uint32_t ms);
#endif /* (!SYS_SUPPORT_OS) */

#endif /* __DELAY_H__ */
