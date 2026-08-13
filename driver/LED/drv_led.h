/**
  ******************************************************************************
  * @file       drv_led.h
  * @author     czzzlq
  * @version    V0.1
  * @date       15-7-2025
  * @brief      LED驱动头文件
  * @attention  仅供学习使用
  * @version    
  *             -V0.1
  ******************************************************************************
**/
#ifndef __DRV_LED_H__
#define __DRV_LED_H__

#include "common_types.h"

//LED逻辑ID
typedef enum{
	DRV_LED_ID_0 = 0, //LED0
	DRV_LED_ID_1,     //LED1
	DRV_LED_ID_MAX    //LED数量上限
} led_id_t;

//LED状态定义
typedef enum{
	DRV_LED_STATE_OFF = 0,  //LED关闭
	DRV_LED_STATE_ON,       //LED点亮
	DRV_LED_STATE_TOGGLE    //LED翻转
} led_state_t;

//LED驱动初始化
status_t drv_led_init(void);
//指定LED的状态
status_t drv_led_set_state(led_id_t id,led_state_t state);
//便捷函数：点亮LED
static inline status_t drv_led_on(led_id_t id){
	return drv_led_set_state(id,DRV_LED_STATE_ON);
}
//便捷函数：关闭LED
static inline status_t drv_led_off(led_id_t id){
	return drv_led_set_state(id,DRV_LED_STATE_OFF);
}
//便捷函数
static inline status_t drv_led_toggle(led_id_t id){
    return drv_led_set_state(id,DRV_LED_STATE_TOGGLE);
}

#endif /* __DRV_LED_H__ */
