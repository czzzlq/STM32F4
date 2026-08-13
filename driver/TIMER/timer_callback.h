/**
  ******************************************************************************
  * @file       timer_callback.h
  * @author     czzzlq
  * @version    V1.0
  * @date       2025-12-26
  * @brief      定时器回调管理器 - 解耦定时器ISR与驱动
  * @attention  仅供学习使用
  ******************************************************************************
**/
#ifndef __TIMER_CALLBACK_H__
#define __TIMER_CALLBACK_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "common_types.h"
#include <stdbool.h>

/*======================= Configuration =======================*/
#define TIMER_CALLBACK_MAX_COUNT    8   /* 最大回调数量 */

/*======================= Types =======================*/
typedef void (*timer_callback_func_t)(void);

typedef enum {
    TIMER_ID_TIM3 = 0,
    TIMER_ID_TIM5,
    TIMER_ID_MAX
} timer_id_t;

/*======================= Functions =======================*/
/**
 * @brief 初始化定时器回调管理器
 */
void timer_callback_init(void);

/**
 * @brief 注册定时器回调函数
 * @param timer_id 定时器ID
 * @param callback 回调函数
 * @return status_t
 */
status_t timer_callback_register(timer_id_t timer_id, timer_callback_func_t callback);

/**
 * @brief 注销定时器回调函数
 * @param timer_id 定时器ID
 * @param callback 回调函数
 * @return status_t
 */
status_t timer_callback_unregister(timer_id_t timer_id, timer_callback_func_t callback);

/**
 * @brief 执行指定定时器的所有回调(在ISR中调用)
 * @param timer_id 定时器ID
 */
void timer_callback_execute(timer_id_t timer_id);

#ifdef __cplusplus
}
#endif

#endif /* __TIMER_CALLBACK_H__ */
