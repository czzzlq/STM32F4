/**
  ******************************************************************************
  * @file       os_wrapper.c
  * @author     czzzlq
  * @version    V1.0
  * @date       2025-12-26
  * @brief      OS抽象层实现 - FreeRTOS适配
  * @attention  仅供学习使用
  ******************************************************************************
**/
#include "os_wrapper.h"

#if OS_USE_FREERTOS

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"

/*======================= Critical Section =======================*/
void os_enter_critical(void)
{
    taskENTER_CRITICAL();
}

void os_exit_critical(void)
{
    taskEXIT_CRITICAL();
}

/*======================= Delay Functions =======================*/
void os_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/*======================= Task Functions =======================*/
bool os_task_create(os_task_func_t func, const char* name, uint32_t stack_size,
                    void* arg, uint32_t priority, os_task_handle_t* handle)
{
    BaseType_t ret = xTaskCreate((TaskFunction_t)func, name, 
                                  stack_size / sizeof(StackType_t),
                                  arg, priority, (TaskHandle_t*)handle);
    return (ret == pdPASS);
}

void os_task_delete(os_task_handle_t handle)
{
    vTaskDelete((TaskHandle_t)handle);
}

void os_scheduler_start(void)
{
    vTaskStartScheduler();
}

/*======================= Semaphore Functions =======================*/
os_sem_handle_t os_sem_create_binary(void)
{
    return (os_sem_handle_t)xSemaphoreCreateBinary();
}

bool os_sem_take(os_sem_handle_t sem, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0xFFFFFFFF) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (xSemaphoreTake((SemaphoreHandle_t)sem, ticks) == pdTRUE);
}

void os_sem_give(os_sem_handle_t sem)
{
    xSemaphoreGive((SemaphoreHandle_t)sem);
}

void os_sem_give_from_isr(os_sem_handle_t sem)
{
    BaseType_t higher_priority_woken = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)sem, &higher_priority_woken);
    portYIELD_FROM_ISR(higher_priority_woken);
}

/*======================= Mutex Functions =======================*/
os_mutex_handle_t os_mutex_create(void)
{
    return (os_mutex_handle_t)xSemaphoreCreateMutex();
}

bool os_mutex_take(os_mutex_handle_t mutex, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0xFFFFFFFF) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (xSemaphoreTake((SemaphoreHandle_t)mutex, ticks) == pdTRUE);
}

void os_mutex_give(os_mutex_handle_t mutex)
{
    xSemaphoreGive((SemaphoreHandle_t)mutex);
}

/*======================= Queue Functions =======================*/
os_queue_handle_t os_queue_create(uint32_t queue_len, uint32_t item_size)
{
    return (os_queue_handle_t)xQueueCreate(queue_len, item_size);
}

bool os_queue_send(os_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0xFFFFFFFF) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (xQueueSend((QueueHandle_t)queue, item, ticks) == pdTRUE);
}

bool os_queue_receive(os_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0xFFFFFFFF) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (xQueueReceive((QueueHandle_t)queue, item, ticks) == pdTRUE);
}

/*======================= Tick Functions =======================*/
uint32_t os_get_tick_count(void)
{
    return xTaskGetTickCount();
}

#else /* Bare metal implementation */

#include "stm32f4xx_hal.h"

static volatile uint32_t s_critical_nesting = 0;

void os_enter_critical(void)
{
    __disable_irq();
    s_critical_nesting++;
}

void os_exit_critical(void)
{
    s_critical_nesting--;
    if (s_critical_nesting == 0) {
        __enable_irq();
    }
}

void os_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

bool os_task_create(os_task_func_t func, const char* name, uint32_t stack_size,
                    void* arg, uint32_t priority, os_task_handle_t* handle)
{
    (void)func; (void)name; (void)stack_size; (void)arg; (void)priority; (void)handle;
    return false; /* 裸机模式不支持任务创建 */
}

void os_task_delete(os_task_handle_t handle)
{
    (void)handle;
}

void os_scheduler_start(void)
{
    /* 裸机模式空实现 */
}

os_sem_handle_t os_sem_create_binary(void)
{
    return NULL;
}

bool os_sem_take(os_sem_handle_t sem, uint32_t timeout_ms)
{
    (void)sem; (void)timeout_ms;
    return true;
}

void os_sem_give(os_sem_handle_t sem)
{
    (void)sem;
}

void os_sem_give_from_isr(os_sem_handle_t sem)
{
    (void)sem;
}

os_mutex_handle_t os_mutex_create(void)
{
    return NULL;
}

bool os_mutex_take(os_mutex_handle_t mutex, uint32_t timeout_ms)
{
    (void)mutex; (void)timeout_ms;
    os_enter_critical();
    return true;
}

void os_mutex_give(os_mutex_handle_t mutex)
{
    (void)mutex;
    os_exit_critical();
}

os_queue_handle_t os_queue_create(uint32_t queue_len, uint32_t item_size)
{
    (void)queue_len; (void)item_size;
    return NULL;
}

bool os_queue_send(os_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    (void)queue; (void)item; (void)timeout_ms;
    return false;
}

bool os_queue_receive(os_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    (void)queue; (void)item; (void)timeout_ms;
    return false;
}

uint32_t os_get_tick_count(void)
{
    return HAL_GetTick();
}

#endif /* OS_USE_FREERTOS */
