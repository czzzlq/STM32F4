/**
 * @file    drv_led.c
 * @brief   LED 驱动（软硬件分离：硬件描述取自 bsp_gpio.h）
 */
#include "drv_led.h"
#include "bsp_gpio.h"
#include "stm32f4xx_hal.h"

/* LED 硬件描述结构体 */
typedef struct
{
    GPIO_TypeDef*   port;           /* GPIO 端口 */
    uint16_t        pin;            /* GPIO 引脚 */
    uint8_t         active_level;   /* 有效电平：1 高电平点亮，0 低电平点亮 */
} led_hardware_t;

/* LED 硬件映射表（放在 RAM 中：F1 内部 Flash 有等待周期，RAM 访问更快） */
static led_hardware_t s_led_table[DRV_LED_ID_MAX] =
{
    [DRV_LED_ID_0] = {BSP_LED0_PORT, BSP_LED0_PIN, BSP_LED0_ACTIVE_LEVEL},
    [DRV_LED_ID_1] = {BSP_LED1_PORT, BSP_LED1_PIN, BSP_LED1_ACTIVE_LEVEL}
};

/**
 * @brief   LED 驱动初始化（初始化后全部熄灭）
 * @retval  STATUS_OK
 */
status_t drv_led_init(void)
{
    BSP_LED0_CLK_ENABLE();
    BSP_LED1_CLK_ENABLE();

    GPIO_InitTypeDef gpioInitStruct = {0};
    gpioInitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    gpioInitStruct.Pull = GPIO_PULLUP;
    gpioInitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    for (uint8_t i = 0; i < DRV_LED_ID_MAX; i++)
    {
        gpioInitStruct.Pin = s_led_table[i].pin;
        HAL_GPIO_Init(s_led_table[i].port, &gpioInitStruct);
    }

    drv_led_off(DRV_LED_ID_0);
    drv_led_off(DRV_LED_ID_1);

    return STATUS_OK;
}

/**
 * @brief   设置指定 LED 的状态
 * @param   id    : LED 逻辑 ID
 * @param   state : DRV_LED_STATE_ON / OFF / TOGGLE
 * @retval  STATUS_OK / STATUS_ERR_PARAM
 */
status_t drv_led_set_state(led_id_t id, led_state_t state)
{
    GPIO_PinState pinState;

    if (id >= DRV_LED_ID_MAX)
    {
        return STATUS_ERR_PARAM;
    }

    led_hardware_t* led = &s_led_table[id];

    switch (state)
    {
        case DRV_LED_STATE_ON:
            pinState = led->active_level ? GPIO_PIN_SET : GPIO_PIN_RESET;
            break;

        case DRV_LED_STATE_OFF:
            pinState = led->active_level ? GPIO_PIN_RESET : GPIO_PIN_SET;
            break;

        case DRV_LED_STATE_TOGGLE:
            HAL_GPIO_TogglePin(led->port, led->pin);
            return STATUS_OK;

        default:
            return STATUS_ERR_PARAM;
    }

    HAL_GPIO_WritePin(led->port, led->pin, pinState);

    return STATUS_OK;
}
