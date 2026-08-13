/**
 * @file    lcd.h
 * @brief   TFTLCD 驱动接口（支持 ILI9341/NT35310/NT35510/SSD1963/ST7789/ST7796/ILI9806）
 * @note    硬件配置见 bsp/bsp_lcd.h
 */
#ifndef __LCD_H__
#define __LCD_H__

#include "bsp_lcd.h"

/* LCD 关键参数 */
typedef struct
{
    uint16_t width;     /* 宽度 */
    uint16_t height;    /* 高度 */
    uint16_t id;        /* 驱动 IC ID */
    uint8_t  dir;       /* 显示方向：0 竖屏，1 横屏 */
    uint16_t wramcmd;   /* 写 GRAM 指令 */
    uint16_t setxcmd;   /* 设置 X 坐标指令 */
    uint16_t setycmd;   /* 设置 Y 坐标指令 */
} _lcd_dev;

extern _lcd_dev lcddev;             /* LCD 关键参数 */
extern uint32_t g_point_color;      /* 画笔颜色 */
extern uint32_t g_back_color;       /* 背景颜色 */

/* 扫描方向 */
#define L2R_U2D         0           /* 从左到右，从上到下 */
#define L2R_D2U         1
#define R2L_U2D         2
#define R2L_D2U         3
#define U2D_L2R         4
#define U2D_R2L         5
#define D2U_L2R         6
#define D2U_R2L         7

#define DFT_SCAN_DIR    L2R_U2D     /* 默认扫描方向 */

/* 常用颜色（RGB565） */
#define WHITE           0xFFFF
#define BLACK           0x0000
#define RED             0xF800
#define GREEN           0x07E0
#define BLUE            0x001F
#define MAGENTA         0XF81F
#define YELLOW          0XFFE0
#define CYAN            0X07FF

/* 非常用颜色 */
#define BROWN           0XBC40
#define BRRED           0XFC07
#define GRAY            0X8430
#define DARKBLUE        0X01CF
#define LIGHTBLUE       0X7D7C
#define GRAYBLUE        0X5458
#define LIGHTGREEN      0X841F
#define LGRAY           0XC618
#define LGRAYBLUE       0XA651
#define LBBLUE          0X2B12

/* SSD1963 分辨率参数（一般不修改） */
#define SSD_HOR_RESOLUTION      800
#define SSD_VER_RESOLUTION      480

#define SSD_HOR_PULSE_WIDTH     1
#define SSD_HOR_BACK_PORCH      46
#define SSD_HOR_FRONT_PORCH     210

#define SSD_VER_PULSE_WIDTH     1
#define SSD_VER_BACK_PORCH      23
#define SSD_VER_FRONT_PORCH     22

#define SSD_HT          (SSD_HOR_RESOLUTION + SSD_HOR_BACK_PORCH + SSD_HOR_FRONT_PORCH)
#define SSD_HPS         (SSD_HOR_BACK_PORCH)
#define SSD_VT          (SSD_VER_RESOLUTION + SSD_VER_BACK_PORCH + SSD_VER_FRONT_PORCH)
#define SSD_VPS         (SSD_VER_BACK_PORCH)

/* 底层接口 */
void lcd_wr_data(volatile uint16_t data);            /* 写数据 */
void lcd_wr_regno(volatile uint16_t regno);          /* 写寄存器编号/地址 */
void lcd_write_reg(uint16_t regno, uint16_t data);   /* 写寄存器值 */

/* 初始化与显示控制 */
void lcd_init(void);
void lcd_display_on(void);
void lcd_display_off(void);
void lcd_scan_dir(uint8_t dir);
void lcd_display_dir(uint8_t dir);
void lcd_ssd_backlight_set(uint8_t pwm);             /* SSD1963 背光 */

/* 绘图接口 */
void lcd_write_ram_prepare(void);
void lcd_set_cursor(uint16_t x, uint16_t y);
uint32_t lcd_read_point(uint16_t x, uint16_t y);
void lcd_draw_point(uint16_t x, uint16_t y, uint32_t color);
void lcd_clear(uint16_t color);
void lcd_fill_circle(uint16_t x, uint16_t y, uint16_t r, uint16_t color);
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint16_t color);
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color);
void lcd_set_window(uint16_t sx, uint16_t sy, uint16_t width, uint16_t height);
void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color);
void lcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color);
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void lcd_draw_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);

/* 字符/数字/字符串显示（size 支持 12/16/24/32） */
void lcd_show_char(uint16_t x, uint16_t y, char chr, uint8_t size, uint8_t mode, uint16_t color);
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint16_t color);
void lcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint16_t color);
void lcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint16_t color);

#endif /* __LCD_H__ */
