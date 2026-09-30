/**
 * @file    touch.h
 * @brief   触摸屏驱动接口（兼容电阻屏 XPT2046/ADS7846 与电容屏 GT9xxx/FT5206）
 * @note    引脚定义见本文件；电容屏的 IIC 时序见 ctiic.h。
 *          电阻屏需要五点校准，校准参数存放在 AT24CXX 中（见 24cxx.h）
 */
#ifndef __TOUCH_H
#define __TOUCH_H

#include "common_types.h"
#include "stm32f4xx_hal.h"

/******************************************************************************************/
/* 电阻触摸屏驱动IC T_PEN/T_CS/T_MISO/T_MOSI/T_SCK 引脚 定义 */

#define T_PEN_GPIO_PORT                 GPIOB
#define T_PEN_GPIO_PIN                  GPIO_PIN_1
#define T_PEN_GPIO_CLK_ENABLE()         do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)   /* 所在IO口时钟使能 */

#define T_CS_GPIO_PORT                  GPIOC
#define T_CS_GPIO_PIN                   GPIO_PIN_13
#define T_CS_GPIO_CLK_ENABLE()          do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)   /* 所在IO口时钟使能 */

#define T_MISO_GPIO_PORT                GPIOB
#define T_MISO_GPIO_PIN                 GPIO_PIN_2
#define T_MISO_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)   /* 所在IO口时钟使能 */

#define T_MOSI_GPIO_PORT                GPIOF
#define T_MOSI_GPIO_PIN                 GPIO_PIN_11
#define T_MOSI_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOF_CLK_ENABLE(); }while(0)   /* 所在IO口时钟使能 */

#define T_CLK_GPIO_PORT                 GPIOB
#define T_CLK_GPIO_PIN                  GPIO_PIN_0
#define T_CLK_GPIO_CLK_ENABLE()         do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)   /* 所在IO口时钟使能 */

/******************************************************************************************/

/* 电阻触摸屏控制引脚 */
#define T_PEN           HAL_GPIO_ReadPin(T_PEN_GPIO_PORT, T_PEN_GPIO_PIN)             /* T_PEN */
#define T_MISO          HAL_GPIO_ReadPin(T_MISO_GPIO_PORT, T_MISO_GPIO_PIN)           /* T_MISO */

/* 注意：宏参数必须加括号。这些宏展开成 "x ? A : B"，若调用处传入
 * 含 ?: 的表达式（如 T_MOSI(a ? 1 : 0)），会因 ?: 右结合而改变含义 */

#define T_MOSI(x)     do{ (x) ? \
                          HAL_GPIO_WritePin(T_MOSI_GPIO_PORT, T_MOSI_GPIO_PIN, GPIO_PIN_SET) : \
                          HAL_GPIO_WritePin(T_MOSI_GPIO_PORT, T_MOSI_GPIO_PIN, GPIO_PIN_RESET); \
                      }while(0)     /* T_MOSI */

#define T_CLK(x)      do{ (x) ? \
                          HAL_GPIO_WritePin(T_CLK_GPIO_PORT, T_CLK_GPIO_PIN, GPIO_PIN_SET) : \
                          HAL_GPIO_WritePin(T_CLK_GPIO_PORT, T_CLK_GPIO_PIN, GPIO_PIN_RESET); \
                      }while(0)     /* T_CLK */

#define T_CS(x)       do{ (x) ? \
                          HAL_GPIO_WritePin(T_CS_GPIO_PORT, T_CS_GPIO_PIN, GPIO_PIN_SET) : \
                          HAL_GPIO_WritePin(T_CS_GPIO_PORT, T_CS_GPIO_PIN, GPIO_PIN_RESET); \
                      }while(0)     /* T_CS */


#define TP_PRES_DOWN    0x8000  /* 笔按下标记 */
#define TP_CATH_PRES    0x4000  /* 捕获到一次按下（用于校准取点） */
#define CT_MAX_TOUCH    10      /* 电容屏最多支持的点数（x[]/y[] 数组长度） */

/* 触摸屏控制器 */
typedef struct
{
    uint8_t (*init)(void);      /* 初始化触摸屏控制器 */
    uint8_t (*scan)(uint8_t);   /* 扫描触摸屏.0,屏幕扫描;1,物理坐标; */
    void (*adjust)(void);       /* 触摸屏校准 */
    uint16_t x[CT_MAX_TOUCH];   /* 当前坐标 */
    uint16_t y[CT_MAX_TOUCH];   /* 电容屏有最多10组坐标;电阻屏用 x[0]/y[0] 表示本次扫描坐标,
                                 * 用 x[CT_MAX_TOUCH-1]/y[CT_MAX_TOUCH-1] 保存本次按下时的首个坐标 */

    uint16_t sta;               /* 笔的状态
                                 * b15:按下1/松开0;
                                 * b14:0,没有按键按下;1,有按键按下.
                                 * b13~b10:保留
                                 * b9~b0:电容触摸屏按下的点数(0,表示未按下,1表示按下)
                                 */

    /* 5点校准触摸屏校准参数(电容屏不需要校准) */
    float xfac;                 /* 5点校准法x方向比例因子 */
    float yfac;                 /* 5点校准法y方向比例因子 */
    short xc;                   /* 中心X坐标物理值(AD值) */
    short yc;                   /* 中心Y坐标物理值(AD值) */

    /* 新增的参数,当触摸屏的左右上下完全颠倒时需要用到.
     * b0:0, 竖屏(适合左右为X坐标,上下为Y坐标的TP)
     *    1, 横屏(适合左右为Y坐标,上下为X坐标的TP)
     * b1~6: 保留.
     * b7:0, 电阻屏
     *    1, 电容屏
     */
    uint8_t touchtype;
} tp_dev_t;

extern tp_dev_t tp_dev;         /* 触屏控制器在touch.c里面定义 */


/* 对外接口 */
uint8_t tp_init(void);                  /* 初始化（自动识别触摸 IC），1 表示做过校准 */
void tp_adjust(void);                   /* 触摸屏五点校准 */
void tp_save_adjust_data(void);         /* 保存校准参数到 EEPROM */
uint8_t tp_get_adjust_data(void);       /* 从 EEPROM 读取校准参数，1 表示有效 */
void tp_draw_big_point(uint16_t x, uint16_t y, uint16_t color); /* 画一个大点 */

#endif /* __TOUCH_H */
