/**
 * @file    bsp_sram.h
 * @brief   外部 SRAM（FSMC）板级硬件配置
 * @note    软硬件分离：驱动层不感知引脚，换板/换外设只需修改本文件
 */
#ifndef __BSP_SRAM_H__
#define __BSP_SRAM_H__

#include "stm32f1xx.h"

/* 控制信号引脚 */
#define SRAM_WR_GPIO_PORT               GPIOD
#define SRAM_WR_GPIO_PIN                GPIO_PIN_5
#define SRAM_WR_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOD_CLK_ENABLE(); }while(0)

#define SRAM_RD_GPIO_PORT               GPIOD
#define SRAM_RD_GPIO_PIN                GPIO_PIN_4
#define SRAM_RD_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOD_CLK_ENABLE(); }while(0)

/* 片选引脚（与 SRAM_FSMC_NEX 配套，改片选需同步修改两处） */
#define SRAM_CS_GPIO_PORT               GPIOG
#define SRAM_CS_GPIO_PIN                GPIO_PIN_10
#define SRAM_CS_GPIO_CLK_ENABLE()       do{ __HAL_RCC_GPIOG_CLK_ENABLE(); }while(0)

/* FSMC 数据/地址总线引脚（FSMC_Dx/FSMC_Ax 复用） */
#define SRAM_FSMC_D_GPIO_PORT           GPIOD
#define SRAM_FSMC_D_GPIO_PINS           (GPIO_PIN_0  | GPIO_PIN_1  | GPIO_PIN_8  | GPIO_PIN_9  | \
                                         GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | \
                                         GPIO_PIN_14 | GPIO_PIN_15)

#define SRAM_FSMC_E_GPIO_PORT           GPIOE
#define SRAM_FSMC_E_GPIO_PINS           (GPIO_PIN_0  | GPIO_PIN_1  | GPIO_PIN_7  | GPIO_PIN_8  | \
                                         GPIO_PIN_9  | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | \
                                         GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15)

#define SRAM_FSMC_F_GPIO_PORT           GPIOF
#define SRAM_FSMC_F_GPIO_PINS           (GPIO_PIN_0  | GPIO_PIN_1  | GPIO_PIN_2  | GPIO_PIN_3  | \
                                         GPIO_PIN_4  | GPIO_PIN_5  | GPIO_PIN_12 | GPIO_PIN_13 | \
                                         GPIO_PIN_14 | GPIO_PIN_15)

#define SRAM_FSMC_G_GPIO_PORT           GPIOG
#define SRAM_FSMC_G_GPIO_PINS           (GPIO_PIN_0  | GPIO_PIN_1  | GPIO_PIN_2  | GPIO_PIN_3  | \
                                         GPIO_PIN_4  | GPIO_PIN_5)

/* FSMC 片选：SRAM 挂在 FSMC_NE3，取值范围 1~4 */
#define SRAM_FSMC_NEX                   3

#define SRAM_FSMC_BCRX          FSMC_Bank1->BTCR[(SRAM_FSMC_NEX - 1) * 2]
#define SRAM_FSMC_BTRX          FSMC_Bank1->BTCR[(SRAM_FSMC_NEX - 1) * 2 + 1]
#define SRAM_FSMC_BWTRX         FSMC_Bank1E->BWTR[(SRAM_FSMC_NEX - 1) * 2]

/* 基地址：BANK1 四区各 64MB
 * NE1: 0x60000000  NE2: 0x64000000  NE3: 0x68000000  NE4: 0x6C000000 */
#define SRAM_BASE_ADDR         (0X60000000 + (0X4000000 * (SRAM_FSMC_NEX - 1)))

#endif /* __BSP_SRAM_H__ */
