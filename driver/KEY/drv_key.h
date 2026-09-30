/**
 * @file    drv_key.h
 * @brief   按键驱动接口
 * @note    WKUP 即 KEY_UP（接 PA0），除普通输入外还可作为 STM32 唤醒输入
 */
#ifndef __DRV_KEY_H__
#define __DRV_KEY_H__

#include "common_types.h"
#include "stm32f4xx_hal.h"

/* 按键 ID */
typedef enum
{
    DRV_KEY_ID_0 = 0,   /* KEY0 */
    DRV_KEY_ID_1,       /* KEY1 */
    DRV_KEY_ID_WKUP,    /* WKUP */
    DRV_KEY_ID_MAX      /* 按键数量上限（占位） */
} drv_key_id_t;

/* 按键事件 */
typedef enum
{
    DRV_KEY_EVENT_NONE = 0,             /* 无事件 */
    DRV_KEY_EVENT_PRESS,                /* 短按事件（释放时产生） */
    DRV_KEY_EVENT_RELEASE,              /* 释放事件 */
    DRV_KEY_EVENT_LONG_PRESS_TRIGGER,   /* 长按事件（按下持续到阈值时产生） */
    DRV_KEY_EVENT_REPEAT                /* 连发事件（长按保持期间周期性产生） */
} drv_key_event_t;

/* 按键控制块 */
typedef struct
{
    GPIO_TypeDef* gpio_port;            /* GPIO 端口 */
    uint16_t    gpio_pin;               /* GPIO 引脚 */
    uint32_t    press_time;             /* 按下时刻（tick，用于长按判断） */
    uint8_t     key_state;              /* 当前状态：0 释放，1 按下 */
    uint8_t     key_pending;            /* 待处理事件位图：bit N 对应 drv_key_event_t N
                                         * （用位图而非单槽，避免两次事件间隔内
                                         *  后一个事件把前一个覆盖掉而丢失） */
    uint8_t     active_level;           /* 有效电平 */
    uint8_t     long_press_trigger;     /* 长按已触发标志 */
    uint32_t    repeat_time;            /* 上次连发时刻（tick） */
} drv_key_ctrl_block_t;

/* 按键驱动初始化 */
status_t drv_key_init(void);
/* 按键扫描（在定时器中断中调用） */
void drv_key_scan_isr(void);
/* 获取按键事件（每次取出一个待处理事件，无事件返回 DRV_KEY_EVENT_NONE，线程安全） */
drv_key_event_t drv_key_get_event(drv_key_id_t id);
/* 查询按键当前是否被按下（电平状态，线程安全） */
bool drv_key_is_pressed(drv_key_id_t id);

#endif /* __DRV_KEY_H__ */
