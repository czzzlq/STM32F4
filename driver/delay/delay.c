/**
  ******************************************************************************
  * @file       delay.c
  * @author     czzzlq
  * @version    V0.1
  * @date       2025年12月14日
  * @brief      delay驱动
  * @attention  仅供学习使用
  * @note       仅适用于STM32F103ZET6
  * @version    
  *             -V0.1
  ******************************************************************************
**/

#include "../system/system_hal.h"
#include "delay.h"

static uint32_t g_usDelayFactor = 0;    //微秒延时的节拍系数（每微秒对应的Systick计数）

#if SYS_SUPPORT_OS

#include "FreeRTOS.h"
#include "task.h"

extern void xPortSysTickHandler(void);  //FreeRTOS的SysTick调度函数

/**
 * @brief SysTick中断服务函数
 * @note 触发时更新系统时标并执行任务调度
 * @retval 无
 */
void SysTick_Handler(void){
    HAL_IncTick();  //更新HAL库时标

    //操作系统启动后才执行调度
    if(xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED){  //检查任务调度器的状态
        xPortSysTickHandler(); //间接触发PendSV中断
    }
}

#endif /* SYS_SUPPORT_OS */

/**
 * @brief 初始化SysTick延时模块
 * @param systemclkMHz:系统时钟频率(单位:MHz)
 * @note 1.配置SysTick时钟源为HCLK/8
 *       2.计算微秒延时系数，初始化操作系统所需的节拍值
 * @retval 无
 */
void systickDelayInit(uint16_t systemclkMHz){
#if SYS_SUPPORT_OS
    uint32_t reloadValue;   //SysTick重装载值
#endif

    SysTick->CTRL = 0;  //先关闭SysTick

    HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK_DIV8);  //配置SysTick时钟源为HCLK / 8,HCLK = systemclkMHz * 1MHz

    g_usDelayFactor = systemclkMHz / 8; //计算微秒延时系数

#if SYS_SUPPORT_OS
    //计算FreeRTOS的SysTick的重装载值
    /*
        #define configTICK_RATE_HZ 1000 定义系统时钟节拍频率, 单位: Hz, 无默认需定义 ,
        即定义了1ms产生一个系统节拍

        reloadValue的计算过程：9000000个脉冲周期每秒 / 每秒1000个系统节拍
        最后得出：每个系统节拍需要9000个脉冲周期
    */
    reloadValue = systemclkMHz / 8; //基础频率(单位:MHz)
    reloadValue *= 1000000 / configTICK_RATE_HZ; //换算为对应节拍的计数

    SysTick->CTRL |= 1 << 1;    //开启SysTick中断
    SysTick->LOAD = reloadValue;    //设置重装载值
    SysTick->CTRL |= 1 << 0;    //开启SysTick
#endif
}

/**
 * @brief 微秒级延时
 * @param us:目标延时微秒数
 * @note 1.利用SysTick递减计数器的特性，累计计数判断延时是否完成
 *       2.兼容操作系统（不依赖中断，避免调度干扰）
 * @retval 无
 */
void systickDelay_us(uint32_t us){
    uint32_t requiredTicks; //所需的总节拍数
    uint32_t startTick;     //起始时刻计数器的数值
    uint32_t currentTick;   //当前时刻计数器值
    uint32_t accumulatedTicks;  //累计的节拍数
    const uint32_t reloadMaxVal = SysTick->LOAD;    //SysTick最大重装载值

    //计算所需总节拍数（微秒数 * 每微秒节拍系数）
    /*
        每个系统节拍1ms，计9000个数
    */
    requiredTicks = us * g_usDelayFactor;
    startTick = SysTick->VAL; //读取SysTick的VLA寄存器，读取SysTick当前的数值作为起始时刻计数器的数值
    accumulatedTicks = 0;   //初始化累计节拍数

    //循环累计节拍
    while(1){
        currentTick = SysTick->VAL;
        
        //若计数器值变化（计数器未停滞）
        if(currentTick != startTick){
            //SysTick是递减计数器，分两种情况讨论（计数器递减可能溢出）
            if(currentTick < startTick){
                //当前值 < 起始值：直接累加差值
                accumulatedTicks += startTick - currentTick;
            }else{
                //当前值 >= 起始值：累加（最大值-当前值+起始值）（递减已溢出一次)
                accumulatedTicks += reloadMaxVal + startTick - currentTick;
            }

            startTick = currentTick;    //更新起始值

            //累计节拍达到目标，退出循环
            if(accumulatedTicks >= requiredTicks){
                break;
            }
        }
    }
}

/**
 * @brief 毫秒级延时-基于微秒延时实现
 * @param ms:目标延时毫秒数
 * @retval 无
 */
void systickDelay_ms(uint16_t ms){
    if(ms == 0)
        return;
    systickDelay_us((uint32_t)ms * 1000);
}

#if(!SYS_SUPPORT_OS)
/**
 * @brief 重写HAL库延时函数
 * @param ms:目标延时毫秒数
 * @retval 无
 */
void HAL_Delay(uint32_t ms){
    systickDelay_ms(ms);
}
#endif /* (!SYS_SUPPORT_OS) */
