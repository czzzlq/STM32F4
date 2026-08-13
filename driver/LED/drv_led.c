/**
  ******************************************************************************
  * @file       LED.c
  * @author     czzzlq
  * @version    V0.1
  * @date       15-7-2025
  * @brief      LED驱动
  * @attention  仅供学习使用
  * @version    
  *             -V0.1
  ******************************************************************************
**/
#include "drv_led.h"
#include "bsp_gpio.h"
#include "stm32f1xx_hal.h"

//LED硬件描述结构体
typedef struct{
	GPIO_TypeDef*   port; //GPIO端口
	uint16_t         pin; //GPIO引脚
	uint8_t active_level; //有效电平：1：高电平点亮；0：低电平电路
} led_hardware_t;

//LED硬件映射表
static led_hardware_t s_led_table[DRV_LED_ID_MAX] = {
	[DRV_LED_ID_0] = {BSP_LED0_PORT,BSP_LED0_PIN,BSP_LED0_ACTIVE_LEVEL},
	[DRV_LED_ID_1] = {BSP_LED1_PORT,BSP_LED1_PIN,BSP_LED1_ACTIVE_LEVEL}
};

/**
 * @brief 	LED驱动初始化
 * @param	void
 * @retval	status_t
 * 				STATUS_OK:初始化成功
 */
status_t drv_led_init(void){
	BSP_LED0_CLK_ENABLE();
	BSP_LED1_CLK_ENABLE();

	GPIO_InitTypeDef gpio_init_structure = {0};
	gpio_init_structure.Mode = GPIO_MODE_OUTPUT_PP;
	gpio_init_structure.Pull = GPIO_PULLUP;
	gpio_init_structure.Speed = GPIO_SPEED_FREQ_HIGH;

	//初始化每一个LED
	for(uint8_t i = 0; i < DRV_LED_ID_MAX; i++){
		gpio_init_structure.Pin = s_led_table[i].pin;
		HAL_GPIO_Init(s_led_table[i].port,&gpio_init_structure);
	}

	//初始化关闭所有LED
	drv_led_off(DRV_LED_ID_0);
	drv_led_off(DRV_LED_ID_1);

	return STATUS_OK;
}

/**
 * @brief 	设置指定LED的状态
 * @param	id：LED的ID号
 * @param	state:LED状态
 * @retval	
 */
status_t drv_led_set_state(led_id_t id,led_state_t state){
	if(id >= DRV_LED_ID_MAX){
		return STATUS_ERR_PARAM;	//传入非法LED ID
	}

	GPIO_PinState pin_state;
	led_hardware_t* led = &s_led_table[id];

	switch(state){
		case DRV_LED_STATE_ON:	//点亮LED
			//根据active_level决定输出电平
			pin_state = led->active_level ? GPIO_PIN_SET : GPIO_PIN_RESET;
			break;
		
		case DRV_LED_STATE_OFF:	//关闭LED
			pin_state = led->active_level ? GPIO_PIN_RESET : GPIO_PIN_SET;
			break;
		
		case DRV_LED_STATE_TOGGLE:	//翻转LED
			HAL_GPIO_TogglePin(led->port,led->pin);
			return STATUS_OK;	//翻转完成后直接返回

		default:
			return STATUS_ERR_PARAM;	//状态参数非法
	}

	HAL_GPIO_WritePin(led->port,led->pin,pin_state);

	return STATUS_OK;
}
