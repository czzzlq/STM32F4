/**
 * @file    bsp_lcd.h
 * @brief   TFTLCD（FSMC 驱动）板级硬件配置
 * @note    软硬件分离：驱动层不感知引脚，换板/换外设只需修改本文件
 */
#ifndef __BSP_LCD_H__
#define __BSP_LCD_H__

#include "stm32f1xx.h"

/* 控制信号引脚 */
#define LCD_WR_GPIO_PORT                GPIOD
#define LCD_WR_GPIO_PIN                 GPIO_PIN_5
#define LCD_WR_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOD_CLK_ENABLE(); }while(0)

#define LCD_RD_GPIO_PORT                GPIOD
#define LCD_RD_GPIO_PIN                 GPIO_PIN_4
#define LCD_RD_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOD_CLK_ENABLE(); }while(0)

#define LCD_BL_GPIO_PORT                GPIOB
#define LCD_BL_GPIO_PIN                 GPIO_PIN_0
#define LCD_BL_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)

/* 片选引脚（与 LCD_FSMC_NEX 配套） */
#define LCD_CS_GPIO_PORT                GPIOG
#define LCD_CS_GPIO_PIN                 GPIO_PIN_12
#define LCD_CS_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOG_CLK_ENABLE(); }while(0)

/* 数据/命令选择引脚（RS，与 LCD_FSMC_AX 配套） */
#define LCD_RS_GPIO_PORT                GPIOG
#define LCD_RS_GPIO_PIN                 GPIO_PIN_0
#define LCD_RS_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOG_CLK_ENABLE(); }while(0)

/* FSMC 数据引脚（LCD 与 SRAM 共用 FSMC 总线） */
#define LCD_FSMC_D_GPIO_PORT            GPIOD
#define LCD_FSMC_D_GPIO_PINS            (GPIO_PIN_0  | GPIO_PIN_1  | GPIO_PIN_8  | \
                                         GPIO_PIN_9  | GPIO_PIN_10 | GPIO_PIN_14 | \
                                         GPIO_PIN_15)

#define LCD_FSMC_E_GPIO_PORT            GPIOE
#define LCD_FSMC_E_GPIO_PINS            (GPIO_PIN_7  | GPIO_PIN_8  | GPIO_PIN_9  | \
                                         GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | \
                                         GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15)

/* FSMC 片选：LCD 挂在 FSMC_NE4，取值范围 1~4 */
#define LCD_FSMC_NEX                    4
/* RS 接 FSMC_A10，取值范围 0~25 */
#define LCD_FSMC_AX                     10

#define LCD_FSMC_BCRX          FSMC_Bank1->BTCR[(LCD_FSMC_NEX - 1) * 2]
#define LCD_FSMC_BTRX          FSMC_Bank1->BTCR[(LCD_FSMC_NEX - 1) * 2 + 1]
#define LCD_FSMC_BWTRX         FSMC_Bank1E->BWTR[(LCD_FSMC_NEX - 1) * 2]

/* LCD 地址结构体：
 * 16 位数据宽度下，RS=0 写命令（LCD_REG），RS=1 写数据（LCD_RAM）
 * LCD_BASE = NEx 基地址 | (2^AX * 2 - 2)，REG 与 RAM 地址连续 */
typedef struct
{
    volatile uint16_t LCD_REG;
    volatile uint16_t LCD_RAM;
} LCD_TypeDef;

#define LCD_BASE        (uint32_t)((0X60000000 + (0X4000000 * (LCD_FSMC_NEX - 1))) | (((1 << LCD_FSMC_AX) * 2) - 2))
#define LCD             ((LCD_TypeDef *) LCD_BASE)

/* 背光控制 */
#define LCD_BL(x)   do{ x ? \
                      HAL_GPIO_WritePin(LCD_BL_GPIO_PORT, LCD_BL_GPIO_PIN, GPIO_PIN_SET) : \
                      HAL_GPIO_WritePin(LCD_BL_GPIO_PORT, LCD_BL_GPIO_PIN, GPIO_PIN_RESET); \
                     }while(0)

#endif /* __BSP_LCD_H__ */
