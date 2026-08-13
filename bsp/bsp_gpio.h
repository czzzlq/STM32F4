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

#include "stm32f1xx.h"

/*======================= LED Configuration =======================*/
//LED0
#define BSP_LED0_PORT           GPIOB
#define BSP_LED0_PIN            GPIO_PIN_5
#define BSP_LED0_CLK_ENABLE()   do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)
#define BSP_LED0_ACTIVE_LEVEL   0   //LED0为低电平点亮

//LED1
#define BSP_LED1_PORT           GPIOE
#define BSP_LED1_PIN            GPIO_PIN_5
#define BSP_LED1_CLK_ENABLE()   do{ __HAL_RCC_GPIOE_CLK_ENABLE(); }while(0)
#define BSP_LED1_ACTIVE_LEVEL   0   //LED0为低电平点亮

/*======================= KEY Configuration =======================*/
//KEY0
#define BSP_KEY0_PORT           GPIOE
#define BSP_KEY0_PIN            GPIO_PIN_4
#define BSP_KEY0_CLK_ENABLE()   do{ __HAL_RCC_GPIOE_CLK_ENABLE(); }while(0)
#define BSP_KEY0_ACTIVE_LEVEL   0

//KEY1
#define BSP_KEY1_PORT           GPIOE
#define BSP_KEY1_PIN            GPIO_PIN_3
#define BSP_KEY1_CLK_ENABLE()   do{ __HAL_RCC_GPIOE_CLK_ENABLE(); }while(0)
#define BSP_KEY1_ACTIVE_LEVEL   0

//WKUP
#define BSP_WKUP_PORT           GPIOA
#define BSP_WKUP_PIN            GPIO_PIN_0
#define BSP_WKUP_CLK_ENABLE()   do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)
#define BSP_WKUP_ACTIVE_LEVEL   1

#endif /* __BSP_GPIO_H__ */
