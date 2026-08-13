/**
 * @file    sram.h
 * @brief   外部 SRAM 驱动接口（FSMC 16 位总线）
 * @note    硬件配置见 bsp/bsp_sram.h
 */
#ifndef __SRAM_H__
#define __SRAM_H__

#include "bsp_sram.h"

extern SRAM_HandleTypeDef g_sram_handler;

void sram_init(void);
void sram_write(uint8_t *pbuf, uint32_t addr, uint32_t datalen);
void sram_read(uint8_t *pbuf, uint32_t addr, uint32_t datalen);

uint8_t sram_test_read(uint32_t addr);
void sram_test_write(uint32_t addr, uint8_t data);

#endif /* __SRAM_H__ */
