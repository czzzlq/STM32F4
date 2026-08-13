/**
 * @file    usart.c
 * @brief   串口1驱动实现（printf 重定向、中断接收、半主机禁用）
 */
#include "usart.h"
#include <string.h>

static UART_HandleTypeDef g_usart1Handle;
static uint8_t g_usart1RxBuffer[USART1_MAX_RECV_LEN] = {0};              /* 接收缓冲 */
static uint16_t g_usart1RxStatus = 0;                                    /* 接收状态（位域见 usart.h） */
static uint8_t g_usart1RxTmpBuf[USART1_RX_BUF_SIZE] = {0};               /* 中断接收临时缓冲 */

/* ==================== 半主机模式禁用 ==================== */
#if (__ARMCC_VERSION >= 6010050)    /* AC6 */

__asm(".global __use_no_semihosting\n\t");
__asm(".global __ARM_use_no_argv \n\t");

#else                               /* AC5 */

#pragma import(__use_no_semihosting)

/* 适配 C 库 FILE 结构体（printf 依赖） */
struct __FILE
{
    int handle;
};

#endif

/* 半主机禁用所需的重定义函数 */
int _ttywrch(int ch)
{
    (void)ch;
    return ch;
}

void _sys_exit(int exitCode)
{
    (void)exitCode;
    while (1);
}

char *_sys_command_string(char *cmd, int len)
{
    (void)cmd;
    (void)len;
    return NULL;
}

FILE __stdout;      /* 标准输出（printf 默认使用） */

/**
 * @brief   fputc 重定向：printf 最终调用此函数，寄存器直发到串口1
 */
int fputc(int ch, FILE *f)
{
    (void)f;

    while ((USART1_CFG_PERIPH->SR & USART_SR_TXE) == 0);
    USART1_CFG_PERIPH->DR = (uint8_t)ch;

    return ch;
}

/* ==================== 串口核心驱动 ==================== */
#if USART1_RX_ENABLE
/**
 * @brief   串口接收回调（中断模式，由 HAL 库中断服务触发）
 * @note    单字节接收，在回调中解析 \r\n 结束符协议
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1_CFG_PERIPH)
    {
        return;
    }

    if ((g_usart1RxStatus & USART1_RX_STA_COMPLETE) == 0)    /* 数据接收未完成时处理 */
    {
        uint16_t recvLen = g_usart1RxStatus & USART1_RX_STA_LEN_MASK;

        if (g_usart1RxStatus & USART1_RX_STA_HAS_CR)         /* 已收到 \r，等待 \n 结束 */
        {
            if (g_usart1RxTmpBuf[0] != '\n')
            {
                g_usart1RxStatus = 0;                        /* 协议异常，重置状态 */
                memset(g_usart1RxBuffer, 0, USART1_MAX_RECV_LEN);
            }
            else
            {
                g_usart1RxStatus |= USART1_RX_STA_COMPLETE;
                if (recvLen < USART1_MAX_RECV_LEN - 1)
                {
                    g_usart1RxBuffer[recvLen] = '\0';
                }
                else
                {
                    g_usart1RxBuffer[USART1_MAX_RECV_LEN - 1] = '\0';
                }
            }
        }
        else                                                 /* 未收到 \r，继续接收数据 */
        {
            if (g_usart1RxTmpBuf[0] == '\r')
            {
                g_usart1RxStatus |= USART1_RX_STA_HAS_CR;
            }
            else if (recvLen < USART1_MAX_RECV_LEN - 1)      /* 预留 \r\n 结束符位置 */
            {
                g_usart1RxBuffer[recvLen] = g_usart1RxTmpBuf[0];
                g_usart1RxStatus++;
            }
            else
            {
                g_usart1RxStatus = 0;                        /* 接收溢出，重置状态 */
                memset(g_usart1RxBuffer, 0, USART1_MAX_RECV_LEN);
            }
        }
    }

    /* 必须重新启动接收，否则只接收一次 */
    if (HAL_UART_Receive_IT(&g_usart1Handle, g_usart1RxTmpBuf, USART1_RX_BUF_SIZE) != HAL_OK)
    {
        g_usart1RxStatus = 0;
        memset(g_usart1RxBuffer, 0, USART1_MAX_RECV_LEN);
    }
}

/**
 * @brief   串口1中断服务函数（转发到 HAL 通用中断处理）
 */
void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&g_usart1Handle);
}

uint8_t *usart1GetRxBuffer(void)
{
    return g_usart1RxBuffer;
}

uint16_t usart1GetRxStatus(void)
{
    return g_usart1RxStatus;
}

void usart1ClearRxStatus(void)
{
    g_usart1RxStatus = 0;
    memset(g_usart1RxBuffer, 0, USART1_MAX_RECV_LEN);
}

#endif /* USART1_RX_ENABLE */

/**
 * @brief   串口1初始化
 * @param   baudRate : 波特率
 */
void usart1Init(uint32_t baudRate)
{
    g_usart1Handle.Instance = USART1_CFG_PERIPH;
    g_usart1Handle.Init.BaudRate = baudRate;
    g_usart1Handle.Init.WordLength = UART_WORDLENGTH_8B;
    g_usart1Handle.Init.StopBits = UART_STOPBITS_1;
    g_usart1Handle.Init.Parity = UART_PARITY_NONE;
    g_usart1Handle.Init.Mode = UART_MODE_TX_RX;
    g_usart1Handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;

    if (HAL_UART_Init(&g_usart1Handle) != HAL_OK)
    {
        while (1);
    }

#if USART1_RX_ENABLE
    /* 启动中断接收（首次触发） */
    if (HAL_UART_Receive_IT(&g_usart1Handle, g_usart1RxTmpBuf, USART1_RX_BUF_SIZE) != HAL_OK)
    {
        while (1);
    }
#endif
}

/**
 * @brief   串口1 MSP 底层初始化（由 HAL_UART_Init 自动调用）
 */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1_CFG_PERIPH)
    {
        return;
    }

    GPIO_InitTypeDef GPIO_InitStructure = {0};

    USART1_RX_GPIO_CLK_ENABLE();
    USART1_TX_GPIO_CLK_ENABLE();
    USART1_CFG_CLK_ENABLE();

    /* TX 引脚 */
    GPIO_InitStructure.Pin = USART1_TX_GPIO_PIN;
    GPIO_InitStructure.Pull = GPIO_PULLUP;
    GPIO_InitStructure.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(USART1_TX_GPIO_PORT, &GPIO_InitStructure);

    /* RX 引脚 */
    GPIO_InitStructure.Pin = USART1_RX_GPIO_PIN;
    GPIO_InitStructure.Pull = GPIO_NOPULL;
    GPIO_InitStructure.Mode = GPIO_MODE_AF_INPUT;
    HAL_GPIO_Init(USART1_RX_GPIO_PORT, &GPIO_InitStructure);

#if USART1_RX_ENABLE
    HAL_NVIC_EnableIRQ(USART1_CFG_IRQn);
    HAL_NVIC_SetPriority(USART1_CFG_IRQn, 3, 3);     /* 抢占优先级 3，子优先级 3 */
#endif
}

/**
 * @brief   串口1阻塞发送（寄存器直发，发送完成后等待 TC）
 * @param   pData : 数据指针
 * @param   len   : 数据长度
 */
void usart1SendBytes(uint8_t *pData, uint16_t len)
{
    if (pData == NULL || len == 0)
    {
        return;
    }

    for (uint16_t i = 0; i < len; i++)
    {
        while ((USART1_CFG_PERIPH->SR & USART_SR_TXE) == 0);
        USART1_CFG_PERIPH->DR = pData[i];
    }

    while ((USART1_CFG_PERIPH->SR & USART_SR_TC) == 0);     /* 等待发送完成 */
}
