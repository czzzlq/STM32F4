/**
 * @file    drv_led.h
 * @brief   LED 驱动接口
 */
#ifndef __DRV_LED_H__
#define __DRV_LED_H__

#include "common_types.h"

/* LED 逻辑 ID */
typedef enum
{
    DRV_LED_ID_0 = 0,   /* LED0 */
    DRV_LED_ID_1,       /* LED1 */
    DRV_LED_ID_MAX      /* LED 数量上限（占位） */
} led_id_t;

/* LED 状态定义 */
typedef enum
{
    DRV_LED_STATE_OFF = 0,  /* 关闭 */
    DRV_LED_STATE_ON,       /* 点亮 */
    DRV_LED_STATE_TOGGLE    /* 翻转 */
} led_state_t;

/* LED 驱动初始化 */
status_t drv_led_init(void);
/* 设置指定 LED 状态 */
status_t drv_led_set_state(led_id_t id, led_state_t state);

/* 便捷函数 */
static inline status_t drv_led_on(led_id_t id)
{
    return drv_led_set_state(id, DRV_LED_STATE_ON);
}

static inline status_t drv_led_off(led_id_t id)
{
    return drv_led_set_state(id, DRV_LED_STATE_OFF);
}

static inline status_t drv_led_toggle(led_id_t id)
{
    return drv_led_set_state(id, DRV_LED_STATE_TOGGLE);
}

#endif /* __DRV_LED_H__ */
