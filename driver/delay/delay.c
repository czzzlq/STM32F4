/**
 * @file    delay.c
 * @brief   SysTick 延时驱动（与 FreeRTOS 共存）
 */
#include "system_hal.h"
#include "delay.h"

/* 微秒延时换算系数：SysTick 时钟 = HCLK/8，每 1us 对应 (systemclkMHz/8) 个计数值 */
static uint32_t s_usDelayFactor = 0;

#if SYS_SUPPORT_OS

#include "FreeRTOS.h"
#include "task.h"

extern void xPortSysTickHandler(void);

/**
 * @brief   SysTick 中断服务函数：维护 HAL 时基并驱动 FreeRTOS 心跳
 */
void SysTick_Handler(void)
{
    HAL_IncTick();

    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
        xPortSysTickHandler();
    }
}

#endif /* SYS_SUPPORT_OS */

/**
 * @brief   初始化 SysTick 定时模块
 * @param   systemclkMHz : 系统时钟频率（MHz），本项目为 72
 * @note    SysTick 时钟源为 HCLK/8；启用 OS 时按 configTICK_RATE_HZ 配置 1ms 心跳
 */
void systickDelayInit(uint16_t systemclkMHz)
{
#if SYS_SUPPORT_OS
    uint32_t reloadValue;
#endif

    SysTick->CTRL = 0;      /* 先关闭 SysTick 再修改配置 */

    HAL_SYSTICK_CLKSourceConfig(SYSTICK_CLKSOURCE_HCLK_DIV8);
    s_usDelayFactor = systemclkMHz / 8;

#if SYS_SUPPORT_OS
    /* 心跳周期 = 1ms：reload = (HCLK/8) / configTICK_RATE_HZ */
    reloadValue = (uint32_t)systemclkMHz / 8 * (1000000U / configTICK_RATE_HZ);

    SysTick->LOAD  = reloadValue;
    SysTick->CTRL |= 1 << 1;    /* 使能中断 */
    SysTick->CTRL |= 1 << 0;    /* 启动计数 */
#endif
}

/**
 * @brief   微秒级延时（忙等待，中断屏蔽期间同样可用）
 * @param   us : 目标延时（微秒）
 * @note    轮询 VAL 寄存器变化并累加计数值，可正确处理回绕（wrap）
 */
void systickDelay_us(uint32_t us)
{
    uint32_t startTick;             /* 起始计数值 */
    uint32_t currentTick;           /* 当前计数值 */
    uint32_t elapsedTicks;          /* 已流逝的计数值 */
    const uint32_t reloadValue = SysTick->LOAD;
    const uint32_t requiredTicks = us * s_usDelayFactor;

    startTick = SysTick->VAL;
    elapsedTicks = 0;

    while (elapsedTicks < requiredTicks)
    {
        currentTick = SysTick->VAL;

        if (currentTick != startTick)
        {
            /* SysTick 递减计数：当前值变小为同一周期内递减，变大则发生了回绕 */
            if (currentTick < startTick)
            {
                elapsedTicks += startTick - currentTick;
            }
            else
            {
                elapsedTicks += reloadValue + startTick - currentTick;
            }

            startTick = currentTick;
        }
    }
}

/**
 * @brief   毫秒级延时（基于微秒延时实现）
 * @param   ms : 目标延时（毫秒）
 */
void systickDelay_ms(uint16_t ms)
{
    if (ms == 0)
    {
        return;
    }
    systickDelay_us((uint32_t)ms * 1000U);
}

#if (!SYS_SUPPORT_OS)
/**
 * @brief   重写 HAL 库延时函数（裸机模式下）
 */
void HAL_Delay(uint32_t ms)
{
    systickDelay_ms(ms);
}
#endif /* (!SYS_SUPPORT_OS) */
