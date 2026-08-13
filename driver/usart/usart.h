/**
  ******************************************************************************
  * @file        usart.h
  * @brief       串口1初始化/收发驱动（支持printf重定向、中断接收）
  * @author      czzlq
  * @date        2025-12-15
  * @version     V1.0
  * @attention   适配STM32F103，兼容AC5/AC6编译器，禁用半主机模式
  ******************************************************************************
  */
#ifndef __USART_H__
#define __USART_H__

#include <stdio.h>
#include "stm32f1xx.h"

//串口1引脚配置
#define USART1_TX_GPIO_PORT             GPIOA
#define USART1_TX_GPIO_PIN              GPIO_PIN_9
#define USART1_TX_GPIO_CLK_ENABLE()     do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0);

#define USART1_RX_GPIO_PORT             GPIOA
#define USART1_RX_GPIO_PIN              GPIO_PIN_10
#define USART1_RX_GPIO_CLK_ENABLE()     do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0);

//串口1外设配置
#define USART1_CFG_PERIPH               USART1
#define USART1_CFG_IRQn                 USART1_IRQn
#define USART1_CFG_IRQHandler           USART1_IRQHandler
#define USART1_CFG_CLK_ENABLE()         do{ __HAL_RCC_USART1_CLK_ENABLE(); }while(0);

//串口接收配置
#define USART1_MAX_RECV_LEN             200U    //最大接收字节数
#define USART1_RX_ENABLE                1U      //1-使能接收，0-禁用接收
#define USART1_RX_BUF_SIZE              1U      //HAL库中断接收缓冲大小

//接收状态位定义
#define USART1_RX_STA_COMPLETE          0X8000U //接收完成标志(bit15)
#define USART1_RX_STA_HAS_CR            0X4000U //接收到回车符(\r,0x0d)(bit14)
#define USART1_RX_STA_LEN_MASK          0X3FFFU //接收字节数掩码(bit13~0)

// 对外接口声明
uint8_t *usart1GetRxBuffer(void);
uint16_t usart1GetRxStatus(void);
void usart1ClearRxStatus(void);
void usart1Init(uint32_t baudRate); //串口1初始化
void usart1SendBytes(uint8_t *pData, uint16_t len);

#endif /* __USART_H__ */