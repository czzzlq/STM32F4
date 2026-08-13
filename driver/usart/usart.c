/**
  ******************************************************************************
  * @file        usart.c
  * @brief       串口1驱动实现（printf重定向、中断接收、半主机禁用）
  * @author      czzzlq
  * @date        2025-12-15
  * @version     V1.0
  * @attention   1. 禁用半主机模式，printf直接输出到串口1
  *              2. 中断接收支持回车(\r)+换行(\n)结束符判断
  ******************************************************************************
  */
#include "usart.h"
#include <string.h>

static UART_HandleTypeDef g_usart1Handle;  //串口1HAL句柄
static uint8_t g_usart1RxBuffer[USART1_MAX_RECV_LEN] = {0};    //接收缓冲
static uint16_t g_usart1RxStatus = 0;  //接收状态(位域设计：bit15:完成，bit14:收到CR,bit13~0:接收到字节的长度)
static uint8_t g_usart1RxTmpBuf[USART1_RX_BUF_SIZE] = {0}; //中断接收临时缓冲区

/* ************************** 半主机模式禁用（核心） ************************** */
#if 1
/* 兼容AC5/AC6编译器，彻底禁用半主机模式 */
#if (__ARMCC_VERSION >= 6010050) //AC6编译器

__asm(".global __use_no_semihosting\n\t");  //声明不使用半主机模式
__asm(".global __ARM_use_no_argv \n\t");   //声明main无参数，避免半主机残留

#else   //AC5编译器

#pragma import(__use_no_semihosting) //禁用半主机模式

//适配C库FILE结构体(printf依赖)
struct __FILE{
    int handle; //仅占位，无需实现文件操作逻辑
};

#endif

//半主机模式禁用必须得函数重定义
//重定义_ttywrch(int ch)
int _ttywrch(int ch){
	(void)ch;	//未使用参数，消除编译警告
	return ch;
}

//重定义_sys_exit，避免程序退出时触发半主机异常
void _sys_exit(int exitCode){
	(void)exitCode;
	while(1);	//死循环防止程序跑飞
}

//重定义_sys_command_string，禁用半主机命令行交互
char *_sys_command_string(char *cmd, int len){
	(void)cmd;
	(void)len;
	return NULL;
}

//定义标准输出（printf默认调用）
FILE __stdout;

//重定向fputc：printf函数最终调用此函数输出字符到串口1
int fputc(int ch, FILE *f){
	(void)f;	//未使用参数，消除编译警告

	//等待串口发送寄存器为空（USART_SR寄存器bit7：TXE）
	while(((USART1_CFG_PERIPH->SR & (1 << 7)) == 0));

	//写入字符到数据寄存器
	USART1_CFG_PERIPH->DR = (uint8_t)ch;

	return ch;
}

#endif 

/* ************************** 串口核心驱动 ************************** */
#if USART1_RX_ENABLE
/**
 * @brief 串口接收回调函数（中断模式） 
 * @param huart:串口句柄
 * @note 接收数据的核心处理逻辑，由HAL库中断服务函数触发
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart){
	if(huart->Instance == USART1_CFG_PERIPH){
		//数据接收未完成时处理数据
		if((g_usart1RxStatus & USART1_RX_STA_COMPLETE) == 0){
			uint16_t recvLen = g_usart1RxStatus & USART1_RX_STA_LEN_MASK;
			//已收到回车符(\r)，判断是否是换行符(\n)
			if(g_usart1RxStatus & USART1_RX_STA_HAS_CR){
				if(g_usart1RxTmpBuf[0] != '\n'){ //判断中断临时接收
					g_usart1RxStatus = 0;	//接收异常，重置状态
					memset(g_usart1RxBuffer,0,USART1_MAX_RECV_LEN);
				}else{
					g_usart1RxStatus |= USART1_RX_STA_COMPLETE;
					if(recvLen < USART1_MAX_RECV_LEN - 1){
                        g_usart1RxBuffer[recvLen] = '\0';
                    } else {
                        g_usart1RxBuffer[USART1_MAX_RECV_LEN - 1] = '\0';
                    }
				}
			}else{	//未收到回车符，继续接收数据
				if(g_usart1RxTmpBuf[0] == '\r'){
					g_usart1RxStatus |= USART1_RX_STA_HAS_CR;	//标记收到回车符
				}else{
					if(recvLen < USART1_MAX_RECV_LEN - 1){	//预留出结束符的位置(\r\n)
						g_usart1RxBuffer[recvLen] = g_usart1RxTmpBuf[0];
						g_usart1RxStatus++;
					}else{
						g_usart1RxStatus = 0;	//接收溢出，状态重置
						memset(g_usart1RxBuffer,0,USART1_MAX_RECV_LEN);
					}	

				}
			}
		}
		//重新开启中断接收（必须重新调用，否则只接收一次）
		HAL_StatusTypeDef ret = HAL_UART_Receive_IT(&g_usart1Handle,g_usart1RxTmpBuf,USART1_RX_BUF_SIZE);
		if(ret != HAL_OK){
			//接收开启失败处理
			g_usart1RxStatus = 0;
			memset(g_usart1RxBuffer, 0, USART1_MAX_RECV_LEN);
		}
	}
}

/**
 * @brief 串口1中断服务函数
 * @note 转发到HAL库通用中断处理函数，保证鲁棒性
 */
void USART1_IRQHandler(void){
	//HAL库中断核心处理
	HAL_UART_IRQHandler(&g_usart1Handle);
}

uint8_t *usart1GetRxBuffer(void){
    return g_usart1RxBuffer;
}

uint16_t usart1GetRxStatus(void){
    return g_usart1RxStatus;
}

void usart1ClearRxStatus(void){
    g_usart1RxStatus = 0;
    memset(g_usart1RxBuffer, 0, USART1_MAX_RECV_LEN);
}

#endif

/**
 * @brief 串口1初始化函数
 * @param baudRate:波特率
 * @retval 无
 */
void usart1Init(uint32_t baudRate){
	//初始化串口1句柄
	g_usart1Handle.Instance = USART1_CFG_PERIPH;	//USART寄存器基地址
	g_usart1Handle.Init.BaudRate = baudRate;		//波特率
	g_usart1Handle.Init.WordLength = UART_WORDLENGTH_8B;	//一帧数据8位
	g_usart1Handle.Init.StopBits = UART_STOPBITS_1;	//一位停止位
	g_usart1Handle.Init.Parity = UART_PARITY_NONE;	//无校验位
	g_usart1Handle.Init.Mode = UART_MODE_TX_RX;	//接收和发送模式都开启
	g_usart1Handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;	//不使用硬件流控制
	
	if(HAL_UART_Init(&g_usart1Handle) != HAL_OK){
		while(1);
	}

#if USART1_RX_ENABLE
	//开启串口中断接收（首次触发）
	if(HAL_UART_Receive_IT(&g_usart1Handle,g_usart1RxTmpBuf,USART1_RX_BUF_SIZE) != HAL_OK){
		while(1);
	}
#endif
}

/**
 * @brief 串口1 MSP底层初始化
 * @param huart:串口句柄
 * @note 函数由HAL_UART_Init()自动调用
 */
void HAL_UART_MspInit(UART_HandleTypeDef *huart){
	if(huart->Instance == USART1_CFG_PERIPH){	//判断UART寄存器基地址
		GPIO_InitTypeDef GPIO_InitStructure = {0};

		//时钟使能
		USART1_RX_GPIO_CLK_ENABLE();
		USART1_TX_GPIO_CLK_ENABLE();
		USART1_CFG_CLK_ENABLE();

		//配置TX引脚
		GPIO_InitStructure.Pin = USART1_TX_GPIO_PIN;
		GPIO_InitStructure.Pull = GPIO_PULLUP;
		GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
		GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;
		HAL_GPIO_Init(USART1_TX_GPIO_PORT,&GPIO_InitStructure);

		//配置RX引脚
		GPIO_InitStructure.Pin = USART1_RX_GPIO_PIN;
		GPIO_InitStructure.Pull = GPIO_NOPULL;
		GPIO_InitStructure.Mode = GPIO_MODE_AF_INPUT;
		HAL_GPIO_Init(USART1_RX_GPIO_PORT,&GPIO_InitStructure);

#if USART1_RX_ENABLE
		//配置中断
		HAL_NVIC_EnableIRQ(USART1_CFG_IRQn);	//使能中断通道
		HAL_NVIC_SetPriority(USART1_CFG_IRQn,3,3);	//抢占优先级3，响应优先级3
#endif
	}
}


/**
 * @brief 串口1发送数据函数
 * @param pData:发送数据的指针；
 * @param len:发送数据的长度
 * @retval 无
 */
void usart1SendBytes(uint8_t *pData, uint16_t len){
    if(pData == NULL || len == 0) return;
    for(uint16_t i=0; i<len; i++){
        while(((USART1_CFG_PERIPH->SR) & (1 << 7)) == 0); //等待TXE
        USART1_CFG_PERIPH->DR = pData[i];
    }
    while(((USART1_CFG_PERIPH->SR) & (1 << 6)) == 0); //等待发送完成（TC）
}
