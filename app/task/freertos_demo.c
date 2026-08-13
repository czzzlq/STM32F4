/**
 * @file    freertos_demo.c
 * @brief   重写驱动后的烧录验证程序
 * @note    验证内容：
 *          1. 串口环形缓冲区 + DMA：PC 发送任意数据帧，板子原样回显
 *          2. 外部 SRAM：探测到芯片才跑自检（精英板等无外部 SRAM 的板子自动跳过），
 *             覆盖 32 位/16 位/字节三种拷贝路径
 *          3. 内存池（malloc）：内部池自检；外部池仅在探测到 SRAM 时参与
 *          4. LCD：按实际分辨率自适应布局（支持 240x320 / 480x800 等任意面板）
 *          5. KEY：KEY0 短按翻转 LED0；KEY1 短按重跑自检；WKUP 长按重绘 LCD
 *          6. LED：LED1 500ms 闪烁
 */
#include "freertos_demo.h"
#include "drv_adc.h"
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

/* ==================== 自检用静态缓冲 ==================== */
/* 测试区避开 malloc 外部内存池区（0x68000000 ~ 0x68000022），从 0x1000 开始 */
#define SRAM_TEST_OFFSET    0x1000
#define SRAM_TEST_SIZE      1024

static uint8_t s_sramTestBuf[SRAM_TEST_SIZE];       /* 写入数据 */
static uint8_t s_sramTestCheck[SRAM_TEST_SIZE];     /* 回读校验数据 */

/**
 * @brief   探测外部 SRAM 是否存在
 * @retval  1 存在；0 不存在（如精英板未焊接 SRAM 芯片）
 * @note    探测方法：连续写入 8 个互不相同的值后再整体读回比对。
 *          无芯片时总线行为不可信（可能浮空返回固定电平，或返回
 *          最后一次写入的值），单字节写读探测会假阳性，多值连续
 *          探测可保证任一异常总线行为都会比对失败
 */
static uint8_t sram_probe(void)
{
    static const uint8_t probePattern[8] = {0x00, 0xFF, 0x55, 0xAA, 0x33, 0xCC, 0xA5, 0x5A};
    uint8_t probeCheck[8];

    sram_write((uint8_t *)probePattern, SRAM_TEST_OFFSET, sizeof(probePattern));
    memset(probeCheck, 0, sizeof(probeCheck));
    sram_read(probeCheck, SRAM_TEST_OFFSET, sizeof(probePattern));

    return (memcmp(probePattern, probeCheck, sizeof(probePattern)) == 0) ? 1 : 0;
}

/**
 * @brief   外部 SRAM 读写自检（无芯片时跳过）
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

    if (!sram_probe())
    {
        printf("[SRAM] not detected, self test skipped\r\n");
        return;
    }

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
 * @note    验证按字扫描的首次适配分配器与释放逻辑；
 *          外部池仅在探测到 SRAM 芯片时参与
 */
static void malloc_self_test(void)
{
    uint8_t *p1, *p2, *p3 = NULL;
    uint32_t errCount = 0;
    uint16_t i;
    uint8_t hasSramEx = sram_probe();

    p1 = (uint8_t *)mymalloc(SRAMIN, 512);      /* 内部 SRAM：512 字节（16 块） */
    p2 = (uint8_t *)mymalloc(SRAMIN, 64);       /* 内部 SRAM：64 字节（2 块） */

    printf("[MALLOC] p1=0x%x p2=0x%x", (unsigned int)p1, (unsigned int)p2);

    if (hasSramEx)
    {
        p3 = (uint8_t *)mymalloc(SRAMEX, 16);   /* 外部 SRAM：16 字节（1 块，池共 32 字节） */
        printf(" p3=0x%x\r\n", (unsigned int)p3);
    }
    else
    {
        printf(" (SRAMEX: not detected, skipped)\r\n");
    }

    if (p1 == NULL || p2 == NULL || (hasSramEx && p3 == NULL))
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

    if (hasSramEx)
    {
        for (i = 0; i < 16; i++) p3[i] = 0xA5;
        for (i = 0; i < 16; i++)
        {
            if (p3[i] != 0xA5) errCount++;
        }
    }

    /* 释放后使用率应归零 */
    myfree(SRAMIN, p1);
    myfree(SRAMIN, p2);
    if (hasSramEx)
    {
        myfree(SRAMEX, p3);
    }

    if (my_mem_perused(SRAMIN) != 0 || (hasSramEx && my_mem_perused(SRAMEX) != 0))
    {
        errCount++;
    }

    /* 越界检测演示：破坏分配块头 magic，释放应被拒绝并计数 */
    {
        uint8_t *pCorrupt = (uint8_t *)mymalloc(SRAMIN, 32);
        uint32_t corruptBefore = g_mallocCorruptCount;

        if (pCorrupt != NULL)
        {
            *(uint32_t *)(pCorrupt - 4) = 0;        /* 模拟越界写破坏块头 */
            myfree(SRAMIN, pCorrupt);               /* 应被拒绝释放 */

            if (g_mallocCorruptCount != corruptBefore + 1)
            {
                errCount++;
            }
        }
        else
        {
            errCount++;
        }

        printf("[MALLOC] corrupt detect: %s\r\n",
               (g_mallocCorruptCount > corruptBefore) ? "PASS" : "FAIL");

        my_mem_init(SRAMIN);    /* 重置内部池（被破坏的块不再归还） */
    }

    printf("[MALLOC] self test: %s (%d errors, used IN=%d%%",
           (errCount == 0) ? "PASS" : "FAIL", errCount,
           my_mem_perused(SRAMIN) / 10);

    if (hasSramEx)
    {
        printf(" EX=%d%%", my_mem_perused(SRAMEX) / 10);
    }
    printf(")\r\n");
}

/**
 * @brief   ADC 自检：内部温度传感器 + Vrefint 反推电源电压
 */
static void adc_self_test(void)
{
    int16_t temp = drv_adc_get_temp_tenth();
    uint16_t vdda = drv_adc_get_vdda_mv();

    if (temp < 0)
    {
        printf("[ADC] temp=-%d.%dC ", (int)(-temp) / 10, (int)(-temp) % 10);
    }
    else
    {
        printf("[ADC] temp=%d.%dC ", (int)temp / 10, (int)temp % 10);
    }
    printf("VDDA=%d.%02dV\r\n", vdda / 1000, (vdda % 1000) / 10);
}

/**
 * @brief   LCD 绘图演示（按实际分辨率自适应布局，覆盖重写后的各绘图算法）
 */
static void demo_lcd(void)
{
    uint16_t w = lcddev.width;
    uint16_t h = lcddev.height;

    lcd_clear(WHITE);
    lcd_show_string(w / 8, h / 32, w * 3 / 4, h / 16, 16, "Driver Verify", RED);

    /* 窗口填充：单次设窗口连续写 RAM */
    lcd_fill(w / 16,          h / 8,  w * 5 / 16,  h / 2, BLUE);
    lcd_fill(w * 6 / 16,      h / 8,  w * 10 / 16, h / 2, GREEN);
    lcd_fill(w * 11 / 16,     h / 8,  w * 15 / 16, h / 2, RED);

    /* 画线：水平（段批量化退化单段）/ 竖直（单列填充快路径）/ 斜线 */
    lcd_draw_line(w / 16,         h * 9 / 16, w * 15 / 16, h * 9 / 16, BLACK);
    lcd_draw_line(w / 2,          h * 17 / 32, w / 2,      h - 2,      BLACK);
    lcd_draw_line(w / 16,         h * 17 / 32, w * 15 / 16, h - 2,      GRAY);

    /* 矩形 / 圆（轮廓）/ 实心圆 */
    lcd_draw_rectangle(w / 12,       h * 19 / 32, w / 6,        h * 25 / 32, BROWN);
    lcd_draw_circle(w * 12 / 16,     h * 25 / 32, h / 14, MAGENTA);
    lcd_fill_circle(w * 9 / 16,      h * 22 / 32, h / 16, YELLOW);

    /* 数字显示：10 次幂查表 */
    lcd_show_num(w / 12,     h * 27 / 32, 12345, 5, 16, BLACK);
    lcd_show_xnum(w * 6 / 16, h * 27 / 32, 9876, 8, 16, 0x80, BLACK);
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
        drv_key_event_t key0Event = drv_key_get_event(DRV_KEY_ID_0);

        if (key0Event == DRV_KEY_EVENT_PRESS)
        {
            drv_led_toggle(DRV_LED_ID_0);
            printf("[KEY] KEY0 press, LED0 toggle\r\n");
        }
        else if (key0Event == DRV_KEY_EVENT_REPEAT)
        {
            drv_led_toggle(DRV_LED_ID_0);       /* 长按连发：LED0 以 500ms 节奏翻转 */
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
 * @note    回显路径：阻塞等帧信号量（usart1WaitFrame）-> usart1ReadBytes
 *          -> usart1SendBytes(DMA 发送)；任务不轮询，帧到达立即响应
 */
static void uart_task(void *pvParameters)
{
    UNUSED(pvParameters);
    static uint8_t frameBuf[USART1_RX_DMA_BUF_SIZE];

    printf("UART echo ready (ring buffer + DMA)\r\n");

    while (1)
    {
        uint16_t len = usart1WaitFrame(0xFFFFFFFF);     /* 阻塞等帧（IDLE 中断唤醒） */

        if (len > 0)
        {
            len = usart1ReadBytes(frameBuf, sizeof(frameBuf));
            printf("[RX] %d bytes: ", len);
            usart1SendBytes(frameBuf, len);     /* DMA 发送回显 */
            printf("\r\n");
        }
    }
}

/**
 * @brief   开始任务：上电自检后创建各任务并删除自身
 */
static void start_task(void *pvParameters)
{
    UNUSED(pvParameters);

    printf("\r\n===== Driver Verification =====\r\n");
    printf("[LCD] %dx%d ID=0x%x\r\n", lcddev.width, lcddev.height, lcddev.id);
    sram_self_test();
    malloc_self_test();
    adc_self_test();
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
