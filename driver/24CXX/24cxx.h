/**
 * @file    24cxx.h
 * @brief   AT24CXX 系列 I2C EEPROM 驱动接口
 * @note    基于 driver/IIC 的软件 IIC 时序；器件型号由 EE_TYPE 选定，
 *          该宏同时用作「末地址」常量（各型号的容量上限值）
 */
#ifndef __24CXX_H
#define __24CXX_H

#include "common_types.h"

/* 各型号的末地址（容量 - 1），也是 at24cxx_check 的探测地址 */
#define AT24C01     127
#define AT24C02     255
#define AT24C04     511
#define AT24C08     1023
#define AT24C16     2047
#define AT24C32     4095
#define AT24C64     8191
#define AT24C128    16383
#define AT24C256    32767

/* 本板使用的 EEPROM 型号 */
#define EE_TYPE     AT24C02

/* addr  : 目标字地址（24C16 及以下为 0~EE_TYPE）
 * data  : 待写入的数据 / 读出数据的存放处
 * datalen: 连续读写的字节数（不跨页也可用，内部按字节写入） */
status_t at24cxx_init(void);                                            /* 初始化 IIC 总线 */
status_t at24cxx_check(void);                                           /* 检查器件是否存在 */
status_t at24cxx_read_one_byte(uint16_t addr, uint8_t *data);           /* 指定地址读取一个字节 */
status_t at24cxx_write_one_byte(uint16_t addr, uint8_t data);           /* 指定地址写入一个字节 */
status_t at24cxx_read(uint16_t addr, uint8_t *pbuf, uint16_t datalen);  /* 从指定地址开始读出指定长度的数据 */
status_t at24cxx_write(uint16_t addr, uint8_t *pbuf, uint16_t datalen); /* 从指定地址开始写入指定长度的数据 */

#endif /* __24CXX_H */
