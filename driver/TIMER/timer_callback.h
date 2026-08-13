/**
 * @file    timer_callback.h
 * @brief   定时器回调管理器 - 解耦定时器 ISR 与各驱动模块
 */
#ifndef __TIMER_CALLBACK_H__
#define __TIMER_CALLBACK_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "common_types.h"
#include <stdbool.h>

/* 每个定时器最多注册的回调数量 */
#define TIMER_CALLBACK_MAX_COUNT    8

typedef void (*timer_callback_func_t)(void);

/* 定时器 ID */
typedef enum
{
    TIMER_ID_TIM3 = 0,
    TIMER_ID_TIM5,
    TIMER_ID_MAX
} timer_id_t;

/* 初始化回调管理器 */
void timer_callback_init(void);
/* 注册回调（重复注册同一回调返回 STATUS_OK） */
status_t timer_callback_register(timer_id_t timer_id, timer_callback_func_t callback);
/* 注销回调 */
status_t timer_callback_unregister(timer_id_t timer_id, timer_callback_func_t callback);
/* 执行指定定时器的全部回调（在 ISR 中调用） */
void timer_callback_execute(timer_id_t timer_id);

#ifdef __cplusplus
}
#endif

#endif /* __TIMER_CALLBACK_H__ */
