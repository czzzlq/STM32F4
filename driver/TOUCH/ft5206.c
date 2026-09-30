/**
 * @file    ft5206.c
 * @brief   FT5206 / CST340 电容触摸 IC 驱动
 * @note    用版本寄存器区分 FT 系列（0x3003/0x0001/0x0002）与 CST340（0x0000）；
 *          所有寄存器访问都检查从机应答，避免芯片缺失时把总线浮空电平
 *          当成有效坐标
 */

#include "ft5206.h"
#include "delay.h"
#include "ctiic.h"
#include "touch.h"
#include "lcd.h"
#include <stdio.h>      /* printf（触摸调试打印） */

/**
 * @brief       向FT5206写入一次数据
 * @param       reg : 起始寄存器地址（8 位）
 *   @arg       buf : 数据缓存区
 *   @arg       len : 写数据长度
 * @retval      0, 成功; 1, 失败;
 * @note        地址阶段的应答同样要检查：命令或地址没被应答时继续发数据
 *              只会把数据写到总线上被丢弃
 */
uint8_t ft5206_wr_reg(uint16_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    ct_iic_start();
    ct_iic_send_byte(FT5206_CMD_WR);    /* 发送写命令 */
    if (ct_iic_wait_ack())
    {
        return 1;                       /* 失败时 wait_ack 已产生 STOP */
    }

    ct_iic_send_byte(reg & 0xFF);       /* 发送低8位地址 */
    if (ct_iic_wait_ack())
    {
        return 1;
    }

    for (i = 0; i < len; i++)
    {
        ct_iic_send_byte(buf[i]);       /* 发数据 */
        if (ct_iic_wait_ack())
        {
            return 1;
        }
    }

    ct_iic_stop();                      /* 产生一个停止条件 */
    return 0;
}

/**
 * @brief       从FT5206读出一次数据
 * @param       reg : 起始寄存器地址（8 位）
 *   @arg       buf : 数据缓存区
 *   @arg       len : 读数据长度
 * @retval      0, 成功; 1, 失败;
 * @note        失败时 buf 内容不可信，调用方必须检查返回值后再使用
 */
uint8_t ft5206_rd_reg(uint16_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    ct_iic_start();
    ct_iic_send_byte(FT5206_CMD_WR);    /* 发送写命令 */
    if (ct_iic_wait_ack())
    {
        return 1;
    }

    ct_iic_send_byte(reg & 0xFF);       /* 发送低8位地址 */
    if (ct_iic_wait_ack())
    {
        return 1;
    }

    ct_iic_start();
    ct_iic_send_byte(FT5206_CMD_RD);    /* 发送读命令 */
    if (ct_iic_wait_ack())
    {
        return 1;
    }

    for (i = 0; i < len; i++)
    {
        /* 最后一个字节回 NACK，通知从机停止发送 */
        buf[i] = ct_iic_read_byte((i == (len - 1)) ? 0 : 1);
    }

    ct_iic_stop();  /* 产生一个停止条件 */
    return 0;
}

/**
 * @brief       初始化FT5206触摸屏
 * @param       无
 * @retval      0, 初始化成功; 1, 初始化失败;
 */
uint8_t ft5206_init(void)
{
    GPIO_InitTypeDef gpio_init_struct;
    uint8_t temp[2];

    FT5206_RST_GPIO_CLK_ENABLE();   /* RST引脚时钟使能 */
    FT5206_INT_GPIO_CLK_ENABLE();   /* INT引脚时钟使能 */

    gpio_init_struct.Pin = FT5206_RST_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;            /* 推挽输出 */
    gpio_init_struct.Pull = GPIO_PULLUP;                    /* 上拉 */
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;     /* 高速 */
    HAL_GPIO_Init(FT5206_RST_GPIO_PORT, &gpio_init_struct); /* 初始化RST引脚 */

    gpio_init_struct.Pin = FT5206_INT_GPIO_PIN;
    gpio_init_struct.Mode = GPIO_MODE_INPUT;                /* 输入 */
    gpio_init_struct.Pull = GPIO_PULLUP;                    /* 上拉 */
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;     /* 高速 */
    HAL_GPIO_Init(FT5206_INT_GPIO_PORT, &gpio_init_struct); /* 初始化INT引脚 */

    ct_iic_init();      /* 初始化电容屏的I2C总线 */
    FT5206_RST(0);      /* 复位 */
    systickDelay_ms(20);
    FT5206_RST(1);      /* 释放复位 */
    systickDelay_ms(50);
    temp[0] = 0;
    ft5206_wr_reg(FT5206_DEVIDE_MODE, temp, 1); /* 进入正常操作模式 */
    ft5206_wr_reg(FT5206_ID_G_MODE, temp, 1);   /* 查询模式 */
    temp[0] = 22;                               /* 触摸有效值，22，越小越灵敏 */
    ft5206_wr_reg(FT5206_ID_G_THGROUP, temp, 1);/* 设置触摸有效值 */
    temp[0] = 12;                               /* 激活周期，不能小于12，最大14 */
    ft5206_wr_reg(FT5206_ID_G_PERIODACTIVE, temp, 1);

    /* 读取版本号，参考值：0x3003 */
    if (ft5206_rd_reg(FT5206_ID_G_LIB_VERSION, &temp[0], 2) != 0)
    {
        return 1;   /* 无应答：触摸 IC 未焊接或总线异常 */
    }

    if ((temp[0] == 0x30 && temp[1] == 0x03) || temp[1] == 0x01 || temp[1] == 0x02 ||
        (temp[0] == 0x00 && temp[1] == 0x00))   /* 版本:0x3003/0x0001/0x0002/CST340 */
    {
        printf("CTP ID:%x\r\n", ((uint16_t)temp[0] << 8) + temp[1]);
        return 0;
    }

    return 1;
}

/* FT5206 5个触摸点 对应的寄存器表 */
static const uint16_t s_ftTpxTbl[5] =
{
    FT5206_TP1_REG, FT5206_TP2_REG, FT5206_TP3_REG, FT5206_TP4_REG, FT5206_TP5_REG
};

/**
 * @brief       扫描触摸屏(采用查询方式)
 * @param       mode : 电容屏未用到此参数, 为了兼容电阻屏
 * @retval      当前触屏状态
 *   @arg       0, 触屏无触摸;
 *   @arg       1, 触屏有触摸;
 * @note        空闲时每 10 次调用才真正读一次寄存器，降低 IIC 占用；
 *              一旦读到有效触点则连续采样 10 次以提高命中率
 */
uint8_t ft5206_scan(uint8_t mode)
{
    uint8_t sta = 0;
    uint8_t buf[4];
    uint8_t i = 0;
    uint8_t res = 0;
    uint16_t temp;
    static uint8_t t = 0;   /* 控制查询间隔,从而降低CPU占用率 */

    UNUSED(mode);           /* 电容屏不需要屏幕坐标/物理坐标模式 */

    t++;

    if ((t % 10) == 0 || t < 10)   /* 空闲时,每进入10次CTP_Scan函数才检测1次,从而节省CPU使用率 */
    {
        if (ft5206_rd_reg(FT5206_REG_NUM_FINGER, &sta, 1) != 0)
        {
            /* 读失败（芯片未应答/总线异常）：强制按「无触摸」处理。若直接返回，
             * TP_PRES_DOWN 会一直残留，表现为触摸屏卡在按下状态 */
            tp_dev.sta &= ~TP_PRES_DOWN;
            return 0;
        }

        if ((sta & 0x0F) && ((sta & 0x0F) < 6))
        {
            temp = 0xFFFF << (sta & 0x0F);              /* 将点的个数转换为1的位数,匹配tp_dev.sta定义 */
            tp_dev.sta = (uint16_t)(~temp) | TP_PRES_DOWN | TP_CATH_PRES;
            systickDelay_ms(4);    /* 必要的延时，否则老是认为有按键按下 */

            for (i = 0; i < ARRAY_SIZE(s_ftTpxTbl); i++)
            {
                if (tp_dev.sta & (1 << i))              /* 触摸有效? */
                {
                    if (ft5206_rd_reg(s_ftTpxTbl[i], buf, 4) != 0)
                    {
                        return res;     /* 读失败：本帧作废，交由下一次采样 */
                    }

                    if (tp_dev.touchtype & 0x01)    /* 横屏 */
                    {
                        tp_dev.y[i] = ((uint16_t)(buf[0] & 0x0F) << 8) + buf[1];
                        tp_dev.x[i] = ((uint16_t)(buf[2] & 0x0F) << 8) + buf[3];
                    }
                    else
                    {
                        tp_dev.x[i] = lcddev.width - (((uint16_t)(buf[0] & 0x0F) << 8) + buf[1]);
                        tp_dev.y[i] = ((uint16_t)(buf[2] & 0x0F) << 8) + buf[3];
                    }

                    if ((buf[0] & 0xF0) != 0x80)    /* 必须是 contact 事件，才认为有效 */
                    {
                        tp_dev.x[i] = 0;
                        tp_dev.y[i] = 0;
                    }
                }
            }

            res = 1;

            if (tp_dev.x[0] == 0 && tp_dev.y[0] == 0)
            {
                sta = 0;    /* 读到的数据都是0,则忽略此次数据 */
            }

            t = 0;  /* 触发一次,则会最少连续监测10次,从而提高命中率 */
        }
    }

    if ((sta & 0x1F) == 0)  /* 无触摸点按下 */
    {
        if (tp_dev.sta & TP_PRES_DOWN)      /* 之前是被按下的 */
        {
            tp_dev.sta &= ~TP_PRES_DOWN;    /* 标记按键松开 */
        }
        else    /* 之前就没有被按下 */
        {
            tp_dev.x[0] = 0xFFFF;
            tp_dev.y[0] = 0xFFFF;
            tp_dev.sta &= 0xE000;           /* 清除点有效标记 */
        }
    }

    if (t > 240)
    {
        t = 10; /* 重新从10开始计数 */
    }

    return res;
}
