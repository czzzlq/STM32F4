/**
 * @file    timer_callback.c
 * @brief   定时器回调管理器实现
 */
#include "timer_callback.h"
#include <string.h>

/* 回调列表 */
typedef struct
{
    timer_callback_func_t callbacks[TIMER_CALLBACK_MAX_COUNT];
    uint8_t count;
} timer_callback_list_t;

static timer_callback_list_t s_callback_lists[TIMER_ID_MAX];

/**
 * @brief   初始化回调管理器
 */
void timer_callback_init(void)
{
    memset(s_callback_lists, 0, sizeof(s_callback_lists));
}

/**
 * @brief   注册定时器回调
 * @param   timer_id : 定时器 ID
 * @param   callback : 回调函数
 * @retval  STATUS_OK / STATUS_ERR_PARAM / STATUS_ERR_NO_MEM
 */
status_t timer_callback_register(timer_id_t timer_id, timer_callback_func_t callback)
{
    timer_callback_list_t* list;

    if (timer_id >= TIMER_ID_MAX || callback == NULL)
    {
        return STATUS_ERR_PARAM;
    }

    list = &s_callback_lists[timer_id];

    for (uint8_t i = 0; i < list->count; i++)
    {
        if (list->callbacks[i] == callback)
        {
            return STATUS_OK;           /* 已注册 */
        }
    }

    if (list->count >= TIMER_CALLBACK_MAX_COUNT)
    {
        return STATUS_ERR_NO_MEM;       /* 已满 */
    }

    list->callbacks[list->count] = callback;
    list->count++;

    return STATUS_OK;
}

/**
 * @brief   注销定时器回调
 * @param   timer_id : 定时器 ID
 * @param   callback : 回调函数
 * @retval  STATUS_OK / STATUS_ERR_PARAM / STATUS_ERR
 */
status_t timer_callback_unregister(timer_id_t timer_id, timer_callback_func_t callback)
{
    timer_callback_list_t* list;

    if (timer_id >= TIMER_ID_MAX || callback == NULL)
    {
        return STATUS_ERR_PARAM;
    }

    list = &s_callback_lists[timer_id];

    for (uint8_t i = 0; i < list->count; i++)
    {
        if (list->callbacks[i] == callback)
        {
            /* 移除并将后续回调前移 */
            for (uint8_t j = i; j < list->count - 1; j++)
            {
                list->callbacks[j] = list->callbacks[j + 1];
            }
            list->count--;
            return STATUS_OK;
        }
    }

    return STATUS_ERR;      /* 未找到 */
}

/**
 * @brief   执行指定定时器的全部回调（ISR 中调用）
 * @param   timer_id : 定时器 ID
 */
void timer_callback_execute(timer_id_t timer_id)
{
    timer_callback_list_t* list;

    if (timer_id >= TIMER_ID_MAX)
    {
        return;
    }

    list = &s_callback_lists[timer_id];

    for (uint8_t i = 0; i < list->count; i++)
    {
        if (list->callbacks[i] != NULL)
        {
            list->callbacks[i]();
        }
    }
}
