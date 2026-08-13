/**
  ******************************************************************************
  * @file       delay.h
  * @author     czzzlq
  * @version    V0.1
  * @date       2025年12月14日
  * @brief      delay驱动
  * @attention  仅供学习使用
  * @note       仅适用于STM32F103ZET6
  * @version    
  *             -V0.1
  ******************************************************************************
**/
 
#ifndef __DELAY_H__
#define __DELAY_H__

#include "../system/system_hal.h"

void systickDelayInit(uint16_t systemclkMHz);
void systickDelay_us(uint32_t us);
void systickDelay_ms(uint16_t ms);

#if(!SYS_SUPPORT_OS)
void HAL_Delay(uint32_t ms);
#endif /* (!SYS_SUPPORT_OS) */

#endif /* __DELAY_H__ */
