/**
 * @file    freertos_demo.c
 * @brief   重写驱动后的烧录验证程序
 * @note    验证内容：
 *          1. 串口环形缓冲区 + DMA：PC 发送任意数据帧，板子原样回显
 *          2. 外部 SRAM：探测到芯片才跑自检（未焊外部 SRAM 的板卡自动跳过），
 *             覆盖 32 位/16 位/字节三种拷贝路径
 *          3. 内存池（malloc）：内部池自检；外部池仅在探测到 SRAM 时参与
 *          4. LCD：按实际分辨率自适应布局（支持 240x320 / 480x800 等任意面板）
 *          5. KEY：KEY0 短按翻转 LED0；KEY1 短按重跑自检；WKUP 长按重绘 LCD
 *          6. LED：LED1 500ms 闪烁
 *          7. LVGL：屏上验证界面（见 lv_mainstart.c），触摸与控件事件联动
 */
#include "freertos_demo.h"
#include "lvgl.h"
#include "lv_port_disp_template.h"
#include "lv_port_indev_template.h"
#include "lv_mainstart.h"
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

/* LVGL 任务：优先级与按键任务同级（渲染很重，不宜再高）；
 * 栈要给足——控件创建、字库解码与渲染的调用深度远大于其它任务，
 * 1024 字（4KB）是 LVGL 官方示例的常用量级 */
#define LVGL_TASK_PRIO      2
#define LVGL_TASK_STK_SIZE  1024

/* my_mem_perused 返回 0~1000（0.1% 精度），除以该系数得到整百分比 */
#define PERUSED_TO_PERCENT  10

static os_task_handle_t s_startTaskHandle;
static os_task_handle_t s_keyTaskHandle;
static os_task_handle_t s_ledTaskHandle;
static os_task_handle_t s_uartTaskHandle;
static os_task_handle_t s_lvglTaskHandle;

/* ==================== 自检用静态缓冲 ==================== */
/* 外部 SRAM 的整片 1MB 都由 malloc 外部池托管（池 960KB + 状态表 60KB），
 * 所以原始读写自检不能再用固定物理偏移乱写，改为从池里临时借一块、用完归还。
 * 块内仍用 SRAM_TEST_OFFSET 起的偏移，保持原来的对齐路径覆盖不变。 */
#define SRAM_TEST_OFFSET    0x1000
#define SRAM_TEST_SIZE      1024
#define SRAM_TEST_BLOCK     0x2000      /* 借用的块大小（含 +2048 单字节用例的余量） */

static uint8_t s_sramTestBuf[SRAM_TEST_SIZE];       /* 写入数据 */
static uint8_t s_sramTestCheck[SRAM_TEST_SIZE];     /* 回读校验数据 */
static uint32_t s_sramTestOff;                      /* 本次自检块相对 SRAM_BASE_ADDR 的偏移 */

/**
 * @brief   从外部池借一块原始读写测试区
 * @retval  1 成功；0 无空闲块
 */
static uint8_t sram_test_block_acquire(void)
{
    uint8_t *blk = (uint8_t *)mymalloc(SRAMEX, SRAM_TEST_BLOCK);

    if (blk == NULL)
    {
        return 0;
    }

    s_sramTestOff = (uint32_t)(blk - (uint8_t *)SRAM_BASE_ADDR);
    return 1;
}

/**
 * @brief   归还原始读写测试区
 */
static void sram_test_block_release(void)
{
    myfree(SRAMEX, (void *)((uint8_t *)SRAM_BASE_ADDR + s_sramTestOff));
}

/**
 * @brief   探测外部 SRAM 是否存在
 * @retval  1 存在；0 不存在（如未焊接 SRAM 芯片的板卡）
 * @note    探测方法：连续写入 8 个互不相同的值后再整体读回比对。
 *          无芯片时总线行为不可信（可能浮空返回固定电平，或返回
 *          最后一次写入的值），单字节写读探测会假阳性，多值连续
 *          探测可保证任一异常总线行为都会比对失败。
 *          探测区从外部池临时借来（池在 bsp_init 中已初始化）；无芯片时
 *          分配器读到的状态表是浮空数据，最坏只会给出一个越界受限于池范围
 *          的伪指针或返回 NULL，两种情况都会让比对失败，判定依然正确
 */
static uint8_t sram_probe(void)
{
    static const uint8_t probePattern[8] = {0x00, 0xFF, 0x55, 0xAA, 0x33, 0xCC, 0xA5, 0x5A};
    uint8_t probeCheck[8];
    uint8_t *blk;
    uint32_t off;
    uint8_t result;

    blk = (uint8_t *)mymalloc(SRAMEX, 32);
    if (blk == NULL)
    {
        return 0;
    }

    off = (uint32_t)(blk - (uint8_t *)SRAM_BASE_ADDR);

    sram_write((uint8_t *)probePattern, off, sizeof(probePattern));
    memset(probeCheck, 0, sizeof(probeCheck));
    sram_read(probeCheck, off, sizeof(probePattern));

    result = (memcmp(probePattern, probeCheck, sizeof(probePattern)) == 0) ? 1 : 0;

    myfree(SRAMEX, blk);

    return result;
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

    /* 从外部池借一块作为原始读写区（整片 SRAM 已由池托管，不能再按固定物理地址乱写） */
    if (!sram_test_block_acquire())
    {
        printf("[SRAM] no free pool block, self test skipped\r\n");
        return;
    }

    /* 生成测试模式（全字节遍历） */
    for (i = 0; i < SRAM_TEST_SIZE; i++)
    {
        s_sramTestBuf[i] = (uint8_t)i;
    }

    /* A. 双 4 字节对齐 + 1024 字节（32 位拷贝路径） */
    sram_write(s_sramTestBuf, s_sramTestOff + SRAM_TEST_OFFSET, 1024);
    memset(s_sramTestCheck, 0, SRAM_TEST_SIZE);
    sram_read(s_sramTestCheck, s_sramTestOff + SRAM_TEST_OFFSET, 1024);
    if (memcmp(s_sramTestBuf, s_sramTestCheck, 1024) != 0)
    {
        errCount++;
    }

    /* B. 奇地址 + 1013 字节（头部字节 + 16 位路径） */
    sram_write(s_sramTestBuf, s_sramTestOff + SRAM_TEST_OFFSET + 1, 1013);
    memset(s_sramTestCheck, 0, SRAM_TEST_SIZE);
    sram_read(s_sramTestCheck, s_sramTestOff + SRAM_TEST_OFFSET + 1, 1013);
    if (memcmp(s_sramTestBuf, s_sramTestCheck, 1013) != 0)
    {
        errCount++;
    }

    /* C. 偶地址 + 1023 字节（16 位路径 + 尾部字节） */
    sram_write(s_sramTestBuf, s_sramTestOff + SRAM_TEST_OFFSET, 1023);
    memset(s_sramTestCheck, 0, SRAM_TEST_SIZE);
    sram_read(s_sramTestCheck, s_sramTestOff + SRAM_TEST_OFFSET, 1023);
    if (memcmp(s_sramTestBuf, s_sramTestCheck, 1023) != 0)
    {
        errCount++;
    }

    /* D. 单字节接口 */
    sram_test_write(s_sramTestOff + SRAM_TEST_OFFSET + 2048, 0x5A);
    if (sram_test_read(s_sramTestOff + SRAM_TEST_OFFSET + 2048) != 0x5A)
    {
        errCount++;
    }

    sram_test_block_release();

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
        p3 = (uint8_t *)mymalloc(SRAMEX, 512 * 1024);   /* 外部 SRAM：512KB（池共 960KB） */
        printf(" p3=0x%x\r\n", (unsigned int)p3);
    }
    else
    {
        printf(" (SRAMEX: not detected, skipped)\r\n");
    }

    if (p1 == NULL || p2 == NULL || (hasSramEx && p3 == NULL))
    {
        printf("[MALLOC] self test: FAIL (alloc failed)\r\n");

        /* 已成功的分配要归还：否则这次失败会永久占住池里的块，
         * 后续重跑自检（KEY1）会因内碎片累积而越来越容易失败 */
        if (p1 != NULL)
        {
            myfree(SRAMIN, p1);
        }

        if (p2 != NULL)
        {
            myfree(SRAMIN, p2);
        }

        if (p3 != NULL)
        {
            myfree(SRAMEX, p3);
        }

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
        /* 大块分配后按首/中/尾抽样读写：一并验证池容量、地址线 A0~A18 与 FSMC 时序 */
        static const uint32_t sampleOffs[4] = {0, 1024, 256 * 1024, 512 * 1024 - 16};

        for (uint32_t k = 0; k < 4; k++)
        {
            for (i = 0; i < 16; i++)
            {
                p3[sampleOffs[k] + i] = (uint8_t)(0xA5 + i);
            }
        }

        for (uint32_t k = 0; k < 4; k++)
        {
            for (i = 0; i < 16; i++)
            {
                if (p3[sampleOffs[k] + i] != (uint8_t)(0xA5 + i))
                {
                    errCount++;
                }
            }
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
           my_mem_perused(SRAMIN) / PERUSED_TO_PERCENT);

    if (hasSramEx)
    {
        printf(" EX=%d%%", my_mem_perused(SRAMEX) / PERUSED_TO_PERCENT);
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
 * @brief   覆盖 LVGL 界面层的弱钩子：把界面上的开关状态映射到 LED0
 * @param   on : 界面开关当前状态
 * @note    这是「界面层不碰驱动」约定的落地点：lv_mainstart.c 只声明钩子，
 *          具体点灯动作在本层实现，跨层依赖被限制在 app 内部
 */
void lv_ui_led_switch_hook(bool on)
{
    if (on)
    {
        drv_led_on(DRV_LED_ID_0);
    }
    else
    {
        drv_led_off(DRV_LED_ID_0);
    }
}

/**
 * @brief   LVGL 任务：初始化图形库与移植层，之后周期驱动 LVGL 定时器
 * @note    1. 调用顺序不能颠倒：lv_init() 建内核 -> lv_port_disp_init() 注册显示
 *              -> lv_port_indev_init() 注册触摸 -> lv_mainstart() 才能建控件
 *          2. lv_port_disp_init() 会从外部 SRAM 池申请整屏绘图缓冲
 *              (800x480x2 = 750KB)。本任务因此必须排在上电自检之后创建：
 *              自检里的 512KB 大块分配在 start_task 返回前已经归还
 *          3. 只有本任务访问 LVGL，所以不需要额外的互斥；将来若要从其它任务
 *              改界面，必须用 lv_async_call 把动作投递到本任务
 *          4. lv_timer_handler() 的返回值是「距离下次该被调用还有多少 ms」，
 *              按它休眠而不是固定轮询，可以把 CPU 让给其它任务
 */
static void lvgl_task(void *pvParameters)
{
    UNUSED(pvParameters);

    lv_init();
    lv_port_disp_init();        /* 内部按需 lcd_init + 设横屏，并分配绘图缓冲 */
    lv_port_indev_init();       /* 内部调用 tp_init() 识别触摸 IC */

    printf("[LVGL] %dx%d disp+indev ready\r\n",
           (int)lv_disp_get_hor_res(NULL), (int)lv_disp_get_ver_res(NULL));

    lv_mainstart();             /* 构建验证界面 */
    printf("[LVGL] UI ready\r\n");

    while (1)
    {
        uint32_t delayMs = lv_timer_handler();

        /* 夹到 1~20ms：太小会空转，太大会拖慢触摸与动画的响应 */
        if (delayMs > 20U)
        {
            delayMs = 20U;
        }
        else if (delayMs < 1U)
        {
            delayMs = 1U;
        }

        os_delay_ms(delayMs);
    }
}

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
 * @note    删除自身必须放在临界区之外：os_exit_critical() 恢复 BASEPRI 后
 *          PendSV 可能立即切换，空闲任务随即回收本任务的 TCB 与栈，
 *          而函数返回还要用这段栈（原实现把 delete 放在临界区里，
 *          临界区结束后的返回路径就跑在已释放的栈上）
 */
static void start_task(void *pvParameters)
{
    bool created = true;

    UNUSED(pvParameters);

    printf("\r\n===== Driver Verification =====\r\n");
    printf("[LCD] %dx%d ID=0x%x\r\n", lcddev.width, lcddev.height, lcddev.id);
    sram_self_test();
    malloc_self_test();
    adc_self_test();
    demo_lcd();

    /* 临界区内创建，保证三个任务一起就绪（避免高优先级任务先跑起来时
     * 低优先级任务还没被创建而出现奇怪的时序） */
    os_enter_critical();

    if (!os_task_create(key_task, "key_task", KEY_TASK_STK_SIZE * sizeof(StackType_t),
                        NULL, KEY_TASK_PRIO, &s_keyTaskHandle))
    {
        created = false;
    }

    if (!os_task_create(led_task, "led_task", LED_TASK_STK_SIZE * sizeof(StackType_t),
                        NULL, LED_TASK_PRIO, &s_ledTaskHandle))
    {
        created = false;
    }

    if (!os_task_create(uart_task, "uart_task", UART_TASK_STK_SIZE * sizeof(StackType_t),
                        NULL, UART_TASK_PRIO, &s_uartTaskHandle))
    {
        created = false;
    }

    /* LVGL 任务最后创建：此时自检占用的大块外部 SRAM 已归还 */
    if (!os_task_create(lvgl_task, "lvgl_task", LVGL_TASK_STK_SIZE * sizeof(StackType_t),
                        NULL, LVGL_TASK_PRIO, &s_lvglTaskHandle))
    {
        created = false;
    }

    os_exit_critical();

    if (!created)
    {
        printf("[OS] task create failed (heap exhausted?)\r\n");
    }

    os_task_delete(s_startTaskHandle);
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
