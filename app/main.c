/**
 * @file    main.c
 * @brief   程序入口：板级初始化 + 启动 RTOS 调度
 * @note    bsp_init 的返回值必须处理：串口/按键/ADC 任一项失败时继续跑
 *          RTOS 会让故障表现成零散的"没反应"，难以定位
 */
#include "main.h"

/**
 * @brief   程序入口
 * @retval  实际不会返回（freertos_demo 会启动调度器）
 * @note    初始化失败时停在死循环里等待调试器接入/看门狗复位，
 *          避免带着未初始化外设继续运行
 */
int main(void)
{
    status_t initResult;

    /* 板级硬件初始化 */
    initResult = bsp_init();

    if (initResult != STATUS_OK)
    {
        while (1)
        {
            ;               /* 初始化失败：停在此处便于定位（未开看门狗时不会自动复位） */
        }
    }

    /* 启动 FreeRTOS 任务 */
    freertos_demo();

    return 0;               /* 调度器正常运行时不会走到这里 */
}
