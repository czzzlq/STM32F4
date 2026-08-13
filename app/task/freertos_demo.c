/**
 * @file    freertos_demo.c
 * @brief   重写驱动后的烧录验证程序
 * @note    验证内容：
 *          1. 串口环形缓冲区 + DMA：PC 发送任意数据帧，板子原样回显
 *          2. 外部 SRAM：上电自检（覆盖 32 位/16 位/字节三种拷贝路径）
 *          3. 内存池（malloc）：上电自检（分配/写/读回/释放/使用率）
 *          4. LCD：绘图算法演示（窗口填充/分段画线/圆/数字显示）
 *          5. KEY：KEY0 短按翻转 LED0；KEY1 短按重跑自检；WKUP 长按重绘 LCD
 *          6. LED：LED1 500ms 闪烁
 */
#include "freertos_demo.h"
#include <string.h>

/* 任务优先级与堆栈 */
#define START_TASK_PRIO     1
#define START_STK_SIZE      128
#define KEY_TASK_PRIO       2
#define KEY_TASK_STK_SIZE   128
#define LED_TASK_PRIO       3
#define LED_TASK_STK_SIZE   128
#define UART_TASK_PRIO      4
#define UART_TASK_STK_SIZE  128

static os_task_handle_t s_startTaskHandle;
static os_task_handle_t s_keyTaskHandle;
static os_task_handle_t s_ledTaskHandle;
static os_task_handle_t s_uartTaskHandle;

/* ==================== 串口帧通知（ISR 置位，任务消费） ==================== */
static volatile uint8_t s_uartFrameReady = 0;
static volatile uint16_t s_uartFrameLen = 0;

/**
 * @brief   串口帧结束（总线空闲）回调，在 USART 中断上下文中执行
 * @param   len : 缓冲内可读字节数（本帧长度）
 * @note    只置标志，实际处理放到任务中（ISR 保持短小）
 */
void usart1RxIdleHook(uint16_t len)
{
    s_uartFrameLen = len;
    s_uartFrameReady = 1;
}

/* ==================== 自检用静态缓冲 ==================== */
/* 避开 malloc 外部内存池区（0x68000000 ~ 0x68000022），测试区从 0x1000 开始 */
#define SRAM_TEST_OFFSET    0x1000
#define SRAM_TEST_SIZE      1024

static uint8_t s_sramTestBuf[SRAM_TEST_SIZE];       /* 写入数据 */
static uint8_t s_sramTestCheck[SRAM_TEST_SIZE];     /* 回读校验数据 */

/**
 * @brief   外部 SRAM 读写自检
 * @note    覆盖 sram_write/read 的三级路径：
 *          A. 双 4 字节对齐 + 4 的倍数长度（32 位路径）
 *          B. 奇地址（头部字节对齐 + 16 位路径）
 *          C. 奇数长度（16 位路径 + 尾部字节）
 *          D. 单字节（sram_test_* 接口）
 */
static void sram_self_test(void)
{
    uint32_t errCount = 0;
    uint16_t i;

    /* 生成测试模式（全字节遍历） */
    for (i = 0; i < SRAM_TEST_SIZE; i++)
    {
        s_sramTestBuf[i] = (uint8_t)i;
    }

    /* A. 双 4 字节对齐 + 1024 字节（32 位拷贝路径） */
    sram_write(s_sramTestBuf, SRAM_TEST_OFFSET, 1024);
    memset(s_sramTestCheck, 0, SRAM_TEST_SIZE);
    sram_read(s_sramTestCheck, SRAM_TEST_OFFSET, 1024);
    if (memcmp(s_sramTestBuf, s_sramTestCheck, 1024) != 0)
    {
        errCount++;
    }

    /* B. 奇地址 + 1013 字节（头部字节 + 16 位路径） */
    sram_write(s_sramTestBuf, SRAM_TEST_OFFSET + 1, 1013);
    memset(s_sramTestCheck, 0, SRAM_TEST_SIZE);
    sram_read(s_sramTestCheck, SRAM_TEST_OFFSET + 1, 1013);
    if (memcmp(s_sramTestBuf, s_sramTestCheck, 1013) != 0)
    {
        errCount++;
    }

    /* C. 偶地址 + 1023 字节（16 位路径 + 尾部字节） */
    sram_write(s_sramTestBuf, SRAM_TEST_OFFSET, 1023);
    memset(s_sramTestCheck, 0, SRAM_TEST_SIZE);
    sram_read(s_sramTestCheck, SRAM_TEST_OFFSET, 1023);
    if (memcmp(s_sramTestBuf, s_sramTestCheck, 1023) != 0)
    {
        errCount++;
    }

    /* D. 单字节接口 */
    sram_test_write(SRAM_TEST_OFFSET + 2048, 0x5A);
    if (sram_test_read(SRAM_TEST_OFFSET + 2048) != 0x5A)
    {
        errCount++;
    }

    printf("[SRAM] self test: %s (%d errors)\r\n", (errCount == 0) ? "PASS" : "FAIL", errCount);
}

/**
 * @brief   内存池自检（分配 -> 写模式 -> 校验 -> 释放 -> 使用率归零）
 * @note    验证按字扫描的首次适配分配器与释放逻辑
 */
static void malloc_self_test(void)
{
    uint8_t *p1, *p2, *p3;
    uint32_t errCount = 0;
    uint16_t i;

    p1 = (uint8_t *)mymalloc(SRAMIN, 512);      /* 内部 SRAM：512 字节（16 块） */
    p2 = (uint8_t *)mymalloc(SRAMIN, 64);       /* 内部 SRAM：64 字节（2 块） */
    p3 = (uint8_t *)mymalloc(SRAMEX, 16);       /* 外部 SRAM：16 字节（1 块，池共 32 字节） */

    printf("[MALLOC] p1=0x%x p2=0x%x p3=0x%x\r\n", (unsigned int)p1, (unsigned int)p2, (unsigned int)p3);

    if (p1 == NULL || p2 == NULL || p3 == NULL)
    {
        printf("[MALLOC] self test: FAIL (alloc failed)\r\n");
        return;
    }

    /* 写模式并校验 */
    for (i = 0; i < 512; i++) p1[i] = (uint8_t)i;
    for (i = 0; i < 512; i++)
    {
        if (p1[i] != (uint8_t)i) errCount++;
    }

    for (i = 0; i < 64; i++) p2[i] = (uint8_t)(0xFF - i);
    for (i = 0; i < 64; i++)
    {
        if (p2[i] != (uint8_t)(0xFF - i)) errCount++;
    }

    for (i = 0; i < 16; i++) p3[i] = 0xA5;
    for (i = 0; i < 16; i++)
    {
        if (p3[i] != 0xA5) errCount++;
    }

    /* 释放后使用率应归零 */
    myfree(SRAMIN, p1);
    myfree(SRAMIN, p2);
    myfree(SRAMEX, p3);

    if (my_mem_perused(SRAMIN) != 0 || my_mem_perused(SRAMEX) != 0)
    {
        errCount++;
    }

    printf("[MALLOC] self test: %s (%d errors, used IN=%d%% EX=%d%%)\r\n",
           (errCount == 0) ? "PASS" : "FAIL", errCount,
           my_mem_perused(SRAMIN) / 10, my_mem_perused(SRAMEX) / 10);
}

/**
 * @brief   LCD 绘图演示（覆盖重写后的各绘图算法）
 */
static void demo_lcd(void)
{
    lcd_clear(WHITE);
    lcd_show_string(10, 5, 220, 32, 16, "Driver Verify", RED);

    /* 窗口填充：单次设窗口连续写 RAM */
    lcd_fill(5,   45, 75,  145, BLUE);
    lcd_fill(85,  45, 155, 145, GREEN);
    lcd_fill(165, 45, 235, 145, RED);

    /* 画线：水平（段批量化退化单段）/ 竖直（单列填充快路径）/ 斜线 */
    lcd_draw_line(5,   160, 235, 160, BLACK);
    lcd_draw_line(120, 155, 120, 315, BLACK);
    lcd_draw_line(5,   155, 235, 315, GRAY);

    /* 矩形 / 圆（轮廓）/ 实心圆 */
    lcd_draw_rectangle(10, 170, 80, 240, BROWN);
    lcd_draw_circle(190, 290, 25, MAGENTA);
    lcd_fill_circle(140, 205, 20, YELLOW);

    /* 数字显示：10 次幂查表 */
    lcd_show_num(10, 275, 12345, 5, 16, BLACK);
    lcd_show_xnum(120, 275, 9876, 8, 16, 0x80, BLACK);     /* 高位补 0 */
}

/* ==================== 任务函数 ==================== */
/**
 * @brief   按键任务：KEY0 翻转 LED0；KEY1 重跑自检；WKUP 长按重绘 LCD
 */
static void key_task(void *pvParameters)
{
    UNUSED(pvParameters);

    while (1)
    {
        if (drv_key_get_event(DRV_KEY_ID_0) == DRV_KEY_EVENT_PRESS)
        {
            drv_led_toggle(DRV_LED_ID_0);
            printf("[KEY] KEY0 press, LED0 toggle\r\n");
        }

        if (drv_key_get_event(DRV_KEY_ID_1) == DRV_KEY_EVENT_PRESS)
        {
            printf("[KEY] KEY1 press, re-run self test\r\n");
            sram_self_test();
            malloc_self_test();
        }

        if (drv_key_get_event(DRV_KEY_ID_WKUP) == DRV_KEY_EVENT_LONG_PRESS_TRIGGER)
        {
            printf("[KEY] WKUP long press, redraw LCD\r\n");
            demo_lcd();
        }

        os_delay_ms(10);
    }
}

/**
 * @brief   LED 任务：LED1 500ms 闪烁
 */
static void led_task(void *pvParameters)
{
    UNUSED(pvParameters);

    while (1)
    {
        drv_led_toggle(DRV_LED_ID_1);
        os_delay_ms(500);
    }
}

/**
 * @brief   串口任务：收到一帧（IDLE 触发）后原样回显（环形缓冲 + DMA 收发验证）
 * @note    回显路径：DMA 环收 -> usart1ReadBytes -> usart1SendBytes(DMA 发送)
 */
static void uart_task(void *pvParameters)
{
    UNUSED(pvParameters);
    static uint8_t frameBuf[USART1_RX_DMA_BUF_SIZE];

    printf("UART echo ready (ring buffer + DMA)\r\n");

    while (1)
    {
        if (s_uartFrameReady)
        {
            uint16_t len = s_uartFrameLen;
            s_uartFrameReady = 0;

            if (len > 0)
            {
                len = usart1ReadBytes(frameBuf, sizeof(frameBuf));
                printf("[RX] %d bytes: ", len);
                usart1SendBytes(frameBuf, len);     /* DMA 发送回显 */
                printf("\r\n");
            }
        }

        os_delay_ms(5);
    }
}

/**
 * @brief   开始任务：上电自检后创建各任务并删除自身
 */
static void start_task(void *pvParameters)
{
    UNUSED(pvParameters);

    printf("\r\n===== Driver Verification =====\r\n");
    sram_self_test();
    malloc_self_test();
    demo_lcd();

    os_enter_critical();

    os_task_create(key_task, "key_task", KEY_TASK_STK_SIZE * sizeof(StackType_t),
                   NULL, KEY_TASK_PRIO, &s_keyTaskHandle);
    os_task_create(led_task, "led_task", LED_TASK_STK_SIZE * sizeof(StackType_t),
                   NULL, LED_TASK_PRIO, &s_ledTaskHandle);
    os_task_create(uart_task, "uart_task", UART_TASK_STK_SIZE * sizeof(StackType_t),
                   NULL, UART_TASK_PRIO, &s_uartTaskHandle);

    os_task_delete(s_startTaskHandle);
    os_exit_critical();
}

/**
 * @brief   FreeRTOS 例程入口（bsp_init 之后由 main 调用）
 */
void freertos_demo(void)
{
    os_task_create(start_task, "start_task", START_STK_SIZE * sizeof(StackType_t),
                   NULL, START_TASK_PRIO, &s_startTaskHandle);
    os_scheduler_start();
}
