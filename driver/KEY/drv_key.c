/**
 * @file    drv_key.c
 * @brief   按键驱动（软硬件分离：硬件描述取自 bsp_gpio.h）
 * @note    扫描在定时器中断中执行，事件获取与状态修改经临界区保护。
 *          待处理事件用位图累积而不是单个槽位：扫描中断可能在后一个事件
 *          发生前，前一个事件还没被任务取走，单槽位会让前一个事件丢失
 */
#include "drv_key.h"
#include "bsp_gpio.h"
#include "os_wrapper.h"
#include "timer_callback.h"

#define DEBOUNCE_TIME           10      /* 消抖时间（ms） */
#define KEY_LONG_PRESS_TIME     3000    /* 长按阈值（ms） */
#define KEY_REPEAT_TIME         500     /* 连发周期（ms） */

/**
 * @brief   按键全局控制块
 * @note    volatile：中断与任务都会访问
 */
volatile drv_key_ctrl_block_t drv_key_ctrl_block[DRV_KEY_ID_MAX] =
{
    {BSP_KEY0_PORT, BSP_KEY0_PIN, 0, 0, 0, BSP_KEY0_ACTIVE_LEVEL, 0, 0},
    {BSP_KEY1_PORT, BSP_KEY1_PIN, 0, 0, 0, BSP_KEY1_ACTIVE_LEVEL, 0, 0},
    {BSP_WKUP_PORT, BSP_WKUP_PIN, 0, 0, 0, BSP_WKUP_ACTIVE_LEVEL, 0, 0}
};

/**
 * @brief   登记一个待处理事件（位图累积）
 * @param   pCtrlBlock : 按键控制块指针
 *   @arg   event      : 事件（不可为 DRV_KEY_EVENT_NONE）
 */
static void key_post_event(volatile drv_key_ctrl_block_t *pCtrlBlock, drv_key_event_t event)
{
    pCtrlBlock->key_pending |= (uint8_t)(1U << (uint8_t)event);
}

/**
 * @brief   单按键状态机检测（在定时器中断中调用）
 * @param   pCtrlBlock : 按键控制块指针（volatile：与任务共享）
 */
static void key_detect(volatile drv_key_ctrl_block_t *pCtrlBlock)
{
    /* 直读 IDR 寄存器（GPIO_PIN_x 与 IDR 位一一对应），省去 HAL 调用开销 */
    uint8_t pinLevel = (pCtrlBlock->gpio_port->IDR & pCtrlBlock->gpio_pin) ? 1 : 0;
    uint32_t now = HAL_GetTick();

    if (pinLevel == pCtrlBlock->active_level)                   /* 按下 */
    {
        if (pCtrlBlock->key_state != 1)                         /* 释放 -> 按下 */
        {
            pCtrlBlock->press_time = now;
            pCtrlBlock->key_state = 1;
            pCtrlBlock->long_press_trigger = 0;                 /* 重置长按标志 */
        }

        uint32_t pressDuration = now - pCtrlBlock->press_time;

        if ((pressDuration >= KEY_LONG_PRESS_TIME) && (!pCtrlBlock->long_press_trigger))
        {
            key_post_event(pCtrlBlock, DRV_KEY_EVENT_LONG_PRESS_TRIGGER);
            pCtrlBlock->long_press_trigger = 1;
            pCtrlBlock->repeat_time = now;              /* 连发计时起点 */
        }
        else if (pCtrlBlock->long_press_trigger)
        {
            /* 长按保持期间按 KEY_REPEAT_TIME 周期产生连发事件 */
            if (now - pCtrlBlock->repeat_time >= KEY_REPEAT_TIME)
            {
                key_post_event(pCtrlBlock, DRV_KEY_EVENT_REPEAT);
                pCtrlBlock->repeat_time = now;
            }
        }
        /* 短按事件等释放后再判定 */
    }
    else                                                        /* 释放 */
    {
        if (pCtrlBlock->key_state == 1)                         /* 按下 -> 释放 */
        {
            uint32_t pressDuration = now - pCtrlBlock->press_time;

            if ((pressDuration >= DEBOUNCE_TIME) &&
                (pressDuration < KEY_LONG_PRESS_TIME) &&
                (!pCtrlBlock->long_press_trigger))              /* 长按过的按键不再产生短按事件 */
            {
                key_post_event(pCtrlBlock, DRV_KEY_EVENT_PRESS);
            }
        }

        pCtrlBlock->key_state = 0;
        pCtrlBlock->long_press_trigger = 0;
    }
}

/**
 * @brief   按键扫描接口（在定时器中断中调用）
 */
void drv_key_scan_isr(void)
{
    for (uint8_t i = 0; i < DRV_KEY_ID_MAX; i++)
    {
        key_detect(&drv_key_ctrl_block[i]);
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

    GPIO_InitTypeDef gpioInitStruct = {0};

    /* KEY0 */
    gpioInitStruct.Pin = BSP_KEY0_PIN;
    gpioInitStruct.Mode = GPIO_MODE_INPUT;
    gpioInitStruct.Pull = GPIO_PULLUP;
    gpioInitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BSP_KEY0_PORT, &gpioInitStruct);

    /* KEY1 */
    gpioInitStruct.Pin = BSP_KEY1_PIN;
    HAL_GPIO_Init(BSP_KEY1_PORT, &gpioInitStruct);

    /* WKUP */
    gpioInitStruct.Pin = BSP_WKUP_PIN;
    gpioInitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(BSP_WKUP_PORT, &gpioInitStruct);
}

/**
 * @brief   按键初始化：配置 GPIO 并注册扫描回调到 TIM5
 * @retval  STATUS_OK / timer_callback_register 的错误码
 * @note    注册失败（回调表满）意味着按键永远不会被扫描，
 *          必须把错误码传出去而不是静默忽略
 */
status_t drv_key_init(void)
{
    key_msp_init();

    return timer_callback_register(TIMER_ID_TIM5, drv_key_scan_isr);
}

/**
 * @brief   获取按键事件（每次取出一个，线程安全）
 * @param   key_id : 按键 ID
 * @retval  按键事件；无事件时返回 DRV_KEY_EVENT_NONE，
 *          ID 非法时返回 (drv_key_event_t)STATUS_ERR_PARAM
 * @note    一次只弹出一个事件，若同一按键累积了多个事件（例如任务被阻塞
 *          期间同时发生了长按与连发），后续事件会在下几次调用中依次取出，
 *          不会像单槽实现那样被覆盖丢失
 */
drv_key_event_t drv_key_get_event(drv_key_id_t key_id)
{
    drv_key_event_t event = DRV_KEY_EVENT_NONE;

    if (key_id >= DRV_KEY_ID_MAX)
    {
        return (drv_key_event_t)STATUS_ERR_PARAM;
    }

    os_enter_critical();

    for (uint8_t e = (uint8_t)DRV_KEY_EVENT_PRESS; e <= (uint8_t)DRV_KEY_EVENT_REPEAT; e++)
    {
        if (drv_key_ctrl_block[key_id].key_pending & (uint8_t)(1U << e))
        {
            drv_key_ctrl_block[key_id].key_pending &= (uint8_t)~(1U << e);
            event = (drv_key_event_t)e;
            break;
        }
    }

    os_exit_critical();

    return event;
}

/**
 * @brief   查询按键当前是否被按下
 * @param   key_id : 按键 ID
 * @retval  true: 按下；false: 释放（ID 非法同样返回 false）
 * @note    与 drv_key_get_event 的区别：本接口只读状态、不消费事件，适合
 *          「按住某键开机」这类需要判断持续按下的场景。读取的是扫描中断
 *          维护的状态（每 10ms 刷新），因此调用方看到的是最多 10ms 前的电平；
 *          需要严格时序判断时仍应直接读 GPIO
 */
bool drv_key_is_pressed(drv_key_id_t key_id)
{
    bool pressed;

    if (key_id >= DRV_KEY_ID_MAX)
    {
        return false;
    }

    os_enter_critical();
    pressed = (drv_key_ctrl_block[key_id].key_state == 1);
    os_exit_critical();

    return pressed;
}
