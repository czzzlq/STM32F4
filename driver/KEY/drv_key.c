/**
 * @file    drv_key.c
 * @brief   按键驱动（软硬件分离：硬件描述取自 bsp_gpio.h）
 * @note    扫描在定时器中断中执行，事件获取与状态修改经临界区保护
 */
#include "drv_key.h"
#include "bsp_gpio.h"
#include "os_wrapper.h"
#include "timer_callback.h"

#define DEBOUNCE_TIME           10      /* 消抖时间（ms） */
#define KEY_LONG_PRESS_TIME     3000    /* 长按阈值（ms） */

/**
 * @brief   按键全局控制块
 * @note    volatile：中断与任务都会访问
 */
volatile drv_key_ctrl_block_t drv_key_ctrl_block[DRV_KEY_ID_MAX] =
{
    {BSP_KEY0_PORT, BSP_KEY0_PIN, 0, 0, DRV_KEY_EVENT_NONE, BSP_KEY0_ACTIVE_LEVEL, 0},
    {BSP_KEY1_PORT, BSP_KEY1_PIN, 0, 0, DRV_KEY_EVENT_NONE, BSP_KEY1_ACTIVE_LEVEL, 0},
    {BSP_WKUP_PORT, BSP_WKUP_PIN, 0, 0, DRV_KEY_EVENT_NONE, BSP_WKUP_ACTIVE_LEVEL, 0}
};

/**
 * @brief   单按键状态机检测（在定时器中断中调用）
 * @param   p_ctrl_block : 按键控制块指针
 */
static void key_detect(drv_key_ctrl_block_t* p_ctrl_block)
{
    /* 直读 IDR 寄存器（GPIO_PIN_x 与 IDR 位一一对应），省去 HAL 调用开销 */
    uint8_t pin_level = (p_ctrl_block->gpio_port->IDR & p_ctrl_block->gpio_pin) ? 1 : 0;
    uint32_t now = HAL_GetTick();

    if (pin_level == p_ctrl_block->active_level)                    /* 按下 */
    {
        if (p_ctrl_block->key_state != 1)                           /* 释放 -> 按下 */
        {
            p_ctrl_block->press_time = now;
            p_ctrl_block->key_state = 1;
            p_ctrl_block->long_press_trigger = 0;                   /* 重置长按标志 */
        }

        uint32_t press_duration = now - p_ctrl_block->press_time;

        if ((press_duration >= KEY_LONG_PRESS_TIME) && (!p_ctrl_block->long_press_trigger))
        {
            p_ctrl_block->key_event = DRV_KEY_EVENT_LONG_PRESS_TRIGGER;
            p_ctrl_block->long_press_trigger = 1;
        }
        /* 短按事件等释放后再判定 */
    }
    else                                                            /* 释放 */
    {
        if (p_ctrl_block->key_state == 1)                           /* 按下 -> 释放 */
        {
            uint32_t press_duration = now - p_ctrl_block->press_time;

            if ((press_duration >= DEBOUNCE_TIME) &&
                (press_duration < KEY_LONG_PRESS_TIME) &&
                (!p_ctrl_block->long_press_trigger))                /* 长按过的按键不再产生短按事件 */
            {
                p_ctrl_block->key_event = DRV_KEY_EVENT_PRESS;
            }
        }

        p_ctrl_block->key_state = 0;
        p_ctrl_block->long_press_trigger = 0;
    }
}

/**
 * @brief   按键扫描接口（在定时器中断中调用）
 */
void drv_key_scan_isr(void)
{
    for (uint8_t i = 0; i < DRV_KEY_ID_MAX; i++)
    {
        key_detect((drv_key_ctrl_block_t*)&drv_key_ctrl_block[i]);
    }
}

/**
 * @brief   按键 GPIO 底层初始化
 */
static void key_msp_init(void)
{
    BSP_KEY0_CLK_ENABLE();
    BSP_KEY1_CLK_ENABLE();
    BSP_WKUP_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStructure = {0};

    /* KEY0 */
    GPIO_InitStructure.Pin = BSP_KEY0_PIN;
    GPIO_InitStructure.Mode = GPIO_MODE_INPUT;
    GPIO_InitStructure.Pull = GPIO_PULLUP;
    GPIO_InitStructure.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BSP_KEY0_PORT, &GPIO_InitStructure);

    /* KEY1 */
    GPIO_InitStructure.Pin = BSP_KEY1_PIN;
    HAL_GPIO_Init(BSP_KEY1_PORT, &GPIO_InitStructure);

    /* WKUP */
    GPIO_InitStructure.Pin = BSP_WKUP_PIN;
    GPIO_InitStructure.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(BSP_WKUP_PORT, &GPIO_InitStructure);
}

/**
 * @brief   按键初始化：配置 GPIO 并注册扫描回调到 TIM5
 * @retval  STATUS_OK
 */
status_t drv_key_init(void)
{
    key_msp_init();
    timer_callback_register(TIMER_ID_TIM5, drv_key_scan_isr);
    return STATUS_OK;
}

/**
 * @brief   获取按键事件（读后清空，临界区保护避免竞态）
 * @param   key_id : 按键 ID
 * @retval  按键事件 / STATUS_ERR_PARAM（ID 非法）
 */
drv_key_event_t drv_key_get_event(uint8_t key_id)
{
    drv_key_event_t event;

    if (key_id >= DRV_KEY_ID_MAX)
    {
        return STATUS_ERR_PARAM;
    }

    os_enter_critical();
    event = (drv_key_event_t)drv_key_ctrl_block[key_id].key_event;
    drv_key_ctrl_block[key_id].key_event = DRV_KEY_EVENT_NONE;
    os_exit_critical();

    return event;
}
