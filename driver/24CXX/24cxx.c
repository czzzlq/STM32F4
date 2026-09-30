/**
 * @file    24cxx.c
 * @brief   AT24CXX 系列 I2C EEPROM 驱动
 * @note    地址长度按型号自适应：24C16 及以下把高位地址并入器件地址字节，
 *          24C32 及以上先发独立的起始地址字节。
 *          所有接口返回 status_t，从机不应答（器件缺失 / 总线异常）会被显式报出，
 *          不再静默失败。
 */

#include "24cxx.h"
#include "delay.h"
#include "myiic.h"

#define AT24CXX_ADDR_WRITE  0xA0    /* 器件地址 + 写方向 */
#define AT24CXX_ADDR_READ   0xA1    /* 器件地址 + 读方向 */
#define AT24CXX_POLL_MAX    15      /* 写周期轮询上限（ms），手册最大 5ms，留 3 倍裕量 */
#define AT24CXX_CHECK_MARK  0x55    /* 器件存在性标记 */

/**
 * @brief   发送「器件地址 + 字地址」阶段
 * @param   addr : 目标字地址
 * @retval  STATUS_OK：从机已应答；STATUS_ERR：从机未应答
 * @note    读写两条路径的公共前缀，抽出来避免重复实现地址阶段。
 *          失败时 iic_wait_ack() 内部已产生 STOP 释放总线，调用方无需再补
 */
static status_t at24cxx_send_addr(uint16_t addr)
{
    if (EE_TYPE > AT24C16)
    {
        /* 24C32 及以上：器件地址与高位地址分两个字节发送 */
        iic_send_byte(AT24CXX_ADDR_WRITE);
        if (iic_wait_ack())
        {
            return STATUS_ERR;
        }

        iic_send_byte((uint8_t)(addr >> 8));
    }
    else
    {
        /* 24C16 及以下：高位地址(a8~a10)占用器件地址的 bit1~bit3 */
        iic_send_byte((uint8_t)(AT24CXX_ADDR_WRITE + ((addr >> 8) << 1)));
    }

    if (iic_wait_ack())
    {
        return STATUS_ERR;
    }

    iic_send_byte((uint8_t)(addr & 0xFF));  /* 低位地址 */
    if (iic_wait_ack())
    {
        return STATUS_ERR;
    }

    return STATUS_OK;
}

/**
 * @brief   等待 EEPROM 内部写周期结束
 * @retval  STATUS_OK：器件重新应答；STATUS_ERR_TIMEOUT：超过 AT24CXX_POLL_MAX
 * @note    不用固定长延时，而是按 I2C 标准做法「地址轮询」(ACK polling)：
 *          写周期内器件不应答，一旦应答即可立刻返回，13 字节的校准参数
 *          保存时间由固定 130ms 降到典型 30~65ms
 */
static status_t at24cxx_wait_ready(void)
{
    uint8_t retry = AT24CXX_POLL_MAX;

    do
    {
        iic_start();
        iic_send_byte(AT24CXX_ADDR_WRITE);

        if (iic_wait_ack() == 0)    /* 收到应答：写周期已结束 */
        {
            iic_stop();
            return STATUS_OK;
        }

        /* 未应答时 iic_wait_ack() 已发出 STOP；等待 1ms 后重试 */
        systickDelay_ms(1);
    } while (--retry);

    return STATUS_ERR_TIMEOUT;
}

/**
 * @brief   初始化 EEPROM 所用的 IIC 接口
 * @retval  STATUS_OK
 */
status_t at24cxx_init(void)
{
    iic_init();
    return STATUS_OK;
}

/**
 * @brief   从指定地址读出一个字节
 * @param   addr : 读取地址
 *   @arg   data : 读出数据的存放处
 * @retval  STATUS_OK / STATUS_ERR_PARAM / STATUS_ERR
 * @note    读完字地址后必须重新发起 START 再发读命令，这是 EEPROM 的
 *          「当前地址读」之外的标准随机读时序
 */
status_t at24cxx_read_one_byte(uint16_t addr, uint8_t *data)
{
    if (data == NULL)
    {
        return STATUS_ERR_PARAM;
    }

    iic_start();                                    /* 发送起始信号 */
    if (at24cxx_send_addr(addr) != STATUS_OK)
    {
        return STATUS_ERR;                          /* 器件未应答 */
    }

    iic_start();                                    /* 重新起始，切换到读方向 */
    iic_send_byte(AT24CXX_ADDR_READ);
    if (iic_wait_ack())
    {
        return STATUS_ERR;
    }

    *data = iic_read_byte(0);                       /* 单字节读：回 NACK 结束 */
    iic_stop();
    return STATUS_OK;
}

/**
 * @brief   在指定地址写入一个字节
 * @param   addr : 写入地址
 *   @arg   data : 待写入的数据
 * @retval  STATUS_OK / STATUS_ERR / STATUS_ERR_TIMEOUT
 * @note    写入后器件进入内部写周期（典型 5ms），必须等其结束才能访问下一个字节
 */
status_t at24cxx_write_one_byte(uint16_t addr, uint8_t data)
{
    iic_start();                                    /* 发送起始信号 */
    if (at24cxx_send_addr(addr) != STATUS_OK)
    {
        return STATUS_ERR;
    }

    /* 写数据无需重新起始，方向沿用写命令 */
    iic_send_byte(data);
    if (iic_wait_ack())
    {
        return STATUS_ERR;
    }

    iic_stop();

    return at24cxx_wait_ready();
}

/**
 * @brief   检查器件是否正常
 * @retval  STATUS_OK：器件存在且可读写；STATUS_ERR：器件未应答或数据校验失败
 * @note    探测地址取 EE_TYPE（末地址），先读后写——正常使用中该地址已存有标记，
 *          因此不会每次上电都产生一次写操作
 */
status_t at24cxx_check(void)
{
    const uint16_t addr = EE_TYPE;      /* 末地址：24C02 为 0xFF */
    uint8_t temp = 0;

    if ((at24cxx_read_one_byte(addr, &temp) == STATUS_OK) && (temp == AT24CXX_CHECK_MARK))
    {
        return STATUS_OK;
    }

    if (at24cxx_write_one_byte(addr, AT24CXX_CHECK_MARK) != STATUS_OK)
    {
        return STATUS_ERR;              /* 器件无应答：未焊接或总线异常 */
    }

    if ((at24cxx_read_one_byte(addr, &temp) != STATUS_OK) || (temp != AT24CXX_CHECK_MARK))
    {
        return STATUS_ERR;
    }

    return STATUS_OK;
}

/**
 * @brief   从指定地址开始连续读出指定个数的数据
 * @param   addr    : 起始地址
 *   @arg   pbuf    : 数据缓冲区
 *   @arg   datalen : 要读出的字节数
 * @retval  STATUS_OK / STATUS_ERR_PARAM / STATUS_ERR
 * @note    逐字节随机读而非页读：每次读都要重发字地址，但可跨页且中途可校验；
 *          失败立即返回，避免器件缺失时白等 datalen 次超时
 */
status_t at24cxx_read(uint16_t addr, uint8_t *pbuf, uint16_t datalen)
{
    if (pbuf == NULL)
    {
        return STATUS_ERR_PARAM;
    }

    while (datalen--)
    {
        if (at24cxx_read_one_byte(addr++, pbuf++) != STATUS_OK)
        {
            return STATUS_ERR;
        }
    }

    return STATUS_OK;
}

/**
 * @brief   从指定地址开始连续写入指定个数的数据
 * @param   addr    : 起始地址
 *   @arg   pbuf    : 待写入数据
 *   @arg   datalen : 要写入的字节数
 * @retval  STATUS_OK / STATUS_ERR_PARAM / STATUS_ERR / STATUS_ERR_TIMEOUT
 * @note    按字节写入（每字节一次写周期）以保证跨页安全，
 *          因此耗时与字节数成正比，不要放在高频路径里调用
 */
status_t at24cxx_write(uint16_t addr, uint8_t *pbuf, uint16_t datalen)
{
    if (pbuf == NULL)
    {
        return STATUS_ERR_PARAM;
    }

    while (datalen--)
    {
        if (at24cxx_write_one_byte(addr++, *pbuf++) != STATUS_OK)
        {
            return STATUS_ERR;
        }
    }

    return STATUS_OK;
}
