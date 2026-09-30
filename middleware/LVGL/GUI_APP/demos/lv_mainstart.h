/**
 * @file    lv_mainstart.h
 * @brief   LVGL 验证界面入口
 * @note    界面代码只依赖 LVGL，不 include 任何驱动头文件；需要联动硬件
 *          （例如界面上的开关去点灯）时由应用层实现 lv_ui_led_switch_hook()
 */
#ifndef __LV_MAINSTART_H__
#define __LV_MAINSTART_H__

#include "lvgl.h"
#include <stdbool.h>

/* 构建 LVGL 验证界面（在 lv_init + lv_port_disp_init + lv_port_indev_init 之后调用） */
void lv_mainstart(void);

/* 界面 -> 应用层的通知钩子：界面上的开关状态变化时回调
 * 注意：声明不带 weak（弱属性只加在 lv_mainstart.c 的定义上），
 * 否则应用层的覆盖定义会被 armcc 传播为弱符号，链接时报重定义
 * 驱动侧弱实现为空操作，应用层给出强定义即可联动真实硬件 */
void lv_ui_led_switch_hook(bool on);

#endif /* __LV_MAINSTART_H__ */
