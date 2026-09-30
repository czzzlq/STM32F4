/**
 * @file    ft5206.h
 * @brief   FT5206 / CST340 电容触摸 IC 驱动接口
 * @note    RST=PC13、INT=PB1；IIC 时序由 ctiic 提供（SCL=PB0、SDA=PF11）。
 *          寄存器地址为 8 位，与 GT9xxx 的 16 位地址不同，故单独一套读写函数
 */
#ifndef __FT5206_H
#define __FT5206_H

#include "common_types.h"
#include "stm32f4xx_hal.h"


/******************************************************************************************/
/* FT5206 INT 和 RST 引脚 定义 */

#define FT5206_RST_GPIO_PORT            GPIOC
#define FT5206_RST_GPIO_PIN             GPIO_PIN_13
#define FT5206_RST_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)   /* PC口时钟使能 */

#define FT5206_INT_GPIO_PORT            GPIOB
#define FT5206_INT_GPIO_PIN             GPIO_PIN_1
#define FT5206_INT_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)   /* PB口时钟使能 */

/******************************************************************************************/

/* 与电容触摸屏连接的芯片引脚(未包含IIC引脚) 
 * IO操作函数 
 */
#define FT5206_RST(x)     do{ (x) ? \
                              HAL_GPIO_WritePin(FT5206_RST_GPIO_PORT, FT5206_RST_GPIO_PIN, GPIO_PIN_SET) : \
                              HAL_GPIO_WritePin(FT5206_RST_GPIO_PORT, FT5206_RST_GPIO_PIN, GPIO_PIN_RESET); \
                          }while(0)       /* 复位引脚 */

#define FT5206_INT        HAL_GPIO_ReadPin(FT5206_INT_GPIO_PORT, FT5206_INT_GPIO_PIN)     /* 读取中断引脚 */

/* IIC读写命令 */
#define FT5206_CMD_WR               0x70        /* 写命令(最低位为0) */
#define FT5206_CMD_RD               0x71        /* 读命令(最低位为1) */

/* FT5206 部分寄存器定义  */
#define FT5206_DEVIDE_MODE          0x00        /* FT5206模式控制寄存器 */
#define FT5206_REG_NUM_FINGER       0x02        /* 触摸状态寄存器 */

#define FT5206_TP1_REG              0x03        /* 第一个触摸点数据地址 */
#define FT5206_TP2_REG              0x09        /* 第二个触摸点数据地址 */
#define FT5206_TP3_REG              0x0F        /* 第三个触摸点数据地址 */
#define FT5206_TP4_REG              0x15        /* 第四个触摸点数据地址 */
#define FT5206_TP5_REG              0x1B        /* 第五个触摸点数据地址 */

#define FT5206_ID_G_LIB_VERSION     0xA1        /* 版本 */
#define FT5206_ID_G_MODE            0xA4        /* FT5206中断模式控制寄存器 */
#define FT5206_ID_G_THGROUP         0x80        /* 触摸有效值设置寄存器 */
#define FT5206_ID_G_PERIODACTIVE    0x88        /* 激活状态周期设置寄存器 */

uint8_t ft5206_wr_reg(uint16_t reg, uint8_t *buf, uint8_t len); /* 写入数据，0 成功 / 1 失败 */
uint8_t ft5206_rd_reg(uint16_t reg, uint8_t *buf, uint8_t len); /* 读出数据，0 成功 / 1 失败 */
uint8_t ft5206_init(void);                                      /* 初始化，0 成功 / 1 失败 */
uint8_t ft5206_scan(uint8_t mode);                              /* 扫描触摸屏，返回是否有触摸 */

#endif /* __FT5206_H */
