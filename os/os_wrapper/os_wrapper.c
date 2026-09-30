/**
 * @file    os_wrapper.c
 * @brief   OS 抽象层实现（FreeRTOS 适配 / 裸机空实现）
 * @note    上层只依赖 os_wrapper.h，切换 OS_USE_FREERTOS 即可在
 *          FreeRTOS 与裸机之间切换，业务代码不需要改
 */
#include "os_wrapper.h"

#if OS_USE_FREERTOS

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"

/*======================= Assert =======================*/
/* 断言现场：只打印第一次，后续只累加次数并记录"最近一次"位置
 * （理由见 FreeRTOSConfig.h 的 configASSERT 说明） */
volatile const char *g_assertFile = NULL;   /* 首次断言的文件 */
volatile uint32_t g_assertLine = 0;         /* 首次断言的行号 */
volatile uint32_t g_assertCount = 0;        /* 断言累计次数 */
volatile const char *g_assertLastFile = NULL;   /* 最近一次断言的文件 */
volatile uint32_t g_assertLastLine = 0;         /* 最近一次断言的行号 */

/**
 * @brief   FreeRTOS 断言失败处理
 * @param   pcFile : 触发断言的文件（__FILE__）
 *   @arg   ulLine : 触发行号（__LINE__）
 * @note    先置位现场再打印：打印本身若再次触发断言也不会重入。
 *          串口只输出第一行，避免中断里反复断言造成刷屏并挤掉正常日志；
 *          调试时看 g_assertFile/g_assertLine（首次）与
 *          g_assertLastFile/g_assertLastLine（最近一次）能区分"同一个断言反复触发"
 *          与"后面又出现了另一个断言"
 */
void vAssertCalled(const char *pcFile, uint32_t ulLine)
{
    g_assertLastFile = pcFile;
    g_assertLastLine = ulLine;

    if (g_assertFile == NULL)
    {
        g_assertFile = pcFile;
        g_assertLine = ulLine;
        g_assertCount = 1;
        printf("Error: %s, %d\r\n", pcFile, (int)ulLine);
    }
    else
    {
        g_assertCount++;
    }
}

/*======================= Critical Section =======================*/
/**
 * @brief   进入临界区
 * @note    FreeRTOS 的 taskENTER_CRITICAL 通过 BASEPRI 屏蔽优先级
 *          不高于 configMAX_SYSCALL_INTERRUPT_PRIORITY 的中断，且可嵌套
 *          （嵌套深度由 uxCriticalNesting 维护），因此成对使用是安全的。
 *          高于该阈值的中断不会被屏蔽，所以驱动侧的中断优先级必须 >= 5
 */
void os_enter_critical(void)
{
    taskENTER_CRITICAL();
}

/**
 * @brief   退出临界区
 * @note    只有嵌套深度归零时才真正开中断
 */
void os_exit_critical(void)
{
    taskEXIT_CRITICAL();
}

/*======================= Delay Functions =======================*/
/**
 * @brief   毫秒级延时（让出 CPU）
 * @param   ms : 延时毫秒数
 * @note    与 HAL_Delay 的区别：本函数会让出 CPU 给其它任务，
 *          只能在任务上下文调用，绝不能在中断中调用
 */
void os_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/*======================= Task Functions =======================*/
/**
 * @brief   创建任务
 * @param   func       : 任务函数
 *   @arg   name       : 任务名（仅调试用）
 *   @arg   stack_size : 栈大小（字节，内部换算为 StackType_t 个数）
 *   @arg   arg        : 传给任务的参数
 *   @arg   priority   : 优先级
 *   @arg   handle     : 任务句柄输出（可为 NULL）
 * @retval  true 创建成功；false 失败（通常是 FreeRTOS 堆不足）
 * @note    栈大小按字节传入、内部除以 sizeof(StackType_t)，避免调用方
 *          误按"字数"计算导致栈偏小 4 倍
 */
bool os_task_create(os_task_func_t func, const char* name, uint32_t stack_size,
                    void* arg, uint32_t priority, os_task_handle_t* handle)
{
    BaseType_t ret = xTaskCreate((TaskFunction_t)func, name, 
                                  stack_size / sizeof(StackType_t),
                                  arg, priority, (TaskHandle_t*)handle);
    return (ret == pdPASS);
}

/**
 * @brief   删除任务
 * @param   handle : 任务句柄（传 NULL 表示删除自身）
 * @note    删除自身后本任务的栈要等空闲任务回收，因此调用点不能在
 *          临界区内：出临界区时若发生任务切换，返回路径会跑在已释放的栈上
 */
void os_task_delete(os_task_handle_t handle)
{
    vTaskDelete((TaskHandle_t)handle);
}

/**
 * @brief   启动任务调度器（正常情况下不会返回）
 */
void os_scheduler_start(void)
{
    vTaskStartScheduler();
}

/*======================= Semaphore Functions =======================*/
/**
 * @brief   创建二值信号量
 * @retval  信号量句柄；NULL 表示创建失败
 * @note    新建的二值信号量初值为 0（不可用），首次 take 会阻塞
 */
os_sem_handle_t os_sem_create_binary(void)
{
    return (os_sem_handle_t)xSemaphoreCreateBinary();
}

/**
 * @brief   获取信号量（阻塞）
 * @param   sem       : 信号量句柄
 *   @arg   timeout_ms: 超时毫秒数，0xFFFFFFFF 表示永久等待
 * @retval  true 获取成功；false 超时或失败
 */
bool os_sem_take(os_sem_handle_t sem, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0xFFFFFFFF) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (xSemaphoreTake((SemaphoreHandle_t)sem, ticks) == pdTRUE);
}

/**
 * @brief   释放信号量（任务上下文）
 * @param   sem : 信号量句柄
 */
void os_sem_give(os_sem_handle_t sem)
{
    xSemaphoreGive((SemaphoreHandle_t)sem);
}

/**
 * @brief   释放信号量（中断上下文）
 * @param   sem : 信号量句柄
 * @note    必须用 FromISR 版本：普通版本会操作就绪链表并可能触发调度，
 *          在中断里直接调用会破坏内核数据结构。调用方的中断优先级
 *          必须在 FreeRTOS 管理范围内（>= configMAX_SYSCALL_INTERRUPT_PRIORITY）
 */
void os_sem_give_from_isr(os_sem_handle_t sem)
{
    BaseType_t higher_priority_woken = pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)sem, &higher_priority_woken);
    portYIELD_FROM_ISR(higher_priority_woken);
}

/*======================= Mutex Functions =======================*/
/**
 * @brief   创建互斥量
 * @retval  互斥量句柄；NULL 表示创建失败
 * @note    互斥量带优先级继承，用于保护共享资源；与二值信号量的
 *          区别是它记录持有者，因此只能由持有者释放
 */
os_mutex_handle_t os_mutex_create(void)
{
    return (os_mutex_handle_t)xSemaphoreCreateMutex();
}

/**
 * @brief   获取互斥量（阻塞）
 * @param   mutex      : 互斥量句柄
 *   @arg   timeout_ms : 超时毫秒数，0xFFFFFFFF 表示永久等待
 * @retval  true 获取成功；false 超时或失败
 */
bool os_mutex_take(os_mutex_handle_t mutex, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0xFFFFFFFF) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (xSemaphoreTake((SemaphoreHandle_t)mutex, ticks) == pdTRUE);
}

/**
 * @brief   释放互斥量
 * @param   mutex : 互斥量句柄
 */
void os_mutex_give(os_mutex_handle_t mutex)
{
    xSemaphoreGive((SemaphoreHandle_t)mutex);
}

/*======================= Queue Functions =======================*/
/**
 * @brief   创建队列
 * @param   queue_len : 队列深度（元素个数）
 *   @arg   item_size : 单个元素字节数
 * @retval  队列句柄；NULL 表示创建失败
 * @note    队列按值拷贝元素，因此入队的数据可以立即复用
 */
os_queue_handle_t os_queue_create(uint32_t queue_len, uint32_t item_size)
{
    return (os_queue_handle_t)xQueueCreate(queue_len, item_size);
}

/**
 * @brief   入队（阻塞）
 * @param   queue      : 队列句柄
 *   @arg   item       : 元素指针（按值拷贝进队列）
 *   @arg   timeout_ms : 超时毫秒数，0xFFFFFFFF 表示永久等待
 * @retval  true 入队成功；false 超时或失败
 */
bool os_queue_send(os_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0xFFFFFFFF) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (xQueueSend((QueueHandle_t)queue, item, ticks) == pdTRUE);
}

/**
 * @brief   出队（阻塞）
 * @param   queue      : 队列句柄
 *   @arg   item       : 接收缓冲
 *   @arg   timeout_ms : 超时毫秒数，0xFFFFFFFF 表示永久等待
 * @retval  true 出队成功；false 超时或失败
 */
bool os_queue_receive(os_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    TickType_t ticks = (timeout_ms == 0xFFFFFFFF) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (xQueueReceive((QueueHandle_t)queue, item, ticks) == pdTRUE);
}

/*======================= Tick Functions =======================*/
/**
 * @brief   获取系统节拍计数
 * @retval  自调度器启动以来的节拍数；configTICK_RATE_HZ=1000 时即毫秒数
 */
uint32_t os_get_tick_count(void)
{
    return xTaskGetTickCount();
}

#else /* Bare metal implementation */

#include "stm32f4xx_hal.h"

static volatile uint32_t s_critical_nesting = 0;    /* 临界区嵌套深度（裸机下自行维护） */

/**
 * @brief   进入临界区（裸机）
 * @note    裸机没有优先级屏蔽机制，只能整体关中断；用嵌套计数保证
 *          多层嵌套时不会在里层就提前开中断
 */
void os_enter_critical(void)
{
    __disable_irq();
    s_critical_nesting++;
}

/**
 * @brief   退出临界区（裸机）
 * @note    嵌套深度归零时才真正开中断
 */
void os_exit_critical(void)
{
    s_critical_nesting--;
    if (s_critical_nesting == 0) {
        __enable_irq();
    }
}

/**
 * @brief   毫秒级延时（裸机）
 * @param   ms : 延时毫秒数
 * @note    裸机下用 HAL_Delay 忙等（依赖 SysTick 中断，不能在关中断时调用）
 */
void os_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

/**
 * @brief   创建任务（裸机不支持）
 * @retval  恒为 false
 */
bool os_task_create(os_task_func_t func, const char* name, uint32_t stack_size,
                    void* arg, uint32_t priority, os_task_handle_t* handle)
{
    (void)func; (void)name; (void)stack_size; (void)arg; (void)priority; (void)handle;
    return false; /* 裸机模式不支持任务创建 */
}

/**
 * @brief   删除任务（裸机空实现）
 */
void os_task_delete(os_task_handle_t handle)
{
    (void)handle;
}

/**
 * @brief   启动调度器（裸机空实现：主循环由调用方自己写）
 */
void os_scheduler_start(void)
{
    /* 裸机模式空实现 */
}

/**
 * @brief   创建二值信号量（裸机不支持）
 * @retval  恒为 NULL
 */
os_sem_handle_t os_sem_create_binary(void)
{
    return NULL;
}

/**
 * @brief   获取信号量（裸机空实现）
 * @retval  恒为 true（裸机是顺序执行，不存在竞争等待）
 */
bool os_sem_take(os_sem_handle_t sem, uint32_t timeout_ms)
{
    (void)sem; (void)timeout_ms;
    return true;
}

/**
 * @brief   释放信号量（裸机空实现）
 */
void os_sem_give(os_sem_handle_t sem)
{
    (void)sem;
}

/**
 * @brief   释放信号量（中断上下文，裸机空实现）
 */
void os_sem_give_from_isr(os_sem_handle_t sem)
{
    (void)sem;
}

/**
 * @brief   创建互斥量（裸机不支持）
 * @retval  恒为 NULL
 */
os_mutex_handle_t os_mutex_create(void)
{
    return NULL;
}

/**
 * @brief   获取互斥量（裸机下退化为进入临界区）
 * @retval  恒为 true
 * @note    裸机没有任务概念，用关中断代替互斥，因此必须与
 *          os_mutex_give 严格配对
 */
bool os_mutex_take(os_mutex_handle_t mutex, uint32_t timeout_ms)
{
    (void)mutex; (void)timeout_ms;
    os_enter_critical();
    return true;
}

/**
 * @brief   释放互斥量（裸机下退化为退出临界区）
 */
void os_mutex_give(os_mutex_handle_t mutex)
{
    (void)mutex;
    os_exit_critical();
}

/**
 * @brief   创建队列（裸机不支持）
 * @retval  恒为 NULL
 */
os_queue_handle_t os_queue_create(uint32_t queue_len, uint32_t item_size)
{
    (void)queue_len; (void)item_size;
    return NULL;
}

/**
 * @brief   入队（裸机不支持）
 * @retval  恒为 false
 */
bool os_queue_send(os_queue_handle_t queue, const void* item, uint32_t timeout_ms)
{
    (void)queue; (void)item; (void)timeout_ms;
    return false;
}

/**
 * @brief   出队（裸机不支持）
 * @retval  恒为 false
 */
bool os_queue_receive(os_queue_handle_t queue, void* item, uint32_t timeout_ms)
{
    (void)queue; (void)item; (void)timeout_ms;
    return false;
}

/**
 * @brief   获取系统节拍计数（裸机）
 * @retval  HAL 的毫秒计数（依赖 SysTick 中断持续走时）
 */
uint32_t os_get_tick_count(void)
{
    return HAL_GetTick();
}

#endif /* OS_USE_FREERTOS */
