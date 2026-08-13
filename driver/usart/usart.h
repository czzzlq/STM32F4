/**
 * @file    usart.h
 * @brief   串口1驱动接口（printf 重定向 + 中断接收）
 * @note    硬件配置见 bsp/bsp_uart.h；支持 AC5/AC6，已禁用半主机模式
 */
#ifndef __USART_H__
#define __USART_H__

#include <stdio.h>
#include "bsp_uart.h"

/* 串口驱动配置 */
#define USART1_MAX_RECV_LEN             200U    /* 接收缓冲区大小 */
#define USART1_RX_ENABLE                1U      /* 1 使能中断接收，0 关闭 */
#define USART1_RX_BUF_SIZE              1U      /* HAL 单次中断接收长度 */

/* 接收状态位域
 * bit15      : 接收完成标志
 * bit14      : 已收到回车符(\r)
 * bit13~0    : 已接收字节长度 */
#define USART1_RX_STA_COMPLETE          0X8000U
#define USART1_RX_STA_HAS_CR            0X4000U
#define USART1_RX_STA_LEN_MASK          0X3FFFU

/* 驱动接口 */
uint8_t *usart1GetRxBuffer(void);
uint16_t usart1GetRxStatus(void);
void usart1ClearRxStatus(void);
void usart1Init(uint32_t baudRate);
void usart1SendBytes(uint8_t *pData, uint16_t len);

#endif /* __USART_H__ */
