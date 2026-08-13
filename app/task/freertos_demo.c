#include "freertos_demo.h"

//START_TASK
#define START_TASK_PRIO      1                                                  /* 任务优先级 */
#define START_STK_SIZE      128                                                 /* 任务堆栈大小 */

//TASK1
#define TASK1_PRIO           2                                                  /* 任务优先级 */    
#define TASK1_STK_SIZE      128                                                 /* 任务堆栈大小 */

//TASK2
#define TASK2_PRIO           3  
#define TASK2_STK_SIZE      128

//START_TASK
os_task_handle_t            StartTask_Handler;
//TASK1
os_task_handle_t            TASK1Task_Handler;
//TASK2
os_task_handle_t            TASK2Task_Handler;

const uint16_t lcd_discolor[11] = {                                                   /* lcd屏幕刷新显示的颜色 */
    WHITE,BLACK,BLUE,RED,
    MAGENTA,GREEN,CYAN,YELLOW,
    BROWN,BRRED,GRAY
};

static void Start_Task(void *pvParameters);
static void Task1(void *pvParameters);
static void Task2(void *pvParameters);

/**
 * @brief   FreeRTOS例程入口函数
 * @param   无
 * @retval  无
 */
void freertos_demo(void)
{
    lcd_show_string(10,10,220,32,32,"STM32",BLACK);
    lcd_show_string(10,80,520,32,32,"FreeRTOS Porting",BLACK);
    lcd_show_string(10,150,220,32,32,"Hello World",BLACK);

    //动态创建任务
    os_task_create(Start_Task, "Start_task", START_STK_SIZE * sizeof(StackType_t),
                   NULL, START_TASK_PRIO, &StartTask_Handler);
    os_scheduler_start();
}

/**
 * @brief   Start_Task
 * @param   pvParameters :传入参数
 * @retval  无
 */
void Start_Task(void *pvParameters)
{
    os_enter_critical();                                                        /* 非中断进入临界区 */

    //动态创建任务1
    os_task_create(Task1, "Task1", TASK1_STK_SIZE * sizeof(StackType_t),
                   NULL, TASK1_PRIO, &TASK1Task_Handler);

    os_task_create(Task2, "Task2", TASK2_STK_SIZE * sizeof(StackType_t),
                   NULL, TASK2_PRIO, &TASK2Task_Handler);
    
    os_task_delete(StartTask_Handler);                                          /* 删除开始任务 */
    os_exit_critical();                                                         /*　退出临界区 */
}

/**
 * @brief   Task1
 * @param   pvParameters :传入参数
 * @retval  无
 */
void Task1(void *pvParameters)
{
    UNUSED(pvParameters);

    while(1)
    {
        if(drv_key_get_event(DRV_KEY_ID_0) == DRV_KEY_EVENT_PRESS){
            drv_led_toggle(DRV_LED_ID_0);
        }
        os_delay_ms(10);
    }
}

/**
 * @brief   Task2
 * @param   pvParameters :传入参数
 * @retval  无
 */
void Task2(void *pvParameters)
{
    UNUSED(pvParameters);

    while(1)
    {
        drv_led_toggle(DRV_LED_ID_1);
        os_delay_ms(500);
    }
}
