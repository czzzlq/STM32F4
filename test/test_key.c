/**
 * @file    test_key.c
 * @brief   按键状态机主机侧回归测试（mingw gcc 编译，PC 上运行）
 * @note    通过公开接口 drv_key_scan_isr() + drv_key_get_event() 黑盒驱动，
 *          用可控 tick（g_test_tick）与 GPIO IDR 模拟按键时序，覆盖：
 *          短按 / 消抖（<10ms）/ 长按（>=3000ms）/ 连发（500ms 周期）
 * @note    用法：test/run_host_tests.sh
 */
#include <stdio.h>
#include "drv_key.h"
#include "bsp_gpio.h"

extern volatile drv_key_ctrl_block_t drv_key_ctrl_block[DRV_KEY_ID_MAX];
extern uint32_t g_test_tick;

#define KEY0_PIN    BSP_KEY0_PIN

/* 低有效按键：IDR 对应位清 0 为按下，置 1 为释放 */
#define KEY0_PRESS()    do{ g_test_key0_port.IDR &= ~KEY0_PIN; }while(0)
#define KEY0_RELEASE()  do{ g_test_key0_port.IDR |=  KEY0_PIN; }while(0)

static void key_reset(void)
{
    uint8_t i;

    for (i = 0; i < DRV_KEY_ID_MAX; i++)
    {
        drv_key_ctrl_block[i].press_time = 0;
        drv_key_ctrl_block[i].key_state = 0;
        drv_key_ctrl_block[i].key_pending = 0;
        drv_key_ctrl_block[i].long_press_trigger = 0;
        drv_key_ctrl_block[i].repeat_time = 0;
        /* gpio_port / gpio_pin / active_level 由 drv_key.c 静态初始化，保持不变 */
    }

    g_test_tick = 0;
    g_test_key0_port.IDR = 0xFFFFFFFFu;     /* 低有效：全 1 = 释放 */
    g_test_key1_port.IDR = 0xFFFFFFFFu;
    g_test_wkup_port.IDR = 0x00000000u;     /* 高有效：0 = 释放 */
}

/* 短按：按下 -> 保持 -> 释放，应产生一次 PRESS */
static int test_short_press(void)
{
    key_reset();
    drv_key_scan_isr();
    if (drv_key_get_event(DRV_KEY_ID_0) != DRV_KEY_EVENT_NONE) return 1;

    KEY0_PRESS();
    g_test_tick = 0;
    drv_key_scan_isr();                              /* 按下沿 */
    if (drv_key_get_event(DRV_KEY_ID_0) != DRV_KEY_EVENT_NONE) return 2;

    g_test_tick = 50;                                /* 保持 50ms */
    drv_key_scan_isr();
    if (drv_key_get_event(DRV_KEY_ID_0) != DRV_KEY_EVENT_NONE) return 3;

    KEY0_RELEASE();
    drv_key_scan_isr();                              /* 释放沿 -> 短按 */
    if (drv_key_get_event(DRV_KEY_ID_0) != DRV_KEY_EVENT_PRESS) return 4;

    /* 读后清空：再次获取应为 NONE */
    if (drv_key_get_event(DRV_KEY_ID_0) != DRV_KEY_EVENT_NONE) return 5;

    return 0;
}

/* 消抖：按下 <10ms 释放，不应产生短按 */
static int test_debounce(void)
{
    key_reset();
    drv_key_scan_isr();

    KEY0_PRESS();
    g_test_tick = 0;
    drv_key_scan_isr();

    g_test_tick = 5;                                 /* 5ms < 消抖阈值 10ms */
    KEY0_RELEASE();
    drv_key_scan_isr();

    return (drv_key_get_event(DRV_KEY_ID_0) == DRV_KEY_EVENT_NONE) ? 0 : 1;
}

/* 长按：>=3000ms 触发 LONG_PRESS_TRIGGER，释放后不再产生短按 */
static int test_long_press(void)
{
    key_reset();
    drv_key_scan_isr();

    KEY0_PRESS();
    g_test_tick = 0;
    drv_key_scan_isr();

    g_test_tick = 3100;
    drv_key_scan_isr();                              /* 触发长按 */
    if (drv_key_get_event(DRV_KEY_ID_0) != DRV_KEY_EVENT_LONG_PRESS_TRIGGER) return 1;

    KEY0_RELEASE();
    drv_key_scan_isr();                              /* 长按后释放：无短按 */
    if (drv_key_get_event(DRV_KEY_ID_0) != DRV_KEY_EVENT_NONE) return 2;

    return 0;
}

/* 连发：长按保持期间每 500ms 产生 REPEAT */
static int test_repeat(void)
{
    key_reset();
    drv_key_scan_isr();

    KEY0_PRESS();
    g_test_tick = 0;
    drv_key_scan_isr();

    g_test_tick = 3100;
    drv_key_scan_isr();                              /* 长按触发，repeat_time=3100 */
    (void)drv_key_get_event(DRV_KEY_ID_0);           /* 消费长按事件 */

    g_test_tick = 3600;                              /* +500ms */
    drv_key_scan_isr();
    if (drv_key_get_event(DRV_KEY_ID_0) != DRV_KEY_EVENT_REPEAT) return 1;

    g_test_tick = 4100;                              /* 再 +500ms */
    drv_key_scan_isr();
    if (drv_key_get_event(DRV_KEY_ID_0) != DRV_KEY_EVENT_REPEAT) return 2;

    g_test_tick = 4300;                              /* 仅 +200ms：不连发 */
    drv_key_scan_isr();
    if (drv_key_get_event(DRV_KEY_ID_0) != DRV_KEY_EVENT_NONE) return 3;

    return 0;
}

/* 事件不丢失：任务未及时取走时，多个事件应依次取出而不是被覆盖 */
static int test_event_queue(void)
{
    key_reset();
    drv_key_scan_isr();

    KEY0_PRESS();
    g_test_tick = 0;
    drv_key_scan_isr();

    g_test_tick = 3100;
    drv_key_scan_isr();                              /* 长按触发（不消费） */

    g_test_tick = 3600;
    drv_key_scan_isr();                              /* 连发（不消费，两个事件同时挂起） */

    if (drv_key_get_event(DRV_KEY_ID_0) != DRV_KEY_EVENT_LONG_PRESS_TRIGGER) return 1;
    if (drv_key_get_event(DRV_KEY_ID_0) != DRV_KEY_EVENT_REPEAT) return 2;
    if (drv_key_get_event(DRV_KEY_ID_0) != DRV_KEY_EVENT_NONE) return 3;

    return 0;
}

int main(void)
{
    int fail = 0;

    fail |= test_short_press();
    fail |= test_debounce();
    fail |= test_long_press();
    fail |= test_repeat();
    fail |= test_event_queue();

    if (fail)
    {
        printf("key: FAILED (code %d)\n", fail);
        return 1;
    }

    printf("key: ALL TESTS PASSED\n");
    return 0;
}
