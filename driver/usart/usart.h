/**
 * @file    usart.h
 * @brief   串口1驱动接口（printf 重定向 + 环形缓冲区 + DMA 收发）
 * @note    硬件配置见 bsp/bsp_uart.h；支持 AC5/AC6，已禁用半主机模式
 * @note    接收架构：DMA 循环模式写入环形缓冲，CPU 零拷贝；
 *          总线空闲（IDLE）时触发 usart1RxIdleHook 通知帧结束
 */
#ifndef __USART_H__
#define __USART_H__

#include <stdio.h>
#include "bsp_uart.h"
#include "common_types.h"

/* 串口驱动配置 */
#define USART1_RX_ENABLE                1U          /* 1 使能接收，0 关闭 */
#define USART1_RX_DMA_BUF_SIZE          256U        /* 接收 DMA 缓冲区大小（必须为 2 的幂） */

/* ==================== 环形缓冲区访问接口 ==================== */
/* 当前可读字节数（0 ~ USART1_RX_DMA_BUF_SIZE-1） */
uint16_t usart1RxAvailable(void);
/* 读 1 字节（非阻塞），retval 1 成功 / 0 无数据 */
uint8_t usart1ReadByte(uint8_t *pData);
/* 读多个字节（非阻塞），retval 实际读取字节数 */
uint16_t usart1ReadBytes(uint8_t *pBuf, uint16_t len);
/* 清空接收缓冲区（丢弃全部未读数据） */
void usart1FlushRx(void);
/* 等待一帧到达（总线空闲），阻塞直到收到帧或超时
 * timeoutMs: 超时毫秒数，0xFFFFFFFF 表示永久等待
 * retval: 当前可读字节数（即帧长度）；0 表示超时 */
uint16_t usart1WaitFrame(uint32_t timeoutMs);
/* 帧结束（总线空闲）回调，由中断触发，应用层可重写
 * 注意：声明不带 weak（弱属性只加在 usart.c 的定义上），
 * 否则应用层的覆盖定义会被 armcc 传播为弱符号，链接时报重定义
 * 驱动弱实现内部会释放帧信号量唤醒 usart1WaitFrame */
void usart1RxIdleHook(uint16_t len);

/* ==================== 初始化与发送 ==================== */
/* 串口1初始化（含环形 DMA 接收启动） */
void usart1Init(uint32_t baudRate);
/* 阻塞发送（DMA 搬运，函数返回时数据已全部发出） */
void usart1SendBytes(uint8_t *pData, uint16_t len);

#endif /* __USART_H__ */
