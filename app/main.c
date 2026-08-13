#include "main.h"

int main(void)
{
    /* 板级硬件初始化 */
    bsp_init();
    
    /* 启动FreeRTOS任务 */
    freertos_demo();
}
