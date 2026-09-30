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
#include "ringbuf.h"
#include <string.h>

static UART_HandleTypeDef s_uartHandle;
static DMA_HandleTypeDef s_uartRxDma;
static DMA_HandleTypeDef s_uartTxDma;

/* 环形接收缓冲（DMA 循环模式写入，2 的幂大小便于掩码取模） */
static uint8_t s_rxDmaBuf[USART1_RX_DMA_BUF_SIZE];
static ringbuf_t s_rxRing;              /* 接收环形缓冲 */

/* 发送环形缓冲（printf 与数据发送统一数据流） */
static uint8_t s_txBuf[USART1_TX_BUF_SIZE];
static ringbuf_t s_txRing;              /* 发送环形缓冲 */
static uint16_t s_txSendLen = 0;        /* 当前 DMA 段长度 */
static uint16_t s_txSendStart = 0;      /* 当前 DMA 段在缓冲中的起始下标（轮询兜底用） */

#if OS_USE_FREERTOS
static os_sem_handle_t s_rxFrameSem = NULL;     /* 帧到达信号量 */
#else
static volatile uint8_t s_rxFrameFlag = 0;      /* 裸机模式：帧到达标志 */
#endif

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
 * @brief   寄存器轮询发送（不依赖 DMA/中断）
 * @param   pData : 数据指针
 * @param   len   : 字节数
 * @note    用于两处：1) 上电横幅，便于区分"串口本身不通"与"DMA 链路不通"；
 *          2) DMA 启动失败时兜底，避免 TX 环形缓冲永久堵塞导致 printf 卡死。
 *          TXE 等待带超时上限，避免 UART 无时钟时死等。
 */
static void usart1SendPolling(const uint8_t *pData, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        uint32_t guard = 0x100000U;

        while (((USART1_CFG_PERIPH->SR & USART_SR_TXE) == 0) && (--guard != 0))
        {
            ;
        }

        if (guard == 0)
        {
            return;                 /* UART 时基异常，放弃本次发送 */
        }

        USART1_CFG_PERIPH->DR = pData[i];
    }

    /* 等最后一字节移位完成，保证复位/断电前数据已出。
     * 同样带超时上限：UART 时基异常时不能死等 */
    {
        uint32_t guard = 0x1000000U;

        while (((USART1_CFG_PERIPH->SR & USART_SR_TC) == 0) && (--guard != 0))
        {
            ;
        }
    }
}

/**
 * @brief   尝试把 TX 环形缓冲的一段连续数据交给 DMA
 * @retval  1  已启动一段
 *          0  DMA 正忙或缓冲为空，无需启动（由完成中断续传）
 *         -1  DMA 启动失败
 * @note    只有本函数会写 s_txSendLen / s_txSendStart，因此**调用方必须先与
 *          该函数的其它调用者互斥**：任务侧用 os_enter_critical()，完成中断
 *          里则天然互斥（见 HAL_UART_TxCpltCallback 的说明）。
 *          这里刻意不做加锁，因为本函数同时服务于任务与中断两条路径
 */
static int32_t usart1TxStartDma(void)
{
    uint16_t avail;

    if (s_uartHandle.gState != HAL_UART_STATE_READY)
    {
        return 0;                   /* 搬运进行中，由完成中断续传 */
    }

    /* 环形回绕：每次最多搬运到缓冲末尾的连续段 */
    avail = ringbuf_read_contig(&s_txRing);
    if (avail == 0)
    {
        return 0;
    }

    s_txSendStart = s_txRing.read;
    s_txSendLen = avail;

    return (HAL_UART_Transmit_DMA(&s_uartHandle, &s_txBuf[s_txSendStart], avail) == HAL_OK) ? 1 : -1;
}

/**
 * @brief   启动/续传 TX 环形缓冲的 DMA 搬运（任务上下文）
 * @note    「判忙 + 取连续段 + 启动 DMA」必须是一个原子操作：
 *          多个任务都会经 printf/SendAsync 走到这里，若在判忙与启动之间被抢占，
 *          后来者会看到 READY 并先启动一段，前者再启动时拿到 HAL_BUSY，
 *          于是轮询兜底把同一批数据又发一遍、同时推进读指针，输出重复且缓冲错位。
 *          因此用临界区保护，并把 TX DMA 中断优先级放在 FreeRTOS 管理范围内，
 *          保证完成中断不会插进这段临界区。
 *          **本函数只能在任务上下文调用**：中断里用 usart1TxStartDma()，
 *          taskENTER_CRITICAL 会断言"当前不在异常处理中"（port.c 的 VECTACTIVE 检查）
 */
static void usart1TxKick(void)
{
    int32_t result;
    uint16_t start;
    uint16_t len;

    os_enter_critical();
    result = usart1TxStartDma();
    start = s_txSendStart;
    len = s_txSendLen;
    os_exit_critical();

    if (result < 0)
    {
        /* 轮询发送会阻塞数十毫秒，绝不能放在临界区内（会拖垮系统节拍）。
         * HAL 出错时已把 gState 复位，这里只负责把数据送出去并推进读指针 */
        usart1SendPolling(&s_txBuf[start], len);

        os_enter_critical();
        ringbuf_advance_read(&s_txRing, len);
        os_exit_critical();

        usart1TxKick();             /* 继续搬下一段（含回绕） */
    }
}

/**
 * @brief   发送完成中断回调：推进消费指针并续传下一段
 * @note    这里**不能**调用 os_enter_critical()：它最终走到 vPortEnterCritical()，
 *          其中有一条 configASSERT 断言当前不在异常处理中（port.c 检查 ICSR 的
 *          VECTACTIVE），从中断调用必然触发断言；而 configASSERT 又用 printf 输出，
 *          printf 会再次入队并触发 DMA 完成中断，形成"断言→打印→中断→断言"的刷屏。
 *          这里也确实不需要加锁：
 *            1. TX DMA 中断优先级 == configMAX_SYSCALL_INTERRUPT_PRIORITY，
 *               任务侧的临界区(BASEPRI)已经把它挡住，所以它只可能在任务临界区
 *               之外运行，永远看不到 s_txSendLen / s_txRing 的中间状态；
 *            2. 中断不会被任务抢占，同级中断之间也不会互相抢占
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1_CFG_PERIPH)
    {
        return;
    }

    ringbuf_advance_read(&s_txRing, s_txSendLen);

    /* 续传下一段；失败不在此处兜底——中断里不能轮询发送，留给下一次
     * 任务侧的 usart1TxKick()（printf/SendAsync 每次都会调用）重试 */
    (void)usart1TxStartDma();
}

/**
 * @brief   fputc 重定向：printf 最终调用此函数
 * @note    字符入 TX 环形缓冲后立即返回（缓冲式，打印不阻塞）；
 *          缓冲满时产生背压等待。仅限任务上下文调用（入队需临界区保护）
 */
int fputc(int ch, FILE *f)
{
    (void)f;

    /* 初始化前打印：DMA 未就绪，退回寄存器直发（保留早期打印能力）。
     * TXE 等待带超时，避免 UART 无时钟时死等 */
    if (s_uartHandle.gState == HAL_UART_STATE_RESET)
    {
        uint32_t guard = 0x100000U;

        while (((USART1_CFG_PERIPH->SR & USART_SR_TXE) == 0) && (--guard != 0))
        {
            ;
        }

        USART1_CFG_PERIPH->DR = (uint8_t)ch;
        return ch;
    }

    os_enter_critical();

    /* 背压：缓冲满则等待 DMA 消费 */
    while (ringbuf_is_full(&s_txRing))
    {
        os_exit_critical();
        systickDelay_us(50);
        os_enter_critical();
    }

    ringbuf_write_byte(&s_txRing, (uint8_t)ch);

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

    ringbuf_set_write(&s_rxRing, received);

    return s_rxRing.write;
}

/**
 * @brief   获取当前可读字节数
 */
uint16_t usart1RxAvailable(void)
{
    usart1RxWriteIndex();       /* 刷新 DMA 写指针（NDTR） */
    return ringbuf_available(&s_rxRing);
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

    return ringbuf_read_byte(&s_rxRing, pData);
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
    if (pBuf == NULL || len == 0)
    {
        return 0;
    }

    usart1RxAvailable();        /* 刷新 DMA 写指针（NDTR）（不调用底层函数：减少接口暴露，屏蔽底层） */

    return ringbuf_read(&s_rxRing, pBuf, len);
}

/**
 * @brief   清空接收缓冲区（丢弃全部未读数据）
 */
void usart1FlushRx(void)
{
    usart1RxWriteIndex();
    ringbuf_discard_all(&s_rxRing);
}

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
 * @brief   串口1中断服务函数（中断号取自 BSP 宏，便于换串口）
 * @note    1. IDLE（帧结束）与 ORE（溢出）标志先手动清除：
 *             读 SR 后再读 DR 即清标志（DMA 模式下读 DR 不影响接收数据）
 *          2. 其余错误经 HAL 通用处理；HAL 因错误停掉 DMA 接收时自动重启
 */
void USART1_CFG_IRQHandler(void)
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
 * @retval  STATUS_OK / STATUS_ERR / STATUS_ERR_BUSY
 * @note    启动环形 DMA 接收并使能 IDLE 帧检测。
 *          IDLE 中断必须在 DMA 接收启动之后再使能：否则启动前若已有一个
 *          IDLE/ORE 挂起，ISR 会先把 DMA 启起来，本函数随后再启动就拿到
 *          HAL_BUSY（原实现直接 while(1) 死机）
 */
status_t usart1Init(uint32_t baudRate)
{
    s_uartHandle.Instance = USART1_CFG_PERIPH;
    s_uartHandle.Init.BaudRate = baudRate;
    s_uartHandle.Init.WordLength = UART_WORDLENGTH_8B;
    s_uartHandle.Init.StopBits = UART_STOPBITS_1;
    s_uartHandle.Init.Parity = UART_PARITY_NONE;
    s_uartHandle.Init.Mode = UART_MODE_TX_RX;
    s_uartHandle.Init.HwFlowCtl = UART_HWCONTROL_NONE;

    /* 初始化收发环形缓冲（大小必须为 2 的幂） */
    ringbuf_init(&s_rxRing, s_rxDmaBuf, USART1_RX_DMA_BUF_SIZE);
    ringbuf_init(&s_txRing, s_txBuf, USART1_TX_BUF_SIZE);

    if (HAL_UART_Init(&s_uartHandle) != HAL_OK)
    {
        return STATUS_ERR;      /* 引脚/时钟配置异常，交由上层决定是否继续 */
    }

    /* 上电横幅：用寄存器轮询直发，不依赖 DMA 与中断。
     * 若串口助手连这一行都收不到，问题就在 CH340 / 跳线帽 / 驱动 / 线材一侧；
     * 若只收到这一行、后面没输出，才是固件 DMA 发送链的问题（已有轮询兜底）。 */
    {
        static const char banner[] = "\r\n[USART1] 115200 8N1 ready\r\n";

        usart1SendPolling((const uint8_t *)banner, (uint16_t)(sizeof(banner) - 1));
    }

#if USART1_RX_ENABLE
#if OS_USE_FREERTOS
    s_rxFrameSem = os_sem_create_binary();      /* 帧到达信号量 */
#endif

    /* 先启动环形 DMA 接收，再使能 IDLE 帧结束检测（顺序不可颠倒） */
    if (HAL_UART_Receive_DMA(&s_uartHandle, s_rxDmaBuf, USART1_RX_DMA_BUF_SIZE) != HAL_OK)
    {
        return STATUS_ERR_BUSY;     /* 不阻塞启动流程，接收功能降级为不可用 */
    }

    __HAL_UART_ENABLE_IT(&s_uartHandle, UART_IT_IDLE);
#endif

    return STATUS_OK;
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

    gpioInitStruct.Mode = GPIO_MODE_AF_PP;
    gpioInitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    gpioInitStruct.Alternate = USART1_GPIO_AF;      /* F4 引脚复用功能号（无 AFIO 寄存器） */

    /* TX 引脚（AF7） */
    gpioInitStruct.Pin = USART1_TX_GPIO_PIN;
    gpioInitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(USART1_TX_GPIO_PORT, &gpioInitStruct);

    /* RX 引脚（AF7）：F4 无 GPIO_MODE_AF_INPUT，统一按复用推挽配置，
     * 上拉保证空闲态为高电平，避免悬空引入噪声/误帧 */
    gpioInitStruct.Pin = USART1_RX_GPIO_PIN;
    gpioInitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(USART1_RX_GPIO_PORT, &gpioInitStruct);

#if USART1_RX_ENABLE
    /* RX DMA：外设 -> 内存，循环模式（环形缓冲核心） */
    s_uartRxDma.Instance = USART1_RX_DMA_STREAM;
    s_uartRxDma.Init.Channel = USART1_RX_DMA_CHANNEL;   /* F4：每个流需指定请求通道 */
    s_uartRxDma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    s_uartRxDma.Init.PeriphInc = DMA_PINC_DISABLE;
    s_uartRxDma.Init.MemInc = DMA_MINC_ENABLE;
    s_uartRxDma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    s_uartRxDma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    s_uartRxDma.Init.Mode = DMA_CIRCULAR;
    s_uartRxDma.Init.Priority = DMA_PRIORITY_HIGH;
    s_uartRxDma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;   /* 直通模式，单字节搬运 */
    s_uartRxDma.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_1QUARTERFULL;
    s_uartRxDma.Init.MemBurst = DMA_MBURST_SINGLE;
    s_uartRxDma.Init.PeriphBurst = DMA_PBURST_SINGLE;
    HAL_DMA_Init(&s_uartRxDma);
    __HAL_LINKDMA(huart, hdmarx, s_uartRxDma);

    /* 优先级 6/0：只做 DMA 状态维护，不调用任何 FreeRTOS API。
     * 注意必须 >= configMAX_SYSCALL_INTERRUPT_PRIORITY(5)，否则会插进
     * 任务的 os_enter_critical() 里，破坏临界区的原子性 */
    HAL_NVIC_SetPriority(USART1_RX_DMA_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART1_RX_DMA_IRQn);
#endif

    /* TX DMA：内存 -> 外设，普通模式 */
    s_uartTxDma.Instance = USART1_TX_DMA_STREAM;
    s_uartTxDma.Init.Channel = USART1_TX_DMA_CHANNEL;   /* F4：每个流需指定请求通道 */
    s_uartTxDma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    s_uartTxDma.Init.PeriphInc = DMA_PINC_DISABLE;
    s_uartTxDma.Init.MemInc = DMA_MINC_ENABLE;
    s_uartTxDma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    s_uartTxDma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    s_uartTxDma.Init.Mode = DMA_NORMAL;
    s_uartTxDma.Init.Priority = DMA_PRIORITY_HIGH;
    s_uartTxDma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;   /* 直通模式，单字节搬运 */
    s_uartTxDma.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_1QUARTERFULL;
    s_uartTxDma.Init.MemBurst = DMA_MBURST_SINGLE;
    s_uartTxDma.Init.PeriphBurst = DMA_PBURST_SINGLE;
    HAL_DMA_Init(&s_uartTxDma);
    __HAL_LINKDMA(huart, hdmatx, s_uartTxDma);

    /* 优先级 5/0：TxCplt 回调里要推进 TX 环形缓冲读指针并续传下一段，
     * 必须与任务的 os_enter_critical() 互斥，故不能高于 5 */
    HAL_NVIC_SetPriority(USART1_TX_DMA_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_TX_DMA_IRQn);

    /* USART 中断（含 IDLE/ORE 帧处理）
     * 优先级 5/0：位于 FreeRTOS 管理范围内（5~15），
     * IDLE 回调中可安全调用 FromISR API（帧信号量）。
     * 先设优先级再使能，避免以默认优先级短暂开中断 */
    HAL_NVIC_SetPriority(USART1_CFG_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_CFG_IRQn);
}

/**
 * @brief   非阻塞发送：数据入 TX 环形缓冲后立即返回
 * @param   pData : 数据指针（入队即完成拷贝，返回后可复用缓冲）
 * @param   len   : 数据长度
 * @note    批量入队：ringbuf_write 一次写入一段连续空间（自动处理回绕），
 *          整个 len 通常只需 1 次临界区；只在缓冲满时退出去等待 DMA 消费。
 *          原实现每字节一次 os_enter_critical/exit，256 字节的 printf
 *          就要进出临界区 256 次
 */
void usart1SendAsync(uint8_t *pData, uint16_t len)
{
    uint16_t written = 0;

    if (pData == NULL || len == 0)
    {
        return;
    }

    while (written < len)
    {
        uint16_t n;

        os_enter_critical();
        n = ringbuf_write(&s_txRing, &pData[written], (uint16_t)(len - written));
        os_exit_critical();

        if (n == 0)
        {
            usart1TxKick();             /* 缓冲满：先推动 DMA，腾出空间 */
            systickDelay_us(50);        /* 背压等待（任务上下文） */
            continue;
        }

        written = (uint16_t)(written + n);
    }

    usart1TxKick();
}

/**
 * @brief   发送完成查询
 * @retval  1 缓冲已排空且末字节移位完成；0 发送进行中
 */
uint8_t usart1TxDone(void)
{
    if (ringbuf_available(&s_txRing) != 0)
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
