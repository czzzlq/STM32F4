/**
 * @file    lcd.c
 * @brief   TFTLCD 驱动（FSMC 16 位总线）
 * @note    1. 硬件配置见 bsp/bsp_lcd.h，换板/换屏只需改 BSP 层
 *          2. 支持 ILI9341/NT35310/NT35510/SSD1963/ST7789/ST7796/ILI9806，
 *             寄存器初始化序列见 lcd_ex.c（以文本方式包含编译）
 *          3. 热路径（清屏/填充/字符显示）均采用"设窗口+连续写 RAM"方式
 */
#include "lcd.h"
#include "lcdfont.h"
#include "usart.h"
#include "delay.h"

#include "lcd_ex.c"     /* 各 IC 的寄存器初始化序列（勿单独加入工程） */

static SRAM_HandleTypeDef g_sram_handle;    /* FSMC 句柄（LCD 使用） */

uint32_t g_point_color = 0XF800;            /* 画笔颜色，默认红色 */
uint32_t g_back_color  = 0XFFFF;            /* 背景颜色，默认白色 */

_lcd_dev lcddev;                            /* LCD 关键参数 */

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
 */
static void lcd_opt_delay(uint32_t i)
{
    while (i--);
}

/**
 * @brief   读取 LCD RAM 数据（先延时再读，防止优化）
 */
static uint16_t lcd_rd_data(void)
{
    volatile uint16_t ram;

    lcd_opt_delay(2);
    ram = LCD->LCD_RAM;
    return ram;
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
    uint16_t regval = 0;
    uint16_t dirreg = 0;
    uint16_t temp;

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
        case L2R_U2D: regval |= (0 << 7) | (0 << 6) | (0 << 5); break;
        case L2R_D2U: regval |= (1 << 7) | (0 << 6) | (0 << 5); break;
        case R2L_U2D: regval |= (0 << 7) | (1 << 6) | (0 << 5); break;
        case R2L_D2U: regval |= (1 << 7) | (1 << 6) | (0 << 5); break;
        case U2D_L2R: regval |= (0 << 7) | (0 << 6) | (1 << 5); break;
        case U2D_R2L: regval |= (0 << 7) | (1 << 6) | (1 << 5); break;
        case D2U_L2R: regval |= (1 << 7) | (0 << 6) | (1 << 5); break;
        case D2U_R2L: regval |= (1 << 7) | (1 << 6) | (1 << 5); break;
    }

    dirreg = 0X36;                              /* 大部分 IC 的寄存器地址 */

    if (lcddev.id == 0X5510)
    {
        dirreg = 0X3600;                        /* NT35510 寄存器地址不同 */
    }

    if (lcddev.id == 0X9341 || lcddev.id == 0X7789 || lcddev.id == 0x7796)
    {
        regval |= 0X08;                         /* 9341/7789/7796 需设置 BGR 位 */
    }

    lcd_write_reg(dirreg, regval);

    if (lcddev.id != 0X1963)                    /* 1963 不需要坐标处理 */
    {
        if (regval & 0X20)
        {
            if (lcddev.width < lcddev.height)   /* 交换 X,Y */
            {
                temp = lcddev.width;
                lcddev.width = lcddev.height;
                lcddev.height = temp;
            }
        }
        else
        {
            if (lcddev.width > lcddev.height)
            {
                temp = lcddev.width;
                lcddev.width = lcddev.height;
                lcddev.height = temp;
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
    uint16_t twidth = sx + width - 1;
    uint16_t theight = sy + height - 1;

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
        lcd_wr_data(twidth >> 8);
        lcd_wr_regno(lcddev.setxcmd + 3);
        lcd_wr_data(twidth & 0XFF);
        lcd_wr_regno(lcddev.setycmd);
        lcd_wr_data(sy >> 8);
        lcd_wr_regno(lcddev.setycmd + 1);
        lcd_wr_data(sy & 0XFF);
        lcd_wr_regno(lcddev.setycmd + 2);
        lcd_wr_data(theight >> 8);
        lcd_wr_regno(lcddev.setycmd + 3);
        lcd_wr_data(theight & 0XFF);
    }
    else                                                /* 其余 IC */
    {
        lcd_wr_regno(lcddev.setxcmd);
        lcd_wr_data(sx >> 8);
        lcd_wr_data(sx & 0XFF);
        lcd_wr_data(twidth >> 8);
        lcd_wr_data(twidth & 0XFF);
        lcd_wr_regno(lcddev.setycmd);
        lcd_wr_data(sy >> 8);
        lcd_wr_data(sy & 0XFF);
        lcd_wr_data(theight >> 8);
        lcd_wr_data(theight & 0XFF);
    }
}

/**
 * @brief   FSMC 底层初始化（由 HAL_SRAM_Init 自动调用）
 * @note    LCD 与外部 SRAM 共用 FSMC 数据总线
 */
void HAL_SRAM_MspInit(SRAM_HandleTypeDef *hsram)
{
    GPIO_InitTypeDef gpio_init_struct = {0};

    __HAL_RCC_FSMC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    /* FSMC 数据引脚 */
    gpio_init_struct.Mode = GPIO_MODE_AF_PP;
    gpio_init_struct.Pull = GPIO_PULLUP;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;

    gpio_init_struct.Pin = LCD_FSMC_D_GPIO_PINS;
    HAL_GPIO_Init(LCD_FSMC_D_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = LCD_FSMC_E_GPIO_PINS;
    HAL_GPIO_Init(LCD_FSMC_E_GPIO_PORT, &gpio_init_struct);
}

/**
 * @brief   初始化 LCD（自动识别驱动 IC 并执行对应初始化序列）
 */
void lcd_init(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    FSMC_NORSRAM_TimingTypeDef fsmc_read_handle = {0};
    FSMC_NORSRAM_TimingTypeDef fsmc_write_handle = {0};

    LCD_CS_GPIO_CLK_ENABLE();
    LCD_WR_GPIO_CLK_ENABLE();
    LCD_RD_GPIO_CLK_ENABLE();
    LCD_RS_GPIO_CLK_ENABLE();
    LCD_BL_GPIO_CLK_ENABLE();

    /* 控制信号引脚（复用推挽） */
    gpio_init_struct.Mode = GPIO_MODE_AF_PP;
    gpio_init_struct.Pull = GPIO_PULLUP;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;

    gpio_init_struct.Pin = LCD_CS_GPIO_PIN;
    HAL_GPIO_Init(LCD_CS_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = LCD_WR_GPIO_PIN;
    HAL_GPIO_Init(LCD_WR_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = LCD_RD_GPIO_PIN;
    HAL_GPIO_Init(LCD_RD_GPIO_PORT, &gpio_init_struct);

    gpio_init_struct.Pin = LCD_RS_GPIO_PIN;
    HAL_GPIO_Init(LCD_RS_GPIO_PORT, &gpio_init_struct);

    /* 背光引脚（普通推挽输出） */
    gpio_init_struct.Pin = LCD_BL_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(LCD_BL_GPIO_PORT, &gpio_init_struct);

    /* FSMC 配置：NE4，16 位数据宽度，读慢写快 */
    g_sram_handle.Instance = FSMC_NORSRAM_DEVICE;
    g_sram_handle.Extended = FSMC_NORSRAM_EXTENDED_DEVICE;

    g_sram_handle.Init.NSBank = FSMC_NORSRAM_BANK4;
    g_sram_handle.Init.DataAddressMux = FSMC_DATA_ADDRESS_MUX_DISABLE;
    g_sram_handle.Init.MemoryDataWidth = FSMC_NORSRAM_MEM_BUS_WIDTH_16;
    g_sram_handle.Init.BurstAccessMode = FSMC_BURST_ACCESS_MODE_DISABLE;
    g_sram_handle.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
    g_sram_handle.Init.WaitSignalActive = FSMC_WAIT_TIMING_BEFORE_WS;
    g_sram_handle.Init.WriteOperation = FSMC_WRITE_OPERATION_ENABLE;
    g_sram_handle.Init.WaitSignal = FSMC_WAIT_SIGNAL_DISABLE;
    g_sram_handle.Init.ExtendedMode = FSMC_EXTENDED_MODE_ENABLE;
    g_sram_handle.Init.AsynchronousWait = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    g_sram_handle.Init.WriteBurst = FSMC_WRITE_BURST_DISABLE;

    /* 读时序：DATAST=15，约 222ns（液晶 IC 读数据较慢） */
    fsmc_read_handle.AddressSetupTime = 0;
    fsmc_read_handle.AddressHoldTime = 0;
    fsmc_read_handle.DataSetupTime = 15;
    fsmc_read_handle.AccessMode = FSMC_ACCESS_MODE_A;

    /* 写时序：DATAST=1，约 28ns（部分 IC 写脉宽要求 50ns） */
    fsmc_write_handle.AddressSetupTime = 0;
    fsmc_write_handle.AddressHoldTime = 0;
    fsmc_write_handle.DataSetupTime = 1;
    fsmc_write_handle.AccessMode = FSMC_ACCESS_MODE_A;

    HAL_SRAM_Init(&g_sram_handle, &fsmc_read_handle, &fsmc_write_handle);
    systickDelay_ms(50);        /* FSMC 初始化后需等待一段时间 */

    /* 依次尝试读取各 IC 的 ID */
    lcd_wr_regno(0XD3);
    lcddev.id = lcd_rd_data();  /* dummy read */
    lcddev.id = lcd_rd_data();  /* 读到 0X00 */
    lcddev.id = lcd_rd_data();  /* 读到 0X93 */
    lcddev.id <<= 8;
    lcddev.id |= lcd_rd_data(); /* 读到 0X41 */

    if (lcddev.id != 0X9341)    /* 非 9341，尝试 ST7789 */
    {
        lcd_wr_regno(0X04);
        lcddev.id = lcd_rd_data();
        lcddev.id = lcd_rd_data();
        lcddev.id = lcd_rd_data();
        lcddev.id <<= 8;
        lcddev.id |= lcd_rd_data();

        if (lcddev.id == 0X8552)    /* 8552 的 ID 转成 7789 */
        {
            lcddev.id = 0x7789;
        }

        if (lcddev.id != 0x7789)    /* 非 7789，尝试 NT35310 */
        {
            lcd_wr_regno(0xD4);
            lcddev.id = lcd_rd_data();
            lcddev.id = lcd_rd_data();
            lcddev.id = lcd_rd_data();
            lcddev.id <<= 8;
            lcddev.id |= lcd_rd_data();

            if (lcddev.id != 0x5310)    /* 非 35310，尝试 ST7796 */
            {
                lcd_wr_regno(0XD3);
                lcddev.id = lcd_rd_data();
                lcddev.id = lcd_rd_data();
                lcddev.id = lcd_rd_data();
                lcddev.id <<= 8;
                lcddev.id |= lcd_rd_data();

                if (lcddev.id != 0x7796)    /* 非 7796，尝试 NT35510 */
                {
                    lcd_write_reg(0xF000, 0x0055);
                    lcd_write_reg(0xF001, 0x00AA);
                    lcd_write_reg(0xF002, 0x0052);
                    lcd_write_reg(0xF003, 0x0008);
                    lcd_write_reg(0xF004, 0x0001);

                    lcd_wr_regno(0xC500);       /* 读 ID 低 8 位 */
                    lcddev.id = lcd_rd_data();
                    lcddev.id <<= 8;

                    lcd_wr_regno(0xC501);       /* 读 ID 高 8 位 */
                    lcddev.id |= lcd_rd_data();

                    systickDelay_ms(5);         /* 0XC501 是 1963 的软复位指令，等待复位完成 */

                    if (lcddev.id != 0x5510)    /* 非 35510，尝试 ILI9806 */
                    {
                        lcd_wr_regno(0XD3);
                        lcddev.id = lcd_rd_data();
                        lcddev.id = lcd_rd_data();
                        lcddev.id = lcd_rd_data();
                        lcddev.id <<= 8;
                        lcddev.id |= lcd_rd_data();

                        if (lcddev.id != 0x9806)    /* 非 9806，尝试 SSD1963 */
                        {
                            lcd_wr_regno(0xA1);
                            lcddev.id = lcd_rd_data();
                            lcddev.id = lcd_rd_data();
                            lcddev.id <<= 8;
                            lcddev.id |= lcd_rd_data();

                            if (lcddev.id == 0x5761)
                            {
                                lcddev.id = 0x1963;     /* 1963 的 ID 是 5761H，统一成 1963 */
                            }
                        }
                    }
                }
            }
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
    uint32_t totalpoint = (uint32_t)lcddev.width * lcddev.height;

    lcd_set_cursor(0x00, 0x0000);
    lcd_write_ram_prepare();

    for (uint32_t index = 0; index < totalpoint; index++)
    {
        LCD->LCD_RAM = color;
    }
}

/**
 * @brief   在指定区域内填充单一颜色
 * @param   sx, sy, ex, ey : 矩形对角坐标
 * @param   color          : 填充颜色
 */
void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color)
{
    uint16_t xlen = ex - sx + 1;

    for (uint16_t i = sy; i <= ey; i++)
    {
        lcd_set_cursor(sx, i);
        lcd_write_ram_prepare();

        for (uint16_t j = 0; j < xlen; j++)
        {
            LCD->LCD_RAM = color;
        }
    }
}

/**
 * @brief   在指定区域内填充指定颜色数组（逐点）
 * @param   sx, sy, ex, ey : 矩形对角坐标
 * @param   color          : 颜色数组首地址
 */
void lcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color)
{
    uint16_t width = ex - sx + 1;
    uint16_t height = ey - sy + 1;

    for (uint16_t i = 0; i < height; i++)
    {
        lcd_set_cursor(sx, sy + i);
        lcd_write_ram_prepare();

        for (uint16_t j = 0; j < width; j++)
        {
            LCD->LCD_RAM = color[i * width + j];
        }
    }
}

/**
 * @brief   画直线（Bresenham 算法）
 */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    int xerr = 0, yerr = 0, delta_x, delta_y, distance;
    int incx, incy, row, col;

    delta_x = x2 - x1;
    delta_y = y2 - y1;
    row = x1;
    col = y1;

    if (delta_x > 0) incx = 1;
    else if (delta_x == 0) incx = 0;
    else
    {
        incx = -1;
        delta_x = -delta_x;
    }

    if (delta_y > 0) incy = 1;
    else if (delta_y == 0) incy = 0;
    else
    {
        incy = -1;
        delta_y = -delta_y;
    }

    distance = (delta_x > delta_y) ? delta_x : delta_y;

    for (uint16_t t = 0; t <= distance + 1; t++)
    {
        lcd_draw_point(row, col, color);
        xerr += delta_x;
        yerr += delta_y;

        if (xerr > distance)
        {
            xerr -= distance;
            row += incx;
        }

        if (yerr > distance)
        {
            yerr -= distance;
            col += incy;
        }
    }
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
 * @brief   画圆（Bresenham 算法）
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
    uint32_t imax = ((uint32_t)r * 707) / 1000 + 1;
    uint32_t sqmax = (uint32_t)r * (uint32_t)r + (uint32_t)r / 2;
    uint32_t xr = r;

    lcd_draw_hline(x - r, y, 2 * r, color);

    for (uint32_t i = 1; i <= imax; i++)
    {
        if ((i * i + xr * xr) > sqmax)
        {
            if (xr > imax)      /* 画外侧线 */
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
 * @note    性能优化：逐列绘制，每列只设置一次 1×N 窗口，列内像素连续写 RAM，
 *          相比逐点 set_cursor 的实现，命令开销从 O(8×size) 降为 O(size)
 */
void lcd_show_char(uint16_t x, uint16_t y, char chr, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t *pfont;
    uint16_t fit_rows;              /* 每列可绘制的行数 */
    uint8_t bytes_per_col;          /* 每列占用的字节数 */
    uint8_t cols;                   /* 总列数 */
    uint8_t clipped;                /* 是否被屏幕边界裁剪 */

    cols = size / 2;
    bytes_per_col = (size % 8) ? (size / 8 + 1) : (size / 8);

    chr = chr - ' ';                /* 字库从空格开始取模 */

    switch (size)
    {
        case 12: pfont = (uint8_t *)asc2_1206[chr]; break;
        case 16: pfont = (uint8_t *)asc2_1608[chr]; break;
        case 24: pfont = (uint8_t *)asc2_2412[chr]; break;
        case 32: pfont = (uint8_t *)asc2_3216[chr]; break;
        default: return;
    }

    if (y >= lcddev.height)
    {
        return;
    }

    fit_rows = (uint16_t)size;
    clipped = 0;
    if (y + size > lcddev.height)
    {
        fit_rows = lcddev.height - y;
        clipped = 1;
    }

    for (uint8_t col = 0; col < cols; col++)
    {
        uint16_t x_pos = x + col;
        uint16_t rows_remaining = fit_rows;
        uint8_t done_column = 0;

        if (x_pos >= lcddev.width)
        {
            return;                 /* x 越界，终止整个字符 */
        }

        lcd_set_window(x_pos, y, 1, fit_rows);
        lcd_write_ram_prepare();

        for (uint8_t b = 0; (b < bytes_per_col) && (!done_column); b++)
        {
            uint8_t temp = pfont[col * bytes_per_col + b];

            for (uint8_t bit = 0; bit < 8; bit++)
            {
                if (rows_remaining == 0)
                {
                    if (clipped)
                    {
                        return;     /* 被边界裁剪：终止整个字符 */
                    }
                    done_column = 1;/* 本列画完，跳过填充位 */
                    break;
                }

                if (temp & 0x80)
                {
                    LCD->LCD_RAM = color;
                }
                else if (mode == 0) /* 非叠加：背景色填充 */
                {
                    LCD->LCD_RAM = g_back_color;
                }
                /* 叠加模式：不写入，窗口自动递增保留原像素 */

                temp <<= 1;
                rows_remaining--;
            }
        }
    }
}

/**
 * @brief   幂运算 m^n（内部函数）
 */
static uint32_t lcd_pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;

    while (n--)
    {
        result *= m;
    }

    return result;
}

/**
 * @brief   显示 len 位数字（高位 0 不显示）
 * @param   x, y : 起点坐标
 * @param   num  : 数值
 * @param   len  : 数字位数
 * @param   size : 字体大小 12/16/24/32
 */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint16_t color)
{
    uint8_t enshow = 0;

    for (uint8_t t = 0; t < len; t++)
    {
        uint8_t temp = (num / lcd_pow(10, len - t - 1)) % 10;

        if (enshow == 0 && t < (len - 1))
        {
            if (temp == 0)
            {
                lcd_show_char(x + (size / 2) * t, y, ' ', size, 0, color);   /* 空格占位 */
                continue;
            }

            enshow = 1;
        }

        lcd_show_char(x + (size / 2) * t, y, temp + '0', size, 0, color);
    }
}

/**
 * @brief   扩展显示 len 位数字
 * @param   x, y : 起点坐标
 * @param   num  : 数值
 * @param   len  : 数字位数
 * @param   size : 字体大小 12/16/24/32
 * @param   mode : [7]:1 高位补 0，0 高位补空格；[0]:1 叠加显示，0 非叠加显示
 */
void lcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t enshow = 0;

    for (uint8_t t = 0; t < len; t++)
    {
        uint8_t temp = (num / lcd_pow(10, len - t - 1)) % 10;

        if (enshow == 0 && t < (len - 1))
        {
            if (temp == 0)
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

            enshow = 1;
        }

        lcd_show_char(x + (size / 2) * t, y, temp + '0', size, mode & 0X01, color);
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
    uint8_t x0 = x;
    width += x;
    height += y;

    while ((*p <= '~') && (*p >= ' '))      /* 判断是否为合法字符 */
    {
        if (x >= width)
        {
            x = x0;
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
