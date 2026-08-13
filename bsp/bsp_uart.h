/**
 * @file    bsp_uart.h
 * @brief   USART1 板级硬件配置
 * @note    软硬件分离：驱动层不感知引脚，换板/换外设只需修改本文件
 */
#ifndef __BSP_UART_H__
#define __BSP_UART_H__

#include "stm32f1xx.h"

/* TX 引脚 */
#define USART1_TX_GPIO_PORT             GPIOA
#define USART1_TX_GPIO_PIN              GPIO_PIN_9
#define USART1_TX_GPIO_CLK_ENABLE()     do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)

/* RX 引脚 */
#define USART1_RX_GPIO_PORT             GPIOA
#define USART1_RX_GPIO_PIN              GPIO_PIN_10
#define USART1_RX_GPIO_CLK_ENABLE()     do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)

/* 外设实例 / 中断号 */
#define USART1_CFG_PERIPH               USART1
#define USART1_CFG_IRQn                 USART1_IRQn
#define USART1_CFG_IRQHandler           USART1_IRQHandler
#define USART1_CFG_CLK_ENABLE()         do{ __HAL_RCC_USART1_CLK_ENABLE(); }while(0)

/* DMA 通道（USART1: RX = DMA1_Channel5，TX = DMA1_Channel4） */
#define USART1_RX_DMA_CHANNEL           DMA1_Channel5
#define USART1_RX_DMA_CLK_ENABLE()      do{ __HAL_RCC_DMA1_CLK_ENABLE(); }while(0)
#define USART1_RX_DMA_IRQn              DMA1_Channel5_IRQn
#define USART1_RX_DMA_IRQHandler        DMA1_Channel5_IRQHandler

#define USART1_TX_DMA_CHANNEL           DMA1_Channel4
#define USART1_TX_DMA_CLK_ENABLE()      do{ __HAL_RCC_DMA1_CLK_ENABLE(); }while(0)
#define USART1_TX_DMA_IRQn              DMA1_Channel4_IRQn
#define USART1_TX_DMA_IRQHandler        DMA1_Channel4_IRQHandler

#endif /* __BSP_UART_H__ */
