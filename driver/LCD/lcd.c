/**
 * @file    lcd.c
 * @brief   TFTLCD 驱动（FSMC 16 位总线）
 * @note    1. 硬件配置见 bsp/bsp_lcd.h，换板/换屏只需改 BSP 层
 *          2. 支持 ILI9341/NT35310/NT35510/SSD1963/ST7789/ST7796/ILI9806，
 *             寄存器初始化序列见 lcd_ex.c（以文本方式包含编译）
 *          3. 算法优化：区域填充单次设窗口连续写 RAM；画线水平段批量化；
 *             数字显示查 10 次幂表；ID 探测表驱动
 */
#include "lcd.h"
#include "lcdfont.h"
#include "usart.h"
#include "delay.h"
#include "common_types.h"

#include "lcd_ex.c"     /* 各 IC 的寄存器初始化序列（勿单独加入工程） */

static SRAM_HandleTypeDef s_sramHandle;     /* FSMC 句柄（LCD 使用） */

uint32_t g_point_color = 0XF800;            /* 画笔颜色，默认红色 */
uint32_t g_back_color  = 0XFFFF;            /* 背景颜色，默认白色 */

_lcd_dev lcddev;                            /* LCD 关键参数 */

/* 10 的幂表（数字显示用，len 有效范围 1~10） */
static const uint32_t s_pow10Table[10] =
{
    1U, 10U, 100U, 1000U, 10000U,
    100000U, 1000000U, 10000000U, 100000000U, 1000000000U
};

/**
 * @brief   写数据到 LCD RAM
 */
void lcd_wr_data(volatile uint16_t data)
{
    LCD->LCD_RAM = data;
}

/**
 * @brief   写寄存器编号/地址到 LCD
 */
void lcd_wr_regno(volatile uint16_t regno)
{
    LCD->LCD_REG = regno;
}

/**
 * @brief   写寄存器（编号 + 值）
 */
void lcd_write_reg(uint16_t regno, uint16_t data)
{
    LCD->LCD_REG = regno;
    LCD->LCD_RAM = data;
}

/**
 * @brief   短延时（高优化等级下防止 FSMC 读写过快）
 * @note    计数变量必须 volatile：普通空转循环在 -O2 下会被整体优化掉，
 *          导致 LCD 读时序不满足要求
 */
static void lcd_opt_delay(uint32_t i)
{
    volatile uint32_t delayCnt = i;

    while (delayCnt--);
}

/**
 * @brief   读取 LCD RAM 数据（先延时再读，防止优化）
 */
static uint16_t lcd_rd_data(void)
{
    volatile uint16_t ramData;

    lcd_opt_delay(2);
    ramData = LCD->LCD_RAM;
    return ramData;
}

/**
 * @brief   准备写 GRAM
 */
void lcd_write_ram_prepare(void)
{
    LCD->LCD_REG = lcddev.wramcmd;
}

/**
 * @brief   读取某点颜色（RGB565）
 * @param   x, y : 坐标
 * @retval  该点颜色；越界返回 0
 */
uint32_t lcd_read_point(uint16_t x, uint16_t y)
{
    uint16_t r = 0, g = 0, b = 0;

    if (x >= lcddev.width || y >= lcddev.height)
    {
        return 0;
    }

    lcd_set_cursor(x, y);

    if (lcddev.id == 0X5510)
    {
        lcd_wr_regno(0X2E00);   /* NT35510 读 GRAM 指令 */
    }
    else
    {
        lcd_wr_regno(0X2E);     /* 其余 IC 读 GRAM 指令 */
    }

    r = lcd_rd_data();          /* dummy read */

    if (lcddev.id == 0x1963)
    {
        return r;               /* SSD1963 直接返回 */
    }

    r = lcd_rd_data();          /* 实际数据 */

    if (lcddev.id == 0x7796)    /* ST7796 一次读取一个像素值 */
    {
        return r;
    }

    /* ILI9341/NT35310/NT35510/ST7789/ILI9806 需 2 次读取并做格式转换：
     * 第一次读到的 r 为 RG 值（R 高 8 位），g = r 低 8 位 << 8 */
    b = lcd_rd_data();
    g = r & 0XFF;
    g <<= 8;

    return (((r >> 11) << 11) | ((g >> 10) << 5) | (b >> 11));
}

/**
 * @brief   开显示
 */
void lcd_display_on(void)
{
    lcd_wr_regno(lcddev.id == 0X5510 ? 0X2900 : 0X29);
}

/**
 * @brief   关显示
 */
void lcd_display_off(void)
{
    lcd_wr_regno(lcddev.id == 0X5510 ? 0X2800 : 0X28);
}

/**
 * @brief   设置光标位置（对 RGB 屏无效）
 */
void lcd_set_cursor(uint16_t x, uint16_t y)
{
    if (lcddev.id == 0X1963)                    /* SSD1963 特殊处理 */
    {
        if (lcddev.dir == 0)                    /* 竖屏模式，X 坐标需变换 */
        {
            x = lcddev.width - 1 - x;
            lcd_wr_regno(lcddev.setxcmd);
            lcd_wr_data(0);
            lcd_wr_data(0);
            lcd_wr_data(x >> 8);
            lcd_wr_data(x & 0XFF);
        }
        else                                    /* 横屏模式 */
        {
            lcd_wr_regno(lcddev.setxcmd);
            lcd_wr_data(x >> 8);
            lcd_wr_data(x & 0XFF);
            lcd_wr_data((lcddev.width - 1) >> 8);
            lcd_wr_data((lcddev.width - 1) & 0XFF);
        }

        lcd_wr_regno(lcddev.setycmd);
        lcd_wr_data(y >> 8);
        lcd_wr_data(y & 0XFF);
        lcd_wr_data((lcddev.height - 1) >> 8);
        lcd_wr_data((lcddev.height - 1) & 0XFF);
    }
    else if (lcddev.id == 0X5510)               /* NT35510：X/Y 寄存器分开 */
    {
        lcd_wr_regno(lcddev.setxcmd);
        lcd_wr_data(x >> 8);
        lcd_wr_regno(lcddev.setxcmd + 1);
        lcd_wr_data(x & 0XFF);
        lcd_wr_regno(lcddev.setycmd);
        lcd_wr_data(y >> 8);
        lcd_wr_regno(lcddev.setycmd + 1);
        lcd_wr_data(y & 0XFF);
    }
    else                                        /* 其余 IC */
    {
        lcd_wr_regno(lcddev.setxcmd);
        lcd_wr_data(x >> 8);
        lcd_wr_data(x & 0XFF);
        lcd_wr_regno(lcddev.setycmd);
        lcd_wr_data(y >> 8);
        lcd_wr_data(y & 0XFF);
    }
}

/**
 * @brief   设置 LCD 自动扫描方向（对 RGB 屏无效）
 * @param   dir : 0~7，定义见 lcd.h
 * @note    横屏模式需转换方向；9341/7789/7796 需额外设置 BGR 位
 */
void lcd_scan_dir(uint8_t dir)
{
    uint16_t regValue = 0;
    uint16_t dirReg = 0;
    uint16_t swapTemp;

    /* 横屏（或 1963 竖屏）时做方向转换 */
    if ((lcddev.dir == 1 && lcddev.id != 0X1963) || (lcddev.dir == 0 && lcddev.id == 0X1963))
    {
        switch (dir)
        {
            case 0: dir = 6; break;
            case 1: dir = 7; break;
            case 2: dir = 4; break;
            case 3: dir = 5; break;
            case 4: dir = 1; break;
            case 5: dir = 0; break;
            case 6: dir = 3; break;
            case 7: dir = 2; break;
        }
    }

    /* 方向写入 0X36 寄存器 bit5~7 */
    switch (dir)
    {
        case L2R_U2D: regValue |= (0 << 7) | (0 << 6) | (0 << 5); break;
        case L2R_D2U: regValue |= (1 << 7) | (0 << 6) | (0 << 5); break;
        case R2L_U2D: regValue |= (0 << 7) | (1 << 6) | (0 << 5); break;
        case R2L_D2U: regValue |= (1 << 7) | (1 << 6) | (0 << 5); break;
        case U2D_L2R: regValue |= (0 << 7) | (0 << 6) | (1 << 5); break;
        case U2D_R2L: regValue |= (0 << 7) | (1 << 6) | (1 << 5); break;
        case D2U_L2R: regValue |= (1 << 7) | (0 << 6) | (1 << 5); break;
        case D2U_R2L: regValue |= (1 << 7) | (1 << 6) | (1 << 5); break;
    }

    dirReg = 0X36;                              /* 大部分 IC 的寄存器地址 */

    if (lcddev.id == 0X5510)
    {
        dirReg = 0X3600;                        /* NT35510 寄存器地址不同 */
    }

    if (lcddev.id == 0X9341 || lcddev.id == 0X7789 || lcddev.id == 0x7796)
    {
        regValue |= 0X08;                       /* 9341/7789/7796 需设置 BGR 位 */
    }

    lcd_write_reg(dirReg, regValue);

    if (lcddev.id != 0X1963)                    /* 1963 不需要坐标处理 */
    {
        if (regValue & 0X20)
        {
            if (lcddev.width < lcddev.height)   /* 交换 X,Y */
            {
                swapTemp = lcddev.width;
                lcddev.width = lcddev.height;
                lcddev.height = swapTemp;
            }
        }
        else
        {
            if (lcddev.width > lcddev.height)
            {
                swapTemp = lcddev.width;
                lcddev.width = lcddev.height;
                lcddev.height = swapTemp;
            }
        }
    }

    /* 设置显示区域 */
    if (lcddev.id == 0X5510)
    {
        lcd_wr_regno(lcddev.setxcmd);
        lcd_wr_data(0);
        lcd_wr_regno(lcddev.setxcmd + 1);
        lcd_wr_data(0);
        lcd_wr_regno(lcddev.setxcmd + 2);
        lcd_wr_data((lcddev.width - 1) >> 8);
        lcd_wr_regno(lcddev.setxcmd + 3);
        lcd_wr_data((lcddev.width - 1) & 0XFF);
        lcd_wr_regno(lcddev.setycmd);
        lcd_wr_data(0);
        lcd_wr_regno(lcddev.setycmd + 1);
        lcd_wr_data(0);
        lcd_wr_regno(lcddev.setycmd + 2);
        lcd_wr_data((lcddev.height - 1) >> 8);
        lcd_wr_regno(lcddev.setycmd + 3);
        lcd_wr_data((lcddev.height - 1) & 0XFF);
    }
    else
    {
        lcd_wr_regno(lcddev.setxcmd);
        lcd_wr_data(0);
        lcd_wr_data(0);
        lcd_wr_data((lcddev.width - 1) >> 8);
        lcd_wr_data((lcddev.width - 1) & 0XFF);
        lcd_wr_regno(lcddev.setycmd);
        lcd_wr_data(0);
        lcd_wr_data(0);
        lcd_wr_data((lcddev.height - 1) >> 8);
        lcd_wr_data((lcddev.height - 1) & 0XFF);
    }
}

/**
 * @brief   画点
 */
void lcd_draw_point(uint16_t x, uint16_t y, uint32_t color)
{
    lcd_set_cursor(x, y);
    lcd_write_ram_prepare();
    LCD->LCD_RAM = color;
}

/**
 * @brief   SSD1963 背光设置
 * @param   pwm : 亮度等级 0~100，越大越亮
 */
void lcd_ssd_backlight_set(uint8_t pwm)
{
    lcd_wr_regno(0xBE);
    lcd_wr_data(0x05);              /* PWM 频率 */
    lcd_wr_data(pwm * 2.55);        /* PWM 占空比 */
    lcd_wr_data(0x01);
    lcd_wr_data(0xFF);
    lcd_wr_data(0x00);
    lcd_wr_data(0x00);
}

/**
 * @brief   设置 LCD 显示方向
 * @param   dir : 0 竖屏，1 横屏
 */
void lcd_display_dir(uint8_t dir)
{
    lcddev.dir = dir;

    if (dir == 0)                   /* 竖屏 */
    {
        lcddev.width = 240;
        lcddev.height = 320;

        if (lcddev.id == 0x5510)
        {
            lcddev.wramcmd = 0X2C00;
            lcddev.setxcmd = 0X2A00;
            lcddev.setycmd = 0X2B00;
            lcddev.width = 480;
            lcddev.height = 800;
        }
        else if (lcddev.id == 0X1963)
        {
            lcddev.wramcmd = 0X2C;
            lcddev.setxcmd = 0X2B;
            lcddev.setycmd = 0X2A;
            lcddev.width = 480;
            lcddev.height = 800;
        }
        else                        /* 9341/5310/7789/7796/9806 等 */
        {
            lcddev.wramcmd = 0X2C;
            lcddev.setxcmd = 0X2A;
            lcddev.setycmd = 0X2B;
        }

        if (lcddev.id == 0X5310 || lcddev.id == 0x7796)    /* 320*480 分辨率 */
        {
            lcddev.width = 320;
            lcddev.height = 480;
        }

        if (lcddev.id == 0X9806)                            /* 480*800 分辨率 */
        {
            lcddev.width = 480;
            lcddev.height = 800;
        }
    }
    else                            /* 横屏 */
    {
        lcddev.width = 320;
        lcddev.height = 240;

        if (lcddev.id == 0x5510)
        {
            lcddev.wramcmd = 0X2C00;
            lcddev.setxcmd = 0X2A00;
            lcddev.setycmd = 0X2B00;
            lcddev.width = 800;
            lcddev.height = 480;
        }
        else if (lcddev.id == 0X1963 || lcddev.id == 0X9806)
        {
            lcddev.wramcmd = 0X2C;
            lcddev.setxcmd = 0X2A;
            lcddev.setycmd = 0X2B;
            lcddev.width = 800;
            lcddev.height = 480;
        }
        else
        {
            lcddev.wramcmd = 0X2C;
            lcddev.setxcmd = 0X2A;
            lcddev.setycmd = 0X2B;
        }

        if (lcddev.id == 0X5310 || lcddev.id == 0x7796)    /* 320*480 分辨率 */
        {
            lcddev.width = 480;
            lcddev.height = 320;
        }
    }

    lcd_scan_dir(DFT_SCAN_DIR);
}

/**
 * @brief   设置窗口（对 RGB 屏无效），并将光标移到窗口左上角
 * @param   sx, sy       : 窗口起始坐标
 * @param   width, height: 窗口宽度和高度（必须大于 0）
 */
void lcd_set_window(uint16_t sx, uint16_t sy, uint16_t width, uint16_t height)
{
    uint16_t winEndX = sx + width - 1;
    uint16_t winEndY = sy + height - 1;

    if (lcddev.id == 0X1963 && lcddev.dir != 1)         /* 1963 竖屏特殊处理 */
    {
        sx = lcddev.width - width - sx;
        height = sy + height - 1;
        lcd_wr_regno(lcddev.setxcmd);
        lcd_wr_data(sx >> 8);
        lcd_wr_data(sx & 0XFF);
        lcd_wr_data((sx + width - 1) >> 8);
        lcd_wr_data((sx + width - 1) & 0XFF);
        lcd_wr_regno(lcddev.setycmd);
        lcd_wr_data(sy >> 8);
        lcd_wr_data(sy & 0XFF);
        lcd_wr_data(height >> 8);
        lcd_wr_data(height & 0XFF);
    }
    else if (lcddev.id == 0X5510)                       /* NT35510 */
    {
        lcd_wr_regno(lcddev.setxcmd);
        lcd_wr_data(sx >> 8);
        lcd_wr_regno(lcddev.setxcmd + 1);
        lcd_wr_data(sx & 0XFF);
        lcd_wr_regno(lcddev.setxcmd + 2);
        lcd_wr_data(winEndX >> 8);
        lcd_wr_regno(lcddev.setxcmd + 3);
        lcd_wr_data(winEndX & 0XFF);
        lcd_wr_regno(lcddev.setycmd);
        lcd_wr_data(sy >> 8);
        lcd_wr_regno(lcddev.setycmd + 1);
        lcd_wr_data(sy & 0XFF);
        lcd_wr_regno(lcddev.setycmd + 2);
        lcd_wr_data(winEndY >> 8);
        lcd_wr_regno(lcddev.setycmd + 3);
        lcd_wr_data(winEndY & 0XFF);
    }
    else                                                /* 其余 IC */
    {
        lcd_wr_regno(lcddev.setxcmd);
        lcd_wr_data(sx >> 8);
        lcd_wr_data(sx & 0XFF);
        lcd_wr_data(winEndX >> 8);
        lcd_wr_data(winEndX & 0XFF);
        lcd_wr_regno(lcddev.setycmd);
        lcd_wr_data(sy >> 8);
        lcd_wr_data(sy & 0XFF);
        lcd_wr_data(winEndY >> 8);
        lcd_wr_data(winEndY & 0XFF);
    }
}

/**
 * @brief   FSMC 底层初始化（由 HAL_SRAM_Init 自动调用）
 * @note    LCD 与外部 SRAM 共用 FSMC 数据总线
 */
void HAL_SRAM_MspInit(SRAM_HandleTypeDef *hsram)
{
    GPIO_InitTypeDef gpioInitStruct = {0};

    __HAL_RCC_FSMC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    /* FSMC 数据引脚 */
    gpioInitStruct.Mode = GPIO_MODE_AF_PP;
    gpioInitStruct.Pull = GPIO_PULLUP;
    gpioInitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    gpioInitStruct.Pin = LCD_FSMC_D_GPIO_PINS;
    HAL_GPIO_Init(LCD_FSMC_D_GPIO_PORT, &gpioInitStruct);

    gpioInitStruct.Pin = LCD_FSMC_E_GPIO_PINS;
    HAL_GPIO_Init(LCD_FSMC_E_GPIO_PORT, &gpioInitStruct);
}

/* ID 探测表项 */
typedef struct
{
    uint16_t cmd;       /* ID 读取指令 */
    uint16_t id;        /* 匹配的 ID */
    uint16_t altId;     /* 部分 IC 返回的替代 ID（0 表示无） */
    uint16_t altTo;     /* 替代 ID 转换成的标准 ID */
} lcd_id_probe_t;

/* 第一批标准探测（4 读时序） */
static const lcd_id_probe_t s_idProbeGroup1[] =
{
    {0XD3, 0X9341, 0,      0     },
    {0X04, 0X7789, 0X8552, 0X7789},
    {0XD4, 0X5310, 0,      0     },
    {0XD3, 0X7796, 0,      0     },
};

/* 第二批探测（NT35510 特殊探测之后） */
static const lcd_id_probe_t s_idProbeGroup2[] =
{
    {0XD3, 0X9806, 0,      0     },
    {0XA1, 0X1963, 0X5761, 0X1963},
};

/**
 * @brief   按标准 4 读时序读取 ID：写指令 -> dummy read -> 读高字节 -> 读低字节
 * @param   cmd : ID 读取指令
 * @retval  读取到的 ID
 */
static uint16_t lcd_read_id(uint16_t cmd)
{
    uint16_t id;

    lcd_wr_regno(cmd);
    id = lcd_rd_data();     /* dummy read */
    id = lcd_rd_data();     /* 读到 0X00 */
    id = lcd_rd_data();     /* 读 ID 高字节 */
    id <<= 8;
    id |= lcd_rd_data();    /* 读 ID 低字节 */

    return id;
}

/**
 * @brief   依次执行 ID 探测表，命中即返回
 * @param   probes : 探测表
 * @param   count  : 表项数量
 * @retval  1 匹配成功；0 全部未匹配
 */
static uint8_t lcd_try_id_probes(const lcd_id_probe_t* probes, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++)
    {
        lcddev.id = lcd_read_id(probes[i].cmd);

        if (probes[i].altId && lcddev.id == probes[i].altId)
        {
            lcddev.id = probes[i].altTo;
        }

        if (lcddev.id == probes[i].id)
        {
            return 1;
        }
    }

    return 0;
}

/**
 * @brief   初始化 LCD（自动识别驱动 IC 并执行对应初始化序列）
 */
void lcd_init(void)
{
    GPIO_InitTypeDef gpioInitStruct = {0};
    FSMC_NORSRAM_TimingTypeDef fsmcReadTiming = {0};
    FSMC_NORSRAM_TimingTypeDef fsmcWriteTiming = {0};

    LCD_CS_GPIO_CLK_ENABLE();
    LCD_WR_GPIO_CLK_ENABLE();
    LCD_RD_GPIO_CLK_ENABLE();
    LCD_RS_GPIO_CLK_ENABLE();
    LCD_BL_GPIO_CLK_ENABLE();

    /* 控制信号引脚（复用推挽） */
    gpioInitStruct.Mode = GPIO_MODE_AF_PP;
    gpioInitStruct.Pull = GPIO_PULLUP;
    gpioInitStruct.Speed = GPIO_SPEED_FREQ_HIGH;

    gpioInitStruct.Pin = LCD_CS_GPIO_PIN;
    HAL_GPIO_Init(LCD_CS_GPIO_PORT, &gpioInitStruct);

    gpioInitStruct.Pin = LCD_WR_GPIO_PIN;
    HAL_GPIO_Init(LCD_WR_GPIO_PORT, &gpioInitStruct);

    gpioInitStruct.Pin = LCD_RD_GPIO_PIN;
    HAL_GPIO_Init(LCD_RD_GPIO_PORT, &gpioInitStruct);

    gpioInitStruct.Pin = LCD_RS_GPIO_PIN;
    HAL_GPIO_Init(LCD_RS_GPIO_PORT, &gpioInitStruct);

    /* 背光引脚（普通推挽输出） */
    gpioInitStruct.Pin = LCD_BL_GPIO_PIN;
    gpioInitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(LCD_BL_GPIO_PORT, &gpioInitStruct);

    /* FSMC 配置：NE4，16 位数据宽度，读慢写快 */
    s_sramHandle.Instance = FSMC_NORSRAM_DEVICE;
    s_sramHandle.Extended = FSMC_NORSRAM_EXTENDED_DEVICE;

    s_sramHandle.Init.NSBank = FSMC_NORSRAM_BANK4;
    s_sramHandle.Init.DataAddressMux = FSMC_DATA_ADDRESS_MUX_DISABLE;
    s_sramHandle.Init.MemoryDataWidth = FSMC_NORSRAM_MEM_BUS_WIDTH_16;
    s_sramHandle.Init.BurstAccessMode = FSMC_BURST_ACCESS_MODE_DISABLE;
    s_sramHandle.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
    s_sramHandle.Init.WaitSignalActive = FSMC_WAIT_TIMING_BEFORE_WS;
    s_sramHandle.Init.WriteOperation = FSMC_WRITE_OPERATION_ENABLE;
    s_sramHandle.Init.WaitSignal = FSMC_WAIT_SIGNAL_DISABLE;
    s_sramHandle.Init.ExtendedMode = FSMC_EXTENDED_MODE_ENABLE;
    s_sramHandle.Init.AsynchronousWait = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    s_sramHandle.Init.WriteBurst = FSMC_WRITE_BURST_DISABLE;

    /* 读时序：DATAST=15，约 222ns（液晶 IC 读数据较慢） */
    fsmcReadTiming.AddressSetupTime = 0;
    fsmcReadTiming.AddressHoldTime = 0;
    fsmcReadTiming.DataSetupTime = 15;
    fsmcReadTiming.AccessMode = FSMC_ACCESS_MODE_A;

    /* 写时序：DATAST=1，约 28ns（部分 IC 写脉宽要求 50ns） */
    fsmcWriteTiming.AddressSetupTime = 0;
    fsmcWriteTiming.AddressHoldTime = 0;
    fsmcWriteTiming.DataSetupTime = 1;
    fsmcWriteTiming.AccessMode = FSMC_ACCESS_MODE_A;

    HAL_SRAM_Init(&s_sramHandle, &fsmcReadTiming, &fsmcWriteTiming);
    systickDelay_ms(50);        /* FSMC 初始化后需等待一段时间 */

    /* 依次尝试读取各 IC 的 ID */
    if (!lcd_try_id_probes(s_idProbeGroup1, ARRAY_SIZE(s_idProbeGroup1)))
    {
        /* NT35510 特殊探测：需解锁寄存器页；C501 读 ID 后需延时等待复位完成 */
        lcd_write_reg(0xF000, 0x0055);
        lcd_write_reg(0xF001, 0x00AA);
        lcd_write_reg(0xF002, 0x0052);
        lcd_write_reg(0xF003, 0x0008);
        lcd_write_reg(0xF004, 0x0001);

        lcd_wr_regno(0xC500);           /* 读 ID 低 8 位 */
        lcddev.id = lcd_rd_data();
        lcddev.id <<= 8;

        lcd_wr_regno(0xC501);           /* 读 ID 高 8 位（0XC501 是 1963 的软复位指令） */
        lcddev.id |= lcd_rd_data();

        systickDelay_ms(5);             /* 等待 1963 软复位完成 */

        if (lcddev.id != 0x5510)
        {
            lcd_try_id_probes(s_idProbeGroup2, ARRAY_SIZE(s_idProbeGroup2));
        }
    }

    /* 注意：串口必须最先初始化，否则此处的 printf 会卡死 */
    printf("LCD ID:%x\r\n", lcddev.id);

    if (lcddev.id == 0X7789)
    {
        lcd_ex_st7789_reginit();
    }
    else if (lcddev.id == 0X9341)
    {
        lcd_ex_ili9341_reginit();
    }
    else if (lcddev.id == 0x5310)
    {
        lcd_ex_nt35310_reginit();
    }
    else if (lcddev.id == 0x7796)
    {
        lcd_ex_st7796_reginit();
    }
    else if (lcddev.id == 0x5510)
    {
        lcd_ex_nt35510_reginit();
    }
    else if (lcddev.id == 0x9806)
    {
        lcd_ex_ili9806_reginit();
    }
    else if (lcddev.id == 0x1963)
    {
        lcd_ex_ssd1963_reginit();
        lcd_ssd_backlight_set(100);
    }

    lcd_display_dir(0);     /* 默认竖屏 */
    LCD_BL(1);              /* 点亮背光 */
    lcd_clear(WHITE);
}

/**
 * @brief   清屏
 * @param   color : 填充颜色
 * @note    设置一次光标后连续写 GRAM（GRAM 自动递增且逐行回绕）
 */
void lcd_clear(uint16_t color)
{
    uint32_t totalPoints = (uint32_t)lcddev.width * lcddev.height;

    lcd_set_cursor(0x00, 0x0000);
    lcd_write_ram_prepare();

    while (totalPoints--)
    {
        LCD->LCD_RAM = color;
    }
}

/**
 * @brief   在指定区域内填充单一颜色
 * @param   sx, sy, ex, ey : 矩形对角坐标
 * @param   color          : 填充颜色
 * @note    算法优化：只设置一次窗口，GRAM 在窗口内自动换行，
 *          命令开销由 O(行数×8) 降为 O(8)
 */
void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color)
{
    uint32_t totalPoints;

    if (ex < sx || ey < sy)
    {
        return;             /* 坐标非法（原实现此处会因回绕画出乱码，此处直接忽略） */
    }

    totalPoints = (uint32_t)(ex - sx + 1) * (ey - sy + 1);

    lcd_set_window(sx, sy, ex - sx + 1, ey - sy + 1);
    lcd_write_ram_prepare();

    while (totalPoints--)
    {
        LCD->LCD_RAM = color;
    }
}

/**
 * @brief   在指定区域内填充指定颜色数组（逐点）
 * @param   sx, sy, ex, ey : 矩形对角坐标
 * @param   color          : 颜色数组首地址
 * @note    与 lcd_fill 相同：单次设窗口 + 连续写
 */
void lcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color)
{
    uint32_t totalPoints;

    if (ex < sx || ey < sy)
    {
        return;
    }

    totalPoints = (uint32_t)(ex - sx + 1) * (ey - sy + 1);

    lcd_set_window(sx, sy, ex - sx + 1, ey - sy + 1);
    lcd_write_ram_prepare();

    while (totalPoints--)
    {
        LCD->LCD_RAM = *color++;
    }
}

/**
 * @brief   画直线（Bresenham 算法 + 水平段批量化）
 * @param   x1, y1 : 起点坐标
 * @param   x2, y2 : 终点坐标
 * @param   color  : 线的颜色
 * @note    算法优化：与逐点版本使用完全相同的 Bresenham 步进，
 *          像素集合一致；连续同行像素合并为水平段一次写出，
 *          命令开销由 O(像素数×8) 降为 O(水平段数×8)
 */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    int xerr = 0, yerr = 0, deltaX, deltaY, distance;
    int incx, incy, curX, curY;
    int runStartX, runEndX, runCol;
    uint16_t runLen;

    deltaX = (int)x2 - (int)x1;
    deltaY = (int)y2 - (int)y1;
    curX = x1;
    curY = y1;

    if (deltaX > 0)
    {
        incx = 1;
    }
    else if (deltaX == 0)
    {
        incx = 0;
    }
    else
    {
        incx = -1;
        deltaX = -deltaX;
    }

    if (deltaY > 0)
    {
        incy = 1;
    }
    else if (deltaY == 0)
    {
        incy = 0;
    }
    else
    {
        incy = -1;
        deltaY = -deltaY;
    }

    distance = (deltaX > deltaY) ? deltaX : deltaY;

    /* 竖直线快路径：单列填充 */
    if (incx == 0)
    {
        lcd_fill(x1, (y1 < y2) ? y1 : y2, x1, (y1 > y2) ? y1 : y2, color);
        return;
    }

    /* 通用路径：Bresenham 步进 + 水平段批量化（水平线自然退化为单段） */
    runStartX = curX;
    runEndX = curX;
    runCol = curY;
    runLen = 1;

    for (uint16_t t = 1; t <= distance + 1; t++)
    {
        xerr += deltaX;
        yerr += deltaY;

        if (xerr > distance)
        {
            xerr -= distance;
            curX += incx;
        }

        if (yerr > distance)
        {
            yerr -= distance;
            curY += incy;
        }

        if ((curY == runCol) && ((runEndX + incx) == curX))
        {
            runEndX = curX;     /* 同行且相邻，延长当前水平段 */
            runLen++;
        }
        else
        {
            /* 写出上一水平段，另起新段 */
            lcd_draw_hline((runStartX < runEndX) ? runStartX : runEndX, runCol, runLen, color);
            runStartX = curX;
            runEndX = curX;
            runCol = curY;
            runLen = 1;
        }
    }

    lcd_draw_hline((runStartX < runEndX) ? runStartX : runEndX, runCol, runLen, color);
}

/**
 * @brief   画水平线
 * @param   x, y : 起点坐标
 * @param   len  : 线长
 */
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color)
{
    if ((len == 0) || (x > lcddev.width) || (y > lcddev.height))
    {
        return;
    }

    lcd_fill(x, y, x + len - 1, y, color);
}

/**
 * @brief   画矩形
 */
void lcd_draw_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    lcd_draw_line(x1, y1, x2, y1, color);
    lcd_draw_line(x1, y1, x1, y2, color);
    lcd_draw_line(x1, y2, x2, y2, color);
    lcd_draw_line(x2, y1, x2, y2, color);
}

/**
 * @brief   画圆（Bresenham 算法，8 点对称描点）
 * @note    圆轮廓不能按水平段批量化（对称点之间是圆内区域），保持逐点绘制
 */
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint16_t color)
{
    int a = 0;
    int b = r;
    int di = 3 - (r << 1);      /* 判断下个点位置的标志 */

    while (a <= b)
    {
        lcd_draw_point(x0 + a, y0 - b, color);
        lcd_draw_point(x0 + b, y0 - a, color);
        lcd_draw_point(x0 + b, y0 + a, color);
        lcd_draw_point(x0 + a, y0 + b, color);
        lcd_draw_point(x0 - a, y0 + b, color);
        lcd_draw_point(x0 - b, y0 + a, color);
        lcd_draw_point(x0 - a, y0 - b, color);
        lcd_draw_point(x0 - b, y0 - a, color);
        a++;

        if (di < 0)
        {
            di += 4 * a + 6;
        }
        else
        {
            di += 10 + 4 * (a - b);
            b--;
        }
    }
}

/**
 * @brief   画实心圆（逐行水平线填充）
 */
void lcd_fill_circle(uint16_t x, uint16_t y, uint16_t r, uint16_t color)
{
    uint32_t iMax = ((uint32_t)r * 707) / 1000 + 1;
    uint32_t sqMax = (uint32_t)r * (uint32_t)r + (uint32_t)r / 2;
    uint32_t xr = r;

    lcd_draw_hline(x - r, y, 2 * r, color);

    for (uint32_t i = 1; i <= iMax; i++)
    {
        if ((i * i + xr * xr) > sqMax)
        {
            if (xr > iMax)      /* 画外侧线 */
            {
                lcd_draw_hline(x - i + 1, y + xr, 2 * (i - 1), color);
                lcd_draw_hline(x - i + 1, y - xr, 2 * (i - 1), color);
            }

            xr--;
        }

        lcd_draw_hline(x - xr, y + i, 2 * xr, color);       /* 画内侧线 */
        lcd_draw_hline(x - xr, y - i, 2 * xr, color);
    }
}

/**
 * @brief   在指定位置显示一个字符
 * @param   x, y  : 起点坐标（字符左上角）
 * @param   chr   : 要显示的字符 " "--->"~"
 * @param   size  : 字体大小 12/16/24/32
 * @param   mode  : 1 叠加显示；0 非叠加显示（背景用 g_back_color 填充）
 * @param   color : 字符颜色
 * @note    逐列绘制：每列只设置一次 1×N 窗口，列内像素连续写 RAM，
 *          命令开销 O(size)，远低于逐点绘制的 O(8×size)
 */
void lcd_show_char(uint16_t x, uint16_t y, char chr, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t *pFont;
    uint16_t fitRows;               /* 每列可绘制的行数 */
    uint8_t bytesPerCol;            /* 每列占用的字节数 */
    uint8_t colCount;               /* 总列数 */
    uint8_t clipped;                /* 是否被屏幕边界裁剪 */

    colCount = size / 2;
    bytesPerCol = (size % 8) ? (size / 8 + 1) : (size / 8);

    chr = chr - ' ';                /* 字库从空格开始取模 */

    switch (size)
    {
        case 12: pFont = (uint8_t *)asc2_1206[chr]; break;
        case 16: pFont = (uint8_t *)asc2_1608[chr]; break;
        case 24: pFont = (uint8_t *)asc2_2412[chr]; break;
        case 32: pFont = (uint8_t *)asc2_3216[chr]; break;
        default: return;
    }

    if (y >= lcddev.height)
    {
        return;
    }

    fitRows = (uint16_t)size;
    clipped = 0;
    if (y + size > lcddev.height)
    {
        fitRows = lcddev.height - y;
        clipped = 1;
    }

    for (uint8_t col = 0; col < colCount; col++)
    {
        uint16_t xPos = x + col;
        uint16_t rowsRemaining = fitRows;
        uint8_t columnDone = 0;

        if (xPos >= lcddev.width)
        {
            return;                 /* x 越界，终止整个字符 */
        }

        lcd_set_window(xPos, y, 1, fitRows);
        lcd_write_ram_prepare();

        for (uint8_t byteIdx = 0; (byteIdx < bytesPerCol) && (!columnDone); byteIdx++)
        {
            uint8_t fontByte = pFont[col * bytesPerCol + byteIdx];

            for (uint8_t bit = 0; bit < 8; bit++)
            {
                if (rowsRemaining == 0)
                {
                    if (clipped)
                    {
                        return;     /* 被边界裁剪：终止整个字符 */
                    }
                    columnDone = 1; /* 本列画完，跳过填充位 */
                    break;
                }

                if (fontByte & 0x80)
                {
                    LCD->LCD_RAM = color;
                }
                else if (mode == 0) /* 非叠加：背景色填充 */
                {
                    LCD->LCD_RAM = g_back_color;
                }
                /* 叠加模式：不写入，窗口自动递增保留原像素 */

                fontByte <<= 1;
                rowsRemaining--;
            }
        }
    }
}

/**
 * @brief   显示 len 位数字（高位 0 不显示）
 * @param   x, y : 起点坐标
 * @param   num  : 数值
 * @param   len  : 数字位数（1~10）
 * @param   size : 字体大小 12/16/24/32
 */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint16_t color)
{
    uint8_t enShow = 0;

    if (len == 0 || len > 10)
    {
        return;
    }

    for (uint8_t t = 0; t < len; t++)
    {
        uint8_t digit = (num / s_pow10Table[len - t - 1]) % 10;

        if (enShow == 0 && t < (len - 1))
        {
            if (digit == 0)
            {
                lcd_show_char(x + (size / 2) * t, y, ' ', size, 0, color);   /* 空格占位 */
                continue;
            }

            enShow = 1;
        }

        lcd_show_char(x + (size / 2) * t, y, digit + '0', size, 0, color);
    }
}

/**
 * @brief   扩展显示 len 位数字
 * @param   x, y : 起点坐标
 * @param   num  : 数值
 * @param   len  : 数字位数（1~10）
 * @param   size : 字体大小 12/16/24/32
 * @param   mode : [7]:1 高位补 0，0 高位补空格；[0]:1 叠加显示，0 非叠加显示
 */
void lcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t enShow = 0;

    if (len == 0 || len > 10)
    {
        return;
    }

    for (uint8_t t = 0; t < len; t++)
    {
        uint8_t digit = (num / s_pow10Table[len - t - 1]) % 10;

        if (enShow == 0 && t < (len - 1))
        {
            if (digit == 0)
            {
                if (mode & 0X80)    /* 高位补 0 */
                {
                    lcd_show_char(x + (size / 2) * t, y, '0', size, mode & 0X01, color);
                }
                else                /* 高位补空格 */
                {
                    lcd_show_char(x + (size / 2) * t, y, ' ', size, mode & 0X01, color);
                }

                continue;
            }

            enShow = 1;
        }

        lcd_show_char(x + (size / 2) * t, y, digit + '0', size, mode & 0X01, color);
    }
}

/**
 * @brief   显示字符串
 * @param   x, y         : 起点坐标
 * @param   width, height: 显示区域大小
 * @param   size         : 字体大小 12/16/24/32
 * @param   p            : 字符串首地址
 * @param   color        : 字符颜色
 */
void lcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint16_t color)
{
    uint8_t startX = x;
    width += x;
    height += y;

    while ((*p <= '~') && (*p >= ' '))      /* 判断是否为合法字符 */
    {
        if (x >= width)
        {
            x = startX;
            y += size;
        }

        if (y >= height)
        {
            break;
        }

        lcd_show_char(x, y, *p, size, 0, color);
        x += size / 2;
        p++;
    }
}
