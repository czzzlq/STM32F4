/**
  ******************************************************************************
  * @file       bsp_gpio.h
  * @author     czzzlq
  * @version    V0.1
  * @date       2025年12月25日
  * @brief      板级GPIO映射
  * @attention  仅供学习使用
  * @version    
  *             -V0.1
  * @note       NULL
  ******************************************************************************
**/
#ifndef __BSP_GPIO_H__
#define __BSP_GPIO_H__

#include "stm32f4xx.h"

/*======================= LED Configuration =======================*/
/* 本板（M144Z-M4）LED0/LED1 分别接 PF9/PF10（低电平点亮）
 * 注意：F1 板卡上的 PB5/PE5 映射在本板是 SPI Flash MOSI / DCMI_D6 */
//LED0
#define BSP_LED0_PORT           GPIOF
#define BSP_LED0_PIN            GPIO_PIN_9
#define BSP_LED0_CLK_ENABLE()   do{ __HAL_RCC_GPIOF_CLK_ENABLE(); }while(0)
#define BSP_LED0_ACTIVE_LEVEL   0   //LED0为低电平点亮

//LED1
#define BSP_LED1_PORT           GPIOF
#define BSP_LED1_PIN            GPIO_PIN_10
#define BSP_LED1_CLK_ENABLE()   do{ __HAL_RCC_GPIOF_CLK_ENABLE(); }while(0)
#define BSP_LED1_ACTIVE_LEVEL   0   //LED1为低电平点亮

/*======================= KEY Configuration =======================*/
/* KEY0（本板按键，接 PE4；若所用板卡的 PE4 另有用途，改这里即可） */
#define BSP_KEY0_PORT           GPIOE
#define BSP_KEY0_PIN            GPIO_PIN_4
#define BSP_KEY0_CLK_ENABLE()   do{ __HAL_RCC_GPIOE_CLK_ENABLE(); }while(0)
#define BSP_KEY0_ACTIVE_LEVEL   0

//KEY1（本板未焊按键；PE3 为独立引出脚，外接按键即可用，内部上拉=未按下）
#define BSP_KEY1_PORT           GPIOE
#define BSP_KEY1_PIN            GPIO_PIN_3
#define BSP_KEY1_CLK_ENABLE()   do{ __HAL_RCC_GPIOE_CLK_ENABLE(); }while(0)
#define BSP_KEY1_ACTIVE_LEVEL   0

//WKUP（本板 KEY_UP 按键）
#define BSP_WKUP_PORT           GPIOA
#define BSP_WKUP_PIN            GPIO_PIN_0
#define BSP_WKUP_CLK_ENABLE()   do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)
#define BSP_WKUP_ACTIVE_LEVEL   1

#endif /* __BSP_GPIO_H__ */
