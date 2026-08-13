/**
  ******************************************************************************
  * @file       os_wrapper.h
  * @author     czzzlq
  * @version    V1.0
  * @date       2025-12-26
  * @brief      OS抽象层头文件 - 提供RTOS无关的接口
  * @attention  仅供学习使用
  * @note       支持FreeRTOS/裸机切换
  ******************************************************************************
**/
#ifndef __OS_WRAPPER_H__
#define __OS_WRAPPER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/*======================= OS Configuration =======================*/
#define OS_USE_FREERTOS     1   /* 1: 使用FreeRTOS, 0: 裸机模式 */

/*======================= Critical Section =======================*/
/**
 * @brief 进入临界区
 */
void os_enter_critical(void);

/**
 * @brief 退出临界区
 */
void os_exit_critical(void);

/*======================= Delay Functions =======================*/
/**
 * @brief OS延时(毫秒)
 * @param ms 延时毫秒数
 */
void os_delay_ms(uint32_t ms);

/*======================= Task Functions =======================*/
typedef void* os_task_handle_t;
typedef void (*os_task_func_t)(void* arg);

/**
 * @brief 创建任务
 * @param func 任务函数
 * @param name 任务名称
 * @param stack_size 栈大小(字节)
 * @param arg 任务参数
 * @param priority 任务优先级
 * @param handle 任务句柄指针
 * @return true: 成功, false: 失败
 */
bool os_task_create(os_task_func_t func, const char* name, uint32_t stack_size, 
                    void* arg, uint32_t priority, os_task_handle_t* handle);

/**
 * @brief 删除任务
 * @param handle 任务句柄(NULL表示删除当前任务)
 */
void os_task_delete(os_task_handle_t handle);

/**
 * @brief 启动调度器
 */
void os_scheduler_start(void);

/*======================= Semaphore Functions =======================*/
typedef void* os_sem_handle_t;

/**
 * @brief 创建二值信号量
 * @return 信号量句柄
 */
os_sem_handle_t os_sem_create_binary(void);

/**
 * @brief 获取信号量
 * @param sem 信号量句柄
 * @param timeout_ms 超时时间(ms), 0xFFFFFFFF表示永久等待
 * @return true: 成功, false: 超时
 */
bool os_sem_take(os_sem_handle_t sem, uint32_t timeout_ms);

/**
 * @brief 释放信号量
 * @param sem 信号量句柄
 */
void os_sem_give(os_sem_handle_t sem);

/**
 * @brief 从ISR释放信号量
 * @param sem 信号量句柄
 */
void os_sem_give_from_isr(os_sem_handle_t sem);

/*======================= Mutex Functions =======================*/
typedef void* os_mutex_handle_t;

/**
 * @brief 创建互斥锁
 * @return 互斥锁句柄
 */
os_mutex_handle_t os_mutex_create(void);

/**
 * @brief 获取互斥锁
 * @param mutex 互斥锁句柄
 * @param timeout_ms 超时时间(ms)
 * @return true: 成功, false: 超时
 */
bool os_mutex_take(os_mutex_handle_t mutex, uint32_t timeout_ms);

/**
 * @brief 释放互斥锁
 * @param mutex 互斥锁句柄
 */
void os_mutex_give(os_mutex_handle_t mutex);

/*======================= Queue Functions =======================*/
typedef void* os_queue_handle_t;

/**
 * @brief 创建消息队列
 * @param queue_len 队列长度
 * @param item_size 每个元素大小
 * @return 队列句柄
 */
os_queue_handle_t os_queue_create(uint32_t queue_len, uint32_t item_size);

/**
 * @brief 发送消息到队列
 * @param queue 队列句柄
 * @param item 消息指针
 * @param timeout_ms 超时时间(ms)
 * @return true: 成功, false: 失败
 */
bool os_queue_send(os_queue_handle_t queue, const void* item, uint32_t timeout_ms);

/**
 * @brief 从队列接收消息
 * @param queue 队列句柄
 * @param item 消息缓冲区
 * @param timeout_ms 超时时间(ms)
 * @return true: 成功, false: 超时
 */
bool os_queue_receive(os_queue_handle_t queue, void* item, uint32_t timeout_ms);

/*======================= Tick Functions =======================*/
/**
 * @brief 获取系统Tick
 * @return 系统Tick值
 */
uint32_t os_get_tick_count(void);

#ifdef __cplusplus
}
#endif

#endif /* __OS_WRAPPER_H__ */
