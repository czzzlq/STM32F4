/**
  ******************************************************************************
  * @file       timer_callback.c
  * @author     czzzlq
  * @version    V1.0
  * @date       2025-12-26
  * @brief      定时器回调管理器实现
  * @attention  仅供学习使用
  ******************************************************************************
**/
#include "timer_callback.h"
#include <string.h>

/*======================= Private Types =======================*/
typedef struct {
    timer_callback_func_t callbacks[TIMER_CALLBACK_MAX_COUNT];
    uint8_t count;
} timer_callback_list_t;

/*======================= Private Variables =======================*/
static timer_callback_list_t s_callback_lists[TIMER_ID_MAX];

/*======================= Public Functions =======================*/
/**
 * @brief 初始化定时器回调管理器
 */
void timer_callback_init(void)
{
    memset(s_callback_lists, 0, sizeof(s_callback_lists));
}

/**
 * @brief 注册定时器回调函数
 * @param timer_id 定时器ID
 * @param callback 回调函数
 * @return status_t
 */
status_t timer_callback_register(timer_id_t timer_id, timer_callback_func_t callback)
{
    if (timer_id >= TIMER_ID_MAX || callback == NULL) {
        return STATUS_ERR_PARAM;
    }

    timer_callback_list_t* list = &s_callback_lists[timer_id];
    
    /* 检查是否已注册 */
    for (uint8_t i = 0; i < list->count; i++) {
        if (list->callbacks[i] == callback) {
            return STATUS_OK; /* 已注册,直接返回成功 */
        }
    }

    /* 检查是否已满 */
    if (list->count >= TIMER_CALLBACK_MAX_COUNT) {
        return STATUS_ERR_NO_MEM;
    }

    /* 注册回调 */
    list->callbacks[list->count] = callback;
    list->count++;

    return STATUS_OK;
}

/**
 * @brief 注销定时器回调函数
 * @param timer_id 定时器ID
 * @param callback 回调函数
 * @return status_t
 */
status_t timer_callback_unregister(timer_id_t timer_id, timer_callback_func_t callback)
{
    if (timer_id >= TIMER_ID_MAX || callback == NULL) {
        return STATUS_ERR_PARAM;
    }

    timer_callback_list_t* list = &s_callback_lists[timer_id];
    
    for (uint8_t i = 0; i < list->count; i++) {
        if (list->callbacks[i] == callback) {
            /* 移除回调,将后面的前移 */
            for (uint8_t j = i; j < list->count - 1; j++) {
                list->callbacks[j] = list->callbacks[j + 1];
            }
            list->count--;
            return STATUS_OK;
        }
    }

    return STATUS_ERR; /* 未找到 */
}

/**
 * @brief 执行指定定时器的所有回调(在ISR中调用)
 * @param timer_id 定时器ID
 */
void timer_callback_execute(timer_id_t timer_id)
{
    if (timer_id >= TIMER_ID_MAX) {
        return;
    }

    timer_callback_list_t* list = &s_callback_lists[timer_id];
    
    for (uint8_t i = 0; i < list->count; i++) {
        if (list->callbacks[i] != NULL) {
            list->callbacks[i]();
        }
    }
}
