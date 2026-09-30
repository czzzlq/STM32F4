/**
 * @file    touch.c
 * @brief   触摸屏驱动（电阻屏 XPT2046/ADS7846 位操作 SPI + 电容屏 GT9xxx/FT5206）
 * @note    电容屏走 ctiic 软件 IIC，无需校准；电阻屏走 GPIO 模拟 SPI，
 *          需要五点校准，校准参数（xfac/yfac/xc/yc）保存在 AT24CXX 中。
 *          扫描接口沿用 tp_dev.scan(0)=屏幕坐标 / (1)=物理坐标 的约定，
 *          以便 tp_dev_t 的函数指针表在两类屏之间无差别调用
 */

#include "touch.h"
#include "delay.h"
#include "24cxx.h"
#include "lcd.h"
#include "gt9xxx.h"
#include "ft5206.h"
#include <stdio.h>
#include <stdlib.h>     /* abs() */

/* 内部函数前置声明：tp_dev 的函数指针表在文件开头就要引用 tp_scan，
 * 而 tp_scan 是文件内私有实现（不再暴露到 touch.h） */
static uint8_t tp_scan(uint8_t mode);

tp_dev_t tp_dev =
{
    tp_init,
    tp_scan,
    tp_adjust,
    {0},            /* x[] */
    {0},            /* y[] */
    0,              /* sta */
    0.0f,           /* xfac */
    0.0f,           /* yfac */
    0,              /* xc */
    0,              /* yc */
    0               /* touchtype */
};

/**
 * @brief       SPI写数据
 *   @note      向触摸屏IC写入1 byte数据
 * @param       data: 要写入的数据
 * @retval      无
 */
static void tp_write_byte(uint8_t data)
{
    uint8_t count = 0;

    for (count = 0; count < 8; count++)
    {
        if (data & 0x80)    /* 高位先发 */
        {
            T_MOSI(1);
        }
        else
        {
            T_MOSI(0);
        }

        data <<= 1;
        T_CLK(0);
        systickDelay_us(1);
        T_CLK(1);           /* 上升沿有效 */
    }
}

/**
 * @brief       SPI读数据
 *   @note      从触摸屏IC读取adc值
 * @param       cmd: 指令
 * @retval      读取到的数据,ADC值(12bit)
 */
static uint16_t tp_read_ad(uint8_t cmd)
{
    uint8_t count = 0;
    uint16_t num = 0;

    T_CLK(0);           /* 先拉低时钟 */
    T_MOSI(0);          /* 拉低数据线 */
    T_CS(0);            /* 选中触摸屏IC */
    tp_write_byte(cmd); /* 发送命令字 */
    systickDelay_us(6); /* ADS7846的转换时间最长为6us */
    T_CLK(0);
    systickDelay_us(1);
    T_CLK(1);           /* 给1个时钟，清除BUSY */
    systickDelay_us(1);
    T_CLK(0);

    for (count = 0; count < 16; count++)    /* 读出16位数据,只有高12位有效 */
    {
        num <<= 1;
        T_CLK(0);       /* 下降沿有效 */
        systickDelay_us(1);
        T_CLK(1);

        if (T_MISO)
        {
            num++;
        }
    }

    num >>= 4;          /* 只有高12位有效. */
    T_CS(1);            /* 释放片选 */
    return num;
}

/* 电阻触摸驱动芯片 数据采集 滤波用参数 */
#define TP_READ_TIMES   5       /* 读取次数 */
#define TP_LOST_VAL     1       /* 两端各丢弃的极值个数 */

/* 下面的单遍实现利用了「只丢一个最小值和一个最大值」这一等价关系，
 * 改动 TP_LOST_VAL 必须同步改回排序实现，否则结果错误 */
#if (TP_LOST_VAL != 1)
#error "tp_read_xoy 的单遍去极值算法仅支持 TP_LOST_VAL == 1"
#endif

/**
 * @brief       读取一个坐标值(x或者y)
 *   @note      连续读取 TP_READ_TIMES 次，去掉最低和最高各 TP_LOST_VAL 个后取平均。
 *              原实现先排序再去极值，这里改为一遍扫描同时求 sum/min/max：
 *              结果与「升序排序后去掉两端」完全等价，但省掉了 5 元素临时数组
 *              与 O(n^2) 排序（中断/热路径上每次都调用，收益虽小但为纯收益）
 *
 * @param       cmd : 指令
 *   @arg       0XD0: 读取X轴坐标(@竖屏状态,横屏状态和Y对调.)
 *   @arg       0X90: 读取Y轴坐标(@竖屏状态,横屏状态和X对调.)
 *
 * @retval      读取到的数据(滤波后的), ADC值(12bit)
 */
static uint16_t tp_read_xoy(uint8_t cmd)
{
    uint16_t i;
    uint16_t sum = 0;
    uint16_t minVal;
    uint16_t maxVal;
    uint16_t value = tp_read_ad(cmd);

    minVal = value;
    maxVal = value;
    sum = value;

    for (i = 1; i < TP_READ_TIMES; i++)
    {
        value = tp_read_ad(cmd);
        sum += value;

        if (value < minVal)
        {
            minVal = value;
        }
        else if (value > maxVal)
        {
            maxVal = value;
        }
    }

    /* 去掉一个最小值和一个最大值后取平均（TP_LOST_VAL == 1） */
    return (uint16_t)((sum - minVal - maxVal) / (TP_READ_TIMES - 2 * TP_LOST_VAL));
}

/**
 * @brief       读取x, y坐标
 * @param       x,y: 读取到的坐标值
 * @retval      无
 */
static void tp_read_xy(uint16_t *x, uint16_t *y)
{
    uint16_t xval, yval;

    if (tp_dev.touchtype & 0x01)    /* X,Y方向与屏幕相反 */
    {
        xval = tp_read_xoy(0x90);   /* 读取X轴坐标AD值, 并进行方向变换 */
        yval = tp_read_xoy(0xD0);   /* 读取Y轴坐标AD值 */
    }
    else                            /* X,Y方向与屏幕相同 */
    {
        xval = tp_read_xoy(0xD0);   /* 读取X轴坐标AD值 */
        yval = tp_read_xoy(0x90);   /* 读取Y轴坐标AD值 */
    }

    *x = xval;
    *y = yval;
}

/* 连续两次读取X,Y坐标的数据误差最大允许值 */
#define TP_ERR_RANGE    50      /* 误差范围 */

/**
 * @brief       连续读取2次触摸IC数据, 并滤波
 *   @note      连续2次读取触摸屏IC,且这两次的偏差不能超过TP_ERR_RANGE,满足
 *              条件,则认为读数正确,否则读数错误.该函数能大大提高准确度.
 *
 * @param       x,y: 读取到的坐标值
 * @retval      0, 失败; 1, 成功;
 */
static uint8_t tp_read_xy2(uint16_t *x, uint16_t *y)
{
    uint16_t x1, y1;
    uint16_t x2, y2;

    tp_read_xy(&x1, &y1);   /* 读取第一次数据 */
    tp_read_xy(&x2, &y2);   /* 读取第二次数据 */

    /* 前后两次采样在+-TP_ERR_RANGE内 */
    if (((x2 <= x1 && x1 < x2 + TP_ERR_RANGE) || (x1 <= x2 && x2 < x1 + TP_ERR_RANGE)) &&
            ((y2 <= y1 && y1 < y2 + TP_ERR_RANGE) || (y1 <= y2 && y2 < y1 + TP_ERR_RANGE)))
    {
        *x = (x1 + x2) / 2;
        *y = (y1 + y2) / 2;
        return 1;
    }

    return 0;
}

/******************************************************************************************/
/* 与LCD部分有关的函数, 用来校准用的 */

/**
 * @brief       画一个校准用的触摸点(十字架)
 * @param       x,y   : 坐标
 *   @arg       color : 颜色
 * @retval      无
 */
static void tp_draw_touch_point(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_draw_line(x - 12, y, x + 13, y, color); /* 横线 */
    lcd_draw_line(x, y - 12, x, y + 13, color); /* 竖线 */
    lcd_draw_point(x + 1, y + 1, color);
    lcd_draw_point(x - 1, y + 1, color);
    lcd_draw_point(x + 1, y - 1, color);
    lcd_draw_point(x - 1, y - 1, color);
    lcd_draw_circle(x, y, 6, color);            /* 画中心圈 */
}

/**
 * @brief       画一个大点(2*2的点)
 * @param       x,y   : 坐标
 *   @arg       color : 颜色
 * @retval      无
 */
void tp_draw_big_point(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_draw_point(x, y, color);       /* 中心点 */
    lcd_draw_point(x + 1, y, color);
    lcd_draw_point(x, y + 1, color);
    lcd_draw_point(x + 1, y + 1, color);
}

/******************************************************************************************/

/**
 * @brief       把浮点坐标夹取到 [0, max] 后转为整数
 * @param       v   : 待转换的浮点坐标
 *   @arg       max : 允许的最大坐标（通常为屏幕边长 - 1）
 * @retval      夹取后的坐标
 * @note        直接对负值或超出 uint16_t 的值做强转在 C 中是未定义行为
 *              （原实现用隐式转换，负值会回绕成 6xxxx 之类的野值），
 *              这里先夹取再转换，保证任何输入都有确定结果
 */
static uint16_t tp_clamp_coord(float v, uint16_t max)
{
    if (v <= 0.0f)
    {
        return 0;
    }

    if (v >= (float)max)
    {
        return max;
    }

    return (uint16_t)v;
}

/**
 * @brief       触摸按键扫描（电阻屏）
 * @param       mode: 坐标模式
 *   @arg       0, 屏幕坐标;
 *   @arg       1, 物理坐标(校准等特殊场合用)
 *
 * @retval      0, 触屏无触摸; 1, 触屏有触摸;
 * @note        坐标换算依赖 tp_dev.xfac/yfac，若因 EEPROM 数据异常导致比例为 0，
 *              本次扫描按「无触摸」返回，避免除 0 得到 inf 后写出越界坐标
 */
static uint8_t tp_scan(uint8_t mode)
{
    if (T_PEN == 0)     /* 有按键按下 */
    {
        if (mode)       /* 读取物理坐标, 无需转换 */
        {
            if (tp_read_xy2(&tp_dev.x[0], &tp_dev.y[0]) == 0)
            {
                return 0;
            }
        }
        else if (tp_read_xy2(&tp_dev.x[0], &tp_dev.y[0]))     /* 读取屏幕坐标, 需要转换 */
        {
            if ((tp_dev.xfac == 0.0f) || (tp_dev.yfac == 0.0f))
            {
                return 0;   /* 校准参数无效，放弃本次坐标（由上层重新校准） */
            }

            /* 物理坐标 -> 逻辑坐标：先与中心物理值作差（有符号），再按比例缩放 */
            tp_dev.x[0] = tp_clamp_coord(((float)((int16_t)tp_dev.x[0] - tp_dev.xc) / tp_dev.xfac)
                                         + (float)lcddev.width / 2.0f, lcddev.width - 1);
            tp_dev.y[0] = tp_clamp_coord(((float)((int16_t)tp_dev.y[0] - tp_dev.yc) / tp_dev.yfac)
                                         + (float)lcddev.height / 2.0f, lcddev.height - 1);
        }
        else
        {
            return 0;       /* 两次采样偏差过大，本次坐标不可信 */
        }

        if ((tp_dev.sta & TP_PRES_DOWN) == 0)   /* 之前没有被按下 */
        {
            tp_dev.sta = TP_PRES_DOWN | TP_CATH_PRES;   /* 按键按下 */
            tp_dev.x[CT_MAX_TOUCH - 1] = tp_dev.x[0];   /* 记录第一次按下时的坐标 */
            tp_dev.y[CT_MAX_TOUCH - 1] = tp_dev.y[0];
        }
    }
    else
    {
        if (tp_dev.sta & TP_PRES_DOWN)      /* 之前是被按下的 */
        {
            tp_dev.sta &= ~TP_PRES_DOWN;    /* 标记按键松开 */
        }
        else     /* 之前就没有被按下 */
        {
            tp_dev.x[CT_MAX_TOUCH - 1] = 0;
            tp_dev.y[CT_MAX_TOUCH - 1] = 0;
            tp_dev.x[0] = 0xFFFF;
            tp_dev.y[0] = 0xFFFF;
        }
    }

    return tp_dev.sta & TP_PRES_DOWN; /* 返回当前的触屏状态 */
}

/* TP_SAVE_ADDR_BASE 定义触摸屏校准参数保存在 EEPROM 里面的起始地址 */
#define TP_SAVE_ADDR_BASE   40      /* 起始地址 */
#define TP_SAVE_DATA_SIZE   12      /* xfac(4) + yfac(4) + xc(2) + yc(2) */
#define TP_SAVE_MARK        0x0A    /* 校准有效标记 */

/**
 * @brief       保存校准参数
 *   @note      参数保存在 EEPROM(24C02) 中，起始地址 TP_SAVE_ADDR_BASE，
 *              共占用 13 字节（12 字节参数 + 1 字节有效标记）。
 *              直接按内存布局写入浮点/整型成员，读取端用同样的布局还原，
 *              因此不依赖具体类型的字节序与对齐细节
 * @retval      无
 */
void tp_save_adjust_data(void)
{
    uint8_t *p = (uint8_t *)&tp_dev.xfac;   /* 指向首地址 */

    /* p+0 起依次是 xfac, yfac, xc, yc */
    at24cxx_write(TP_SAVE_ADDR_BASE, p, TP_SAVE_DATA_SIZE);
    at24cxx_write_one_byte(TP_SAVE_ADDR_BASE + TP_SAVE_DATA_SIZE, TP_SAVE_MARK);
}

/**
 * @brief       获取保存在EEPROM里面的校准值
 * @retval      0，获取失败，要重新校准
 *              1，成功获取数据
 * @note        除有效标记外还要求比例因子非 0：EEPROM 空片或被擦除时
 *              标记位可能凑巧为 0x0A，但比例为 0 会让坐标换算除 0
 */
uint8_t tp_get_adjust_data(void)
{
    uint8_t *p = (uint8_t *)&tp_dev.xfac;
    uint8_t marker = 0;

    if (at24cxx_read(TP_SAVE_ADDR_BASE, p, TP_SAVE_DATA_SIZE) != STATUS_OK)
    {
        return 0;   /* EEPROM 无应答 */
    }

    if (at24cxx_read_one_byte(TP_SAVE_ADDR_BASE + TP_SAVE_DATA_SIZE, &marker) != STATUS_OK)
    {
        return 0;
    }

    if (marker != TP_SAVE_MARK)
    {
        return 0;
    }

    if ((tp_dev.xfac == 0.0f) || (tp_dev.yfac == 0.0f))
    {
        return 0;
    }

    return 1;
}

/* 校准提示字符串（lcd_show_string 只读该内容，故用 const 放在 .rodata） */
static const char *const s_tpRemindMsg =
    "Please use the stylus click the cross on the screen."
    "The cross will always move until the screen adjustment is completed.";

/**
 * @brief       提示校准结果(各个参数)
 * @param       xy[5][2]: 5个物理坐标值
 *   @arg       px,py   : x,y方向的比例因子(约接近1越好)
 * @retval      无
 */
static void tp_adjust_info_show(uint16_t xy[5][2], float px, float py)
{
    uint8_t i;
    char sbuf[20];

    for (i = 0; i < 5; i++)   /* 显示5个物理坐标值 */
    {
        snprintf(sbuf, sizeof(sbuf), "x%d:%d", i + 1, xy[i][0]);
        lcd_show_string(40, 160 + (i * 20), lcddev.width, lcddev.height, 16, sbuf, RED);
        snprintf(sbuf, sizeof(sbuf), "y%d:%d", i + 1, xy[i][1]);
        lcd_show_string(40 + 80, 160 + (i * 20), lcddev.width, lcddev.height, 16, sbuf, RED);
    }

    /* 显示X/Y方向的比例因子 */
    lcd_fill(40, 160 + (i * 20), lcddev.width - 1, 16, WHITE);  /* 清除之前的px,py显示 */
    snprintf(sbuf, sizeof(sbuf), "px:%0.2f", px);
    lcd_show_string(40, 160 + (i * 20), lcddev.width, lcddev.height, 16, sbuf, RED);
    snprintf(sbuf, sizeof(sbuf), "py:%0.2f", py);
    lcd_show_string(40 + 80, 160 + (i * 20), lcddev.width, lcddev.height, 16, sbuf, RED);
}

/**
 * @brief       触摸屏校准代码
 *   @note      使用五点校准法
 *              本函数得到x轴/y轴比例因子xfac/yfac及物理中心坐标值(xc,yc)等4个参数
 *              我们规定: 物理坐标即AD采集到的坐标值,范围是0~4095.
 *                        逻辑坐标即LCD屏幕的坐标, 范围为LCD屏幕的分辨率.
 *
 * @retval      无
 * @note        连续 10 秒没有有效按下则放弃本次校准，改为尝试读取已有参数。
 *              比例因子全程用 float：Cortex-M4F 只有单精度 FPU，double 会退化
 *              成软件浮点，而 AD 值只有 12 位，单精度完全够用
 */
void tp_adjust(void)
{
    uint16_t pxy[5][2];     /* 物理坐标缓存值 */
    uint8_t  cnt = 0;
    short s1, s2, s3, s4;   /* 4个点的坐标差值 */
    float px = 0.0f;        /* X轴物理坐标比例,用于判定是否校准成功 */
    float py = 0.0f;        /* Y轴物理坐标比例 */
    uint16_t outtime = 0;

    lcd_clear(WHITE);       /* 清屏 */
    lcd_show_string(40, 40, 160, 100, 16, s_tpRemindMsg, RED);  /* 显示提示信息 */
    tp_draw_touch_point(20, 20, RED);   /* 画点1 */
    tp_dev.sta = 0;         /* 消除触发信号 */

    while (1)               /* 如果连续10秒钟没有按下,则自动退出 */
    {
        tp_dev.scan(1);     /* 扫描物理坐标 */

        if ((tp_dev.sta & 0xC000) == TP_CATH_PRES)   /* 按键按下了一次(此时按键松开了.) */
        {
            outtime = 0;
            tp_dev.sta &= ~TP_CATH_PRES;    /* 标记按键已经被处理过了. */

            pxy[cnt][0] = tp_dev.x[0];      /* 保存X物理坐标 */
            pxy[cnt][1] = tp_dev.y[0];      /* 保存Y物理坐标 */
            cnt++;

            switch (cnt)
            {
                case 1:
                    tp_draw_touch_point(20, 20, WHITE);                 /* 清除点1 */
                    tp_draw_touch_point(lcddev.width - 20, 20, RED);    /* 画点2 */
                    break;

                case 2:
                    tp_draw_touch_point(lcddev.width - 20, 20, WHITE);  /* 清除点2 */
                    tp_draw_touch_point(20, lcddev.height - 20, RED);   /* 画点3 */
                    break;

                case 3:
                    tp_draw_touch_point(20, lcddev.height - 20, WHITE); /* 清除点3 */
                    tp_draw_touch_point(lcddev.width - 20, lcddev.height - 20, RED);    /* 画点4 */
                    break;

                case 4:
                    lcd_clear(WHITE);   /* 画第五个点了, 直接清屏 */
                    tp_draw_touch_point(lcddev.width / 2, lcddev.height / 2, RED);  /* 画点5 */
                    break;

                case 5:     /* 全部5个点已经得到 */
                    s1 = pxy[1][0] - pxy[0][0]; /* 第2个点和第1个点的X轴物理坐标差值(AD值) */
                    s3 = pxy[3][0] - pxy[2][0]; /* 第4个点和第3个点的X轴物理坐标差值(AD值) */
                    s2 = pxy[3][1] - pxy[1][1]; /* 第4个点和第2个点的Y轴物理坐标差值(AD值) */
                    s4 = pxy[2][1] - pxy[0][1]; /* 第3个点和第1个点的Y轴物理坐标差值(AD值) */

                    /* 先在整数域做合法性检查：差值非 0 且在 12 位 AD 量程内。
                     * 这样后面的除法一定安全，也避免了先算出 inf 再判定的写法 */
                    if ((s1 != 0) && (s2 != 0) && (s3 != 0) && (s4 != 0) &&
                        (abs(s1) <= 4095) && (abs(s2) <= 4095) &&
                        (abs(s3) <= 4095) && (abs(s4) <= 4095))
                    {
                        px = (float)s1 / (float)s3;     /* X轴比例因子 */
                        py = (float)s2 / (float)s4;     /* Y轴比例因子 */

                        if (px < 0.0f)
                        {
                            px = -px;                   /* 负数改正数 */
                        }

                        if (py < 0.0f)
                        {
                            py = -py;                   /* 负数改正数 */
                        }
                    }

                    if ((px < 0.95f) || (px > 1.05f) || (py < 0.95f) || (py > 1.05f))   /* 比例不合格 */
                    {
                        cnt = 0;
                        tp_draw_touch_point(lcddev.width / 2, lcddev.height / 2, WHITE); /* 清除点5 */
                        tp_draw_touch_point(20, 20, RED);   /* 重新画点1 */
                        tp_adjust_info_show(pxy, px, py);   /* 显示当前信息,方便找问题 */
                        continue;
                    }

                    tp_dev.xfac = (float)(s1 + s3) / (2.0f * (float)(lcddev.width - 40));
                    tp_dev.yfac = (float)(s2 + s4) / (2.0f * (float)(lcddev.height - 40));

                    tp_dev.xc = pxy[4][0];      /* X轴,物理中心坐标 */
                    tp_dev.yc = pxy[4][1];      /* Y轴,物理中心坐标 */

                    lcd_clear(WHITE);   /* 清屏 */
                    lcd_show_string(35, 110, lcddev.width, lcddev.height, 16,
                                    "Touch Screen Adjust OK!", BLUE);   /* 校正完成 */
                    systickDelay_ms(1000);
                    tp_save_adjust_data();

                    lcd_clear(WHITE);   /* 清屏 */
                    return; /* 校正完成 */
            }
        }

        systickDelay_ms(10);
        outtime++;

        if (outtime > 1000)
        {
            tp_get_adjust_data();
            break;
        }
    }
}

/**
 * @brief       触摸屏初始化
 * @retval      0,没有进行校准
 *              1,进行过校准
 * @note        先按 LCD 驱动 IC 的 ID 推断触摸 IC 类型，识别成功则把
 *              tp_dev.scan 指向对应驱动；都识别不出来才按电阻屏处理
 */
uint8_t tp_init(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};

    tp_dev.touchtype = 0;                   /* 默认设置(电阻屏 & 竖屏) */
    tp_dev.touchtype |= lcddev.dir & 0x01;  /* 根据LCD判定是横屏还是竖屏 */

    if (lcddev.id == 0x7796)    /* 3.5寸屏有两种，一种屏幕ID为0x5510带电阻触摸屏，一种屏幕ID为0x7796带GT型号的电容触摸屏 */
    {
        if (gt9xxx_init() == 0) /* 初始化GT系列触摸屏成功,即当前3.5寸屏为电容触摸屏 */
        {
            tp_dev.scan = gt9xxx_scan;  /* 扫描函数指向GT9147触摸屏扫描 */
            tp_dev.touchtype |= 0x80;   /* 电容屏 */
            return 0;
        }
    }

    if (lcddev.id == 0x5510 || lcddev.id == 0x9806 || lcddev.id == 0x4342 ||
        lcddev.id == 0x4384 || lcddev.id == 0x1018)     /* 电容触摸屏,4.3寸/10.1寸屏 */
    {
        gt9xxx_init();
        tp_dev.scan = gt9xxx_scan;  /* 扫描函数指向GT9147触摸屏扫描 */
        tp_dev.touchtype |= 0x80;   /* 电容屏 */
        return 0;
    }
    else if (lcddev.id == 0x1963 || lcddev.id == 0x7084 || lcddev.id == 0x7016) /* SSD1963 7寸屏或者 7寸800*480/1024*600 RGB屏 */
    {
        if (ft5206_init() == 0)     /* 触摸IC是FT系列的就执行ft5206_init函数以及使用ft5206_scan扫描函数 */
        {
            tp_dev.scan = ft5206_scan;  /* 扫描函数指向FT5206触摸屏扫描 */
        }
        else                        /* 触摸IC是GT系列的就执行gt9xxx_init函数以及使用gt9xxx_scan扫描函数 */
        {
            gt9xxx_init();
            tp_dev.scan = gt9xxx_scan;  /* 扫描函数指向GT9147触摸屏扫描 */
        }

        tp_dev.touchtype |= 0x80;   /* 电容屏 */
        return 0;
    }
    else
    {
        /* 走到这里按电阻屏处理：配置 XPT2046/ADS7846 的位操作 SPI 引脚 */
        T_PEN_GPIO_CLK_ENABLE();    /* T_PEN脚时钟使能 */
        T_CS_GPIO_CLK_ENABLE();     /* T_CS脚时钟使能 */
        T_MISO_GPIO_CLK_ENABLE();   /* T_MISO脚时钟使能 */
        T_MOSI_GPIO_CLK_ENABLE();   /* T_MOSI脚时钟使能 */
        T_CLK_GPIO_CLK_ENABLE();    /* T_CLK脚时钟使能 */

        gpio_init_struct.Pin = T_PEN_GPIO_PIN;
        gpio_init_struct.Mode = GPIO_MODE_INPUT;                 /* 输入 */
        gpio_init_struct.Pull = GPIO_PULLUP;                     /* 上拉 */
        gpio_init_struct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;      /* 高速 */
        HAL_GPIO_Init(T_PEN_GPIO_PORT, &gpio_init_struct);       /* 初始化T_PEN引脚 */

        gpio_init_struct.Pin = T_MISO_GPIO_PIN;
        HAL_GPIO_Init(T_MISO_GPIO_PORT, &gpio_init_struct);      /* 初始化T_MISO引脚 */

        gpio_init_struct.Pin = T_MOSI_GPIO_PIN;
        gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;             /* 推挽输出 */
        gpio_init_struct.Pull = GPIO_PULLUP;                     /* 上拉 */
        gpio_init_struct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;      /* 高速 */
        HAL_GPIO_Init(T_MOSI_GPIO_PORT, &gpio_init_struct);      /* 初始化T_MOSI引脚 */

        gpio_init_struct.Pin = T_CLK_GPIO_PIN;
        HAL_GPIO_Init(T_CLK_GPIO_PORT, &gpio_init_struct);       /* 初始化T_CLK引脚 */

        gpio_init_struct.Pin = T_CS_GPIO_PIN;
        HAL_GPIO_Init(T_CS_GPIO_PORT, &gpio_init_struct);        /* 初始化T_CS引脚 */

        tp_read_xy(&tp_dev.x[0], &tp_dev.y[0]); /* 第一次读取初始化 */
        at24cxx_init();         /* 初始化24CXX */

        if (tp_get_adjust_data())
        {
            return 0;           /* 已经校准 */
        }

        /* 未校准（或 EEPROM 数据无效）：立即进入五点校准并保存 */
        lcd_clear(WHITE);       /* 清屏 */
        tp_adjust();            /* 屏幕校准 */
        tp_save_adjust_data();

        tp_get_adjust_data();
    }

    return 1;
}
