/**
 * @file    usart.c
 * @brief   串口1驱动实现（环形缓冲区 + DMA 收发 + printf 重定向 + 半主机禁用）
 * @note    接收：DMA 循环模式直接写入环形缓冲（CPU 零拷贝），
 *          读写位置由 NDTR 计数器与读指针推算，单生产者（DMA）单消费者模型无需加锁；
 *          总线空闲（IDLE）中断通知帧结束（信号量唤醒 usart1WaitFrame）
 * @note    发送：统一走 TX 环形缓冲（printf 与数据发送共用一条数据流，不会交叉）：
 *          fputc/SendAsync 入队即返回，DMA 空闲时由 Kick 搬运，
 *          发送完成中断续传下一段直到排空；SendBytes 为入队+等待排空的阻塞版。
 *          缓冲满时产生背压等待（printf 仅限任务上下文调用）
 */
#include "usart.h"
#include "os_wrapper.h"
#include "delay.h"
#include <string.h>

static UART_HandleTypeDef s_uartHandle;
static DMA_HandleTypeDef s_uartRxDma;
static DMA_HandleTypeDef s_uartTxDma;

/* 环形接收缓冲（DMA 循环模式写入，2 的幂大小便于掩码取模） */
static uint8_t s_rxDmaBuf[USART1_RX_DMA_BUF_SIZE];
static uint16_t s_rxReadIndex = 0;      /* 消费者读指针 */

/* 发送环形缓冲（printf 与数据发送统一数据流） */
static uint8_t s_txBuf[USART1_TX_BUF_SIZE];
static volatile uint16_t s_txReadIndex = 0;     /* DMA 消费指针 */
static volatile uint16_t s_txWriteIndex = 0;    /* 生产者写指针 */
static uint16_t s_txSendLen = 0;                /* 当前 DMA 段长度 */

#if OS_USE_FREERTOS
static os_sem_handle_t s_rxFrameSem = NULL;     /* 帧到达信号量 */
#else
static volatile uint8_t s_rxFrameFlag = 0;      /* 裸机模式：帧到达标志 */
#endif

#define RX_DMA_MASK     (USART1_RX_DMA_BUF_SIZE - 1)
#define TX_DMA_MASK     (USART1_TX_BUF_SIZE - 1)

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
 * @brief   启动/续传 TX 环形缓冲的 DMA 搬运
 * @note    DMA 空闲且缓冲非空时，搬运读指针处的一段连续数据
 */
static void usart1TxKick(void)
{
    uint16_t avail;

    if (s_uartHandle.gState != HAL_UART_STATE_READY)
    {
        return;                     /* 搬运进行中，由完成中断续传 */
    }

    avail = (s_txWriteIndex - s_txReadIndex) & TX_DMA_MASK;
    if (avail == 0)
    {
        return;
    }

    /* 环形回绕：每次最多搬运到缓冲末尾的连续段 */
    uint16_t firstSeg = USART1_TX_BUF_SIZE - s_txReadIndex;
    if (avail > firstSeg)
    {
        avail = firstSeg;
    }

    s_txSendLen = avail;
    HAL_UART_Transmit_DMA(&s_uartHandle, &s_txBuf[s_txReadIndex], avail);
}

/**
 * @brief   发送完成中断回调：推进消费指针并续传下一段
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1_CFG_PERIPH)
    {
        return;
    }

    s_txReadIndex = (s_txReadIndex + s_txSendLen) & TX_DMA_MASK;
    usart1TxKick();
}

/**
 * @brief   fputc 重定向：printf 最终调用此函数
 * @note    字符入 TX 环形缓冲后立即返回（缓冲式，打印不阻塞）；
 *          缓冲满时产生背压等待。仅限任务上下文调用（入队需临界区保护）
 */
int fputc(int ch, FILE *f)
{
    (void)f;

    /* 初始化前打印：DMA 未就绪，退回寄存器直发（保留早期打印能力） */
    if (s_uartHandle.gState == HAL_UART_STATE_RESET)
    {
        while ((USART1_CFG_PERIPH->SR & USART_SR_TXE) == 0);
        USART1_CFG_PERIPH->DR = (uint8_t)ch;
        return ch;
    }

    os_enter_critical();

    /* 背压：缓冲满则等待 DMA 消费 */
    while (((s_txWriteIndex + 1) & TX_DMA_MASK) == s_txReadIndex)
    {
        os_exit_critical();
        systickDelay_us(50);
        os_enter_critical();
    }

    s_txBuf[s_txWriteIndex] = (uint8_t)ch;
    s_txWriteIndex = (s_txWriteIndex + 1) & TX_DMA_MASK;

    os_exit_critical();

    usart1TxKick();

    return ch;
}

/* ==================== 环形缓冲区访问接口 ==================== */
#if USART1_RX_ENABLE

/**
 * @brief   计算 DMA 写入位置（已接收字节数 = 缓冲大小 - NDTR 剩余计数）
 */
static uint16_t usart1RxWriteIndex(void)
{
    uint16_t received = USART1_RX_DMA_BUF_SIZE -
                        (uint16_t)__HAL_DMA_GET_COUNTER(s_uartHandle.hdmarx);

    return received & RX_DMA_MASK;
}

/**
 * @brief   获取当前可读字节数
 */
uint16_t usart1RxAvailable(void)
{
    return (usart1RxWriteIndex() - s_rxReadIndex) & RX_DMA_MASK;
}

/**
 * @brief   读 1 字节（非阻塞）
 * @param   pData : 数据指针
 * @retval  1 读取成功；0 无数据
 */
uint8_t usart1ReadByte(uint8_t *pData)
{
    if (pData == NULL || usart1RxAvailable() == 0)
    {
        return 0;
    }

    *pData = s_rxDmaBuf[s_rxReadIndex];
    s_rxReadIndex = (s_rxReadIndex + 1) & RX_DMA_MASK;

    return 1;
}

/**
 * @brief   读多个字节（非阻塞，最多返回当前可读数量）
 * @param   pBuf : 数据缓冲
 * @param   len  : 期望读取字节数
 * @retval  实际读取字节数
 * @note    环形回绕最多两次 memcpy；调用方保证单任务访问（单消费者模型）
 */
uint16_t usart1ReadBytes(uint8_t *pBuf, uint16_t len)
{
    uint16_t avail;
    uint16_t firstSeg;

    if (pBuf == NULL || len == 0)
    {
        return 0;
    }

    avail = usart1RxAvailable();
    if (len > avail)
    {
        len = avail;
    }

    firstSeg = USART1_RX_DMA_BUF_SIZE - s_rxReadIndex;      /* 到缓冲区末尾的连续段长度 */
    if (len <= firstSeg)
    {
        memcpy(pBuf, &s_rxDmaBuf[s_rxReadIndex], len);
    }
    else
    {
        memcpy(pBuf, &s_rxDmaBuf[s_rxReadIndex], firstSeg);
        memcpy(pBuf + firstSeg, s_rxDmaBuf, len - firstSeg);
    }

    s_rxReadIndex = (s_rxReadIndex + len) & RX_DMA_MASK;

    return len;
}

/**
 * @brief   清空接收缓冲区（丢弃全部未读数据）
 */
void usart1FlushRx(void)
{
    s_rxReadIndex = usart1RxWriteIndex();
}

/**
 * @brief   帧结束（总线空闲）回调（弱函数，应用层可重写）
 * @param   len : 当前缓冲内可读字节数（即本帧长度）
 * @note    在 USART 中断上下文中执行
 */
/**
 * @brief   等待一帧到达（阻塞接口，替代任务轮询）
 * @param   timeoutMs : 超时毫秒数，0xFFFFFFFF 表示永久等待
 * @retval  当前可读字节数（帧长度）；0 表示超时
 * @note    FreeRTOS 模式：阻塞在帧信号量上（由 IDLE 中断释放），
 *          USART1 中断优先级须在 FreeRTOS 管理范围内（>=5）
 *          裸机模式：按 tick 忙等待帧标志
 */
uint16_t usart1WaitFrame(uint32_t timeoutMs)
{
#if OS_USE_FREERTOS
    if (s_rxFrameSem == NULL || !os_sem_take(s_rxFrameSem, timeoutMs))
    {
        return 0;
    }
#else
    uint32_t startTick = HAL_GetTick();

    while (!s_rxFrameFlag)
    {
        if ((timeoutMs != 0xFFFFFFFF) && ((HAL_GetTick() - startTick) >= timeoutMs))
        {
            return 0;
        }
    }
    s_rxFrameFlag = 0;
#endif
    return usart1RxAvailable();
}

/**
 * @brief   帧结束（总线空闲）回调（弱函数，应用层可重写）
 * @param   len : 当前缓冲内可读字节数（即本帧长度）
 * @note    在 USART 中断上下文中执行；驱动默认实现释放帧信号量
 *          唤醒 usart1WaitFrame。应用层重写时需自行处理通知
 */
__WEAK void usart1RxIdleHook(uint16_t len)
{
    (void)len;

#if OS_USE_FREERTOS
    if (s_rxFrameSem != NULL)
    {
        os_sem_give_from_isr(s_rxFrameSem);
    }
#else
    s_rxFrameFlag = 1;
#endif
}

#endif /* USART1_RX_ENABLE */

/* ==================== 中断服务函数 ==================== */
#if USART1_RX_ENABLE
/**
 * @brief   串口1中断服务函数
 * @note    1. IDLE（帧结束）与 ORE（溢出）标志先手动清除：
 *             读 SR 后再读 DR 即清标志（DMA 模式下读 DR 不影响接收数据）
 *          2. 其余错误经 HAL 通用处理；HAL 因错误停掉 DMA 接收时自动重启
 */
void USART1_IRQHandler(void)
{
    uint32_t sr = USART1_CFG_PERIPH->SR;

    if (sr & (USART_SR_IDLE | USART_SR_ORE))
    {
        (void)USART1_CFG_PERIPH->DR;        /* 清 IDLE/ORE 标志 */

        if (sr & USART_SR_IDLE)
        {
            usart1RxIdleHook(usart1RxAvailable());
        }
    }

    HAL_UART_IRQHandler(&s_uartHandle);

    /* HAL 错误路径会停掉 DMA 接收（RxState 回到 READY），此处自动重启 */
    if (s_uartHandle.RxState == HAL_UART_STATE_READY)
    {
        HAL_UART_Receive_DMA(&s_uartHandle, s_rxDmaBuf, USART1_RX_DMA_BUF_SIZE);
    }
}

/**
 * @brief   接收 DMA 中断服务函数（环形模式下每个周期触发，维护 HAL 状态机）
 */
void USART1_RX_DMA_IRQHandler(void)
{
    HAL_DMA_IRQHandler(s_uartHandle.hdmarx);
}
#endif /* USART1_RX_ENABLE */

/**
 * @brief   发送 DMA 中断服务函数
 */
void USART1_TX_DMA_IRQHandler(void)
{
    HAL_DMA_IRQHandler(s_uartHandle.hdmatx);
}

/* ==================== 初始化与发送 ==================== */
/**
 * @brief   串口1初始化
 * @param   baudRate : 波特率
 * @note    启动环形 DMA 接收并使能 IDLE 帧检测
 */
void usart1Init(uint32_t baudRate)
{
    s_uartHandle.Instance = USART1_CFG_PERIPH;
    s_uartHandle.Init.BaudRate = baudRate;
    s_uartHandle.Init.WordLength = UART_WORDLENGTH_8B;
    s_uartHandle.Init.StopBits = UART_STOPBITS_1;
    s_uartHandle.Init.Parity = UART_PARITY_NONE;
    s_uartHandle.Init.Mode = UART_MODE_TX_RX;
    s_uartHandle.Init.HwFlowCtl = UART_HWCONTROL_NONE;

    if (HAL_UART_Init(&s_uartHandle) != HAL_OK)
    {
        while (1);
    }

#if USART1_RX_ENABLE
#if OS_USE_FREERTOS
    s_rxFrameSem = os_sem_create_binary();      /* 帧到达信号量 */
#endif

    /* 启动环形 DMA 接收 + IDLE 帧结束检测 */
    __HAL_UART_ENABLE_IT(&s_uartHandle, UART_IT_IDLE);

    if (HAL_UART_Receive_DMA(&s_uartHandle, s_rxDmaBuf, USART1_RX_DMA_BUF_SIZE) != HAL_OK)
    {
        while (1);
    }
#endif
}

/**
 * @brief   串口1 MSP 底层初始化（由 HAL_UART_Init 自动调用）
 * @note    配置 GPIO、USART 时钟与 RX/TX DMA 通道（RX 为循环模式）
 */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1_CFG_PERIPH)
    {
        return;
    }

    GPIO_InitTypeDef gpioInitStruct = {0};

    USART1_RX_GPIO_CLK_ENABLE();
    USART1_TX_GPIO_CLK_ENABLE();
    USART1_CFG_CLK_ENABLE();
    USART1_RX_DMA_CLK_ENABLE();
    USART1_TX_DMA_CLK_ENABLE();

    /* TX 引脚 */
    gpioInitStruct.Pin = USART1_TX_GPIO_PIN;
    gpioInitStruct.Pull = GPIO_PULLUP;
    gpioInitStruct.Mode = GPIO_MODE_AF_PP;
    gpioInitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(USART1_TX_GPIO_PORT, &gpioInitStruct);

    /* RX 引脚 */
    gpioInitStruct.Pin = USART1_RX_GPIO_PIN;
    gpioInitStruct.Pull = GPIO_NOPULL;
    gpioInitStruct.Mode = GPIO_MODE_AF_INPUT;
    HAL_GPIO_Init(USART1_RX_GPIO_PORT, &gpioInitStruct);

#if USART1_RX_ENABLE
    /* RX DMA：外设 -> 内存，循环模式（环形缓冲核心） */
    s_uartRxDma.Instance = USART1_RX_DMA_CHANNEL;
    s_uartRxDma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    s_uartRxDma.Init.PeriphInc = DMA_PINC_DISABLE;
    s_uartRxDma.Init.MemInc = DMA_MINC_ENABLE;
    s_uartRxDma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    s_uartRxDma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    s_uartRxDma.Init.Mode = DMA_CIRCULAR;
    s_uartRxDma.Init.Priority = DMA_PRIORITY_HIGH;
    HAL_DMA_Init(&s_uartRxDma);
    __HAL_LINKDMA(huart, hdmarx, s_uartRxDma);

    HAL_NVIC_SetPriority(USART1_RX_DMA_IRQn, 3, 2);
    HAL_NVIC_EnableIRQ(USART1_RX_DMA_IRQn);
#endif

    /* TX DMA：内存 -> 外设，普通模式 */
    s_uartTxDma.Instance = USART1_TX_DMA_CHANNEL;
    s_uartTxDma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    s_uartTxDma.Init.PeriphInc = DMA_PINC_DISABLE;
    s_uartTxDma.Init.MemInc = DMA_MINC_ENABLE;
    s_uartTxDma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    s_uartTxDma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    s_uartTxDma.Init.Mode = DMA_NORMAL;
    s_uartTxDma.Init.Priority = DMA_PRIORITY_HIGH;
    HAL_DMA_Init(&s_uartTxDma);
    __HAL_LINKDMA(huart, hdmatx, s_uartTxDma);

    HAL_NVIC_SetPriority(USART1_TX_DMA_IRQn, 3, 1);
    HAL_NVIC_EnableIRQ(USART1_TX_DMA_IRQn);

    /* USART 中断（含 IDLE/ORE 帧处理）
     * 优先级 5/0：位于 FreeRTOS 管理范围内（5~15），
     * IDLE 回调中可安全调用 FromISR API（帧信号量） */
    HAL_NVIC_EnableIRQ(USART1_CFG_IRQn);
    HAL_NVIC_SetPriority(USART1_CFG_IRQn, 5, 0);
}

/**
 * @brief   非阻塞发送：数据入 TX 环形缓冲后立即返回
 * @param   pData : 数据指针（入队即完成拷贝，返回后可复用缓冲）
 * @param   len   : 数据长度
 * @note    缓冲满时产生背压等待；DMA 搬运由完成中断自动续传
 */
void usart1SendAsync(uint8_t *pData, uint16_t len)
{
    if (pData == NULL || len == 0)
    {
        return;
    }

    for (uint16_t i = 0; i < len; i++)
    {
        os_enter_critical();

        while (((s_txWriteIndex + 1) & TX_DMA_MASK) == s_txReadIndex)
        {
            os_exit_critical();
            systickDelay_us(50);
            os_enter_critical();
        }

        s_txBuf[s_txWriteIndex] = (uint8_t)pData[i];
        s_txWriteIndex = (s_txWriteIndex + 1) & TX_DMA_MASK;

        os_exit_critical();
    }

    usart1TxKick();
}

/**
 * @brief   发送完成查询
 * @retval  1 缓冲已排空且末字节移位完成；0 发送进行中
 */
uint8_t usart1TxDone(void)
{
    if (((s_txWriteIndex - s_txReadIndex) & TX_DMA_MASK) != 0)
    {
        return 0;       /* 缓冲中仍有数据 */
    }

    if (s_uartHandle.gState != HAL_UART_STATE_READY)
    {
        return 0;       /* 最后一段仍在搬运 */
    }

    if ((USART1_CFG_PERIPH->SR & USART_SR_TC) == 0)
    {
        return 0;       /* 末字节仍在移位 */
    }

    return 1;
}

/**
 * @brief   串口1阻塞发送（入队 + 等待排空）
 * @param   pData : 数据指针
 * @param   len   : 数据长度
 * @note    函数返回时数据已全部发出（与原语义一致）；
 *          等待用 SysTick 忙等，调度器启动前后均可调用
 */
void usart1SendBytes(uint8_t *pData, uint16_t len)
{
    usart1SendAsync(pData, len);

    while (!usart1TxDone())
    {
        systickDelay_us(100);
    }
}
