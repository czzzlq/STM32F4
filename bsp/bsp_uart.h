/**
 * @file    bsp_uart.h
 * @brief   USART1 板级硬件配置
 * @note    软硬件分离：驱动层不感知引脚，换板/换外设只需修改本文件
 */
#ifndef __BSP_UART_H__
#define __BSP_UART_H__

#include "stm32f4xx.h"

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

/* 引脚复用功能号（PA9/PA10 复用为 USART1，AF7） */
#define USART1_GPIO_AF                  GPIO_AF7_USART1

/* DMA（STM32F4 为“流 + 通道”结构，请求映射见参考手册 DMA request mapping：
 *   USART1_RX = DMA2_Stream2 / Channel4，USART1_TX = DMA2_Stream7 / Channel4） */
#define USART1_RX_DMA_STREAM            DMA2_Stream2
#define USART1_RX_DMA_CHANNEL           DMA_CHANNEL_4
#define USART1_RX_DMA_CLK_ENABLE()      do{ __HAL_RCC_DMA2_CLK_ENABLE(); }while(0)
#define USART1_RX_DMA_IRQn              DMA2_Stream2_IRQn
#define USART1_RX_DMA_IRQHandler        DMA2_Stream2_IRQHandler

#define USART1_TX_DMA_STREAM            DMA2_Stream7
#define USART1_TX_DMA_CHANNEL           DMA_CHANNEL_4
#define USART1_TX_DMA_CLK_ENABLE()      do{ __HAL_RCC_DMA2_CLK_ENABLE(); }while(0)
#define USART1_TX_DMA_IRQn              DMA2_Stream7_IRQn
#define USART1_TX_DMA_IRQHandler        DMA2_Stream7_IRQHandler

#endif /* __BSP_UART_H__ */
