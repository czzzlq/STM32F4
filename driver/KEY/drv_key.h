/**
  ******************************************************************************
  * @file       drv_key.h
  * @author     czzzlq
  * @version    V0.1
  * @date       2025年12月25日
  * @brief      按键驱动
  * @attention  仅供学习使用
  * @version    
  *             -V0.1
  *             -v0.2:实现应用层与硬件层分离，硬件层负责实现接口
  * @note       1.WKUP即KEY_UP，因为KEY_UP按键连接到PA0，既可作为普通输入，也可做STM32
  *                 唤醒输入。
  ******************************************************************************
**/
#ifndef __DRV_KEY_H__
#define __DRV_KEY_H__

#include "common_types.h"
#include "stm32f1xx_hal.h"

typedef enum{                        //按键ID
    DRV_KEY_ID_0 = 0,   //KEY0
    DRV_KEY_ID_1,       //KEY1
    DRV_KEY_ID_WKUP,    //WKUP
    DRV_KEY_ID_MAX      //按键数量上限（占位）
}drv_key_id_t;

typedef enum{
    DRV_KEY_EVENT_NONE = 0,             //无事件
    DRV_KEY_EVENT_PRESS,                //按键按下事件
    DRV_KEY_EVENT_RELEASE,              //按键释放事件
    DRV_KEY_EVENT_LONG_PRESS_TRIGGER    //按键长按事件
} drv_key_event_t;

typedef struct{
    GPIO_TypeDef* gpio_port;         //GPIO端口
    uint16_t    gpio_pin;            //GPIO引脚
    uint32_t    press_time;          //按下时间（用于判断长按）
    uint8_t     key_state;           //按键当前状态
    uint8_t     key_event;           //触发按键事件,取值为eKeyEvent枚举
    uint8_t     active_level;        //按键有效电平
    uint8_t     long_press_trigger;   //长按触发标志
} drv_key_ctrl_block_t;

//按键驱动初始化
status_t drv_key_init(void);
//在定时器中调用的按键扫描函数
void drv_key_scan_isr(void);
//从应用层获取按键事件（线程安全）
drv_key_event_t drv_key_get_event(drv_key_id_t id);

#endif
