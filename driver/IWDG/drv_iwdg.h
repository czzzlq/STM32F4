/**
 * @file    drv_iwdg.h
 * @brief   独立看门狗驱动接口
 * @note    IWDG 一旦使能无法关闭，且必须在超时前喂狗，否则系统复位；
 *          默认不在 bsp_init 中自动启用，由应用层按需开启
 */
#ifndef __DRV_IWDG_H__
#define __DRV_IWDG_H__

#include "common_types.h"

/* 初始化看门狗（timeoutMs 为喂狗超时时间，LSI 约 40kHz） */
status_t drv_iwdg_init(uint32_t timeoutMs);
/* 喂狗 */
void drv_iwdg_feed(void);

#endif /* __DRV_IWDG_H__ */
