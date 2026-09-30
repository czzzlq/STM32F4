/**
 * @file    sram.c
 * @brief   外部 SRAM 驱动（FSMC 16 位总线）
 * @note    1. 引脚/片选配置见 bsp/bsp_sram.h，换板只需改 BSP 层
 *          2. FSMC 数据总线初始化由 HAL_SRAM_Init 回调完成（见 lcd.c 的 HAL_SRAM_MspInit），
 *             本文件同时直接初始化数据/地址总线引脚，保证单独编译本模块也可用
 *          3. 读写分级加速：双 4 字节对齐时按 32 位拷贝，否则按 16 位拷贝
 *             （FSMC 16 位总线自动拆分 32 位事务，32 位路径可省一半循环指令）
 */
#include "sram.h"

SRAM_HandleTypeDef g_sram_handler;

/**
 * @brief   初始化外部 SRAM（FSMC）
 */
void sram_init(void)
{
    GPIO_InitTypeDef gpioInitStruct = {0};
    FSMC_NORSRAM_TimingTypeDef fsmcTiming = {0};

    SRAM_CS_GPIO_CLK_ENABLE();
    SRAM_WR_GPIO_CLK_ENABLE();
    SRAM_RD_GPIO_CLK_ENABLE();
    SRAM_FSMC_CLK_ENABLE();
    SRAM_FSMC_D_GPIO_CLK_ENABLE();
    SRAM_FSMC_E_GPIO_CLK_ENABLE();
    SRAM_FSMC_F_GPIO_CLK_ENABLE();
    SRAM_FSMC_G_GPIO_CLK_ENABLE();

    /* 控制信号/数据/地址总线：CS/WR/RD + FSMC_Dx/Ax
     * （F4 必须指定 AF12=FSMC，否则引脚停在 AF0，总线信号出不去） */
    gpioInitStruct.Mode = GPIO_MODE_AF_PP;
    gpioInitStruct.Pull = GPIO_PULLUP;
    gpioInitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    gpioInitStruct.Alternate = GPIO_AF12_FSMC;

    gpioInitStruct.Pin = SRAM_CS_GPIO_PIN;
    HAL_GPIO_Init(SRAM_CS_GPIO_PORT, &gpioInitStruct);

    gpioInitStruct.Pin = SRAM_WR_GPIO_PIN;
    HAL_GPIO_Init(SRAM_WR_GPIO_PORT, &gpioInitStruct);

    gpioInitStruct.Pin = SRAM_RD_GPIO_PIN;
    HAL_GPIO_Init(SRAM_RD_GPIO_PORT, &gpioInitStruct);

    /* 数据/地址总线（FSMC_Dx/FSMC_Ax） */
    gpioInitStruct.Pin = SRAM_FSMC_D_GPIO_PINS;
    HAL_GPIO_Init(SRAM_FSMC_D_GPIO_PORT, &gpioInitStruct);

    gpioInitStruct.Pin = SRAM_FSMC_E_GPIO_PINS;
    HAL_GPIO_Init(SRAM_FSMC_E_GPIO_PORT, &gpioInitStruct);

    gpioInitStruct.Pin = SRAM_FSMC_F_GPIO_PINS;
    HAL_GPIO_Init(SRAM_FSMC_F_GPIO_PORT, &gpioInitStruct);

    gpioInitStruct.Pin = SRAM_FSMC_G_GPIO_PINS;
    HAL_GPIO_Init(SRAM_FSMC_G_GPIO_PORT, &gpioInitStruct);

    /* FSMC 配置：16 位数据宽度，模式 A */
    g_sram_handler.Instance = FSMC_NORSRAM_DEVICE;
    g_sram_handler.Extended = FSMC_NORSRAM_EXTENDED_DEVICE;

    g_sram_handler.Init.NSBank = (SRAM_FSMC_NEX == 1) ? FSMC_NORSRAM_BANK1 :
                                 (SRAM_FSMC_NEX == 2) ? FSMC_NORSRAM_BANK2 :
                                 (SRAM_FSMC_NEX == 3) ? FSMC_NORSRAM_BANK3 :
                                                        FSMC_NORSRAM_BANK4;
    g_sram_handler.Init.DataAddressMux = FSMC_DATA_ADDRESS_MUX_DISABLE;
    g_sram_handler.Init.MemoryType = FSMC_MEMORY_TYPE_SRAM;
    g_sram_handler.Init.MemoryDataWidth = FSMC_NORSRAM_MEM_BUS_WIDTH_16;
    g_sram_handler.Init.BurstAccessMode = FSMC_BURST_ACCESS_MODE_DISABLE;
    g_sram_handler.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
    g_sram_handler.Init.WaitSignalActive = FSMC_WAIT_TIMING_BEFORE_WS;
    g_sram_handler.Init.WriteOperation = FSMC_WRITE_OPERATION_ENABLE;
    g_sram_handler.Init.WaitSignal = FSMC_WAIT_SIGNAL_DISABLE;
    g_sram_handler.Init.ExtendedMode = FSMC_EXTENDED_MODE_DISABLE;
    g_sram_handler.Init.AsynchronousWait = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    g_sram_handler.Init.WriteBurst = FSMC_WRITE_BURST_DISABLE;

    /* 时序（模式 A；FSMC 时基 = HCLK = 168MHz，1 HCLK ≈ 5.952ns，
     *      本版 HAL 把寄存器值直接写入 BTR/BWTR，不做 -1 换算，即"值 = 周期数"）
     *
     * 器件：XM8A51216V33A（512K×16 = 1MB，异步 XRAM，15ns 档）
     *       数据手册 002-00008 Rev.A1 P13 Switching Characteristics:
     *         tRC ≥ 15ns（读周期）、tAA ≤ 15ns（地址→数据有效）、tACE ≤ 12ns（片选→数据有效）
     *         tDOE ≤ 3.4ns（OE→数据有效）
     *         tWC ≥ 15ns（写周期）、tSCE ≥ 7ns、tAW ≥ 8.4ns、tPWE ≥ 0.7ns（WE 脉宽）
     *         tSA/tSD/tHD = 0ns（地址/数据建立保持无要求）
     *         tPOWER = 1000us（上电到首次访问，由启动流程天然满足：首次真正的读写
     *                 发生在 sram_init 之后，远大于 1ms）
     *
     * 取值 ADDSET=1、DATAST=3（读=写，ExtendedMode 关闭）：
     *   读周期 = ADDSET+DATAST+2 = 6 HCLK = 35.7ns ≥ tRC 15ns（2.4 倍裕量）
     *   地址有效→采样 = ADDSET+DATAST = 4 HCLK = 23.8ns ≥ tAA 15ns / tACE 12ns
     *   NOE 低电平 = DATAST = 3 HCLK = 17.9ns ≥ tDOE 3.4ns
     *   写周期 = ADDSET+DATAST = 4 HCLK = 23.8ns ≥ tWC 15ns / tSCE 7ns / tAW 8.4ns
     *   NWE 低电平 = DATAST = 3 HCLK = 17.9ns ≥ tPWE 0.7ns
     * 说明：原 F1 配置（0/1）在 168MHz 下只有 5.95ns 写周期，低于 tWC/tSCE/tAW，
     *       属超频使用，故按手册重算 */
    fsmcTiming.AddressSetupTime = 0x01;
    fsmcTiming.AddressHoldTime = 0x00;
    fsmcTiming.DataSetupTime = 0x03;
    fsmcTiming.BusTurnAroundDuration = 0x00;
    fsmcTiming.AccessMode = FSMC_ACCESS_MODE_A;

    HAL_SRAM_Init(&g_sram_handler, &fsmcTiming, &fsmcTiming);
}

/**
 * @brief   向 SRAM 指定地址写入指定长度数据
 * @param   pbuf    : 数据源
 * @param   addr    : 写入起始地址
 * @param   datalen : 写入字节数
 */
void sram_write(uint8_t *pbuf, uint32_t addr, uint32_t datalen)
{
    volatile uint8_t *dst8 = (volatile uint8_t *)(SRAM_BASE_ADDR + addr);

    /* 头部字节对齐到偶地址 */
    while (datalen && ((uint32_t)dst8 & 1U))
    {
        *dst8++ = *pbuf++;
        datalen--;
    }

    /* 双 4 字节对齐时按 32 位拷贝（16 位总线自动拆分为 2 次半字事务） */
    if ((((uint32_t)dst8 & 3U) == 0) && (((uint32_t)pbuf & 3U) == 0))
    {
        volatile uint32_t *dst32 = (volatile uint32_t *)dst8;
        uint32_t *src32 = (uint32_t *)pbuf;

        while (datalen >= 4)
        {
            *dst32++ = *src32++;
            datalen -= 4;
        }

        dst8 = (volatile uint8_t *)dst32;
        pbuf = (uint8_t *)src32;
    }

    /* 主体 16 位拷贝：目的地址已偶对齐；源允许非对齐（M3 支持普通内存非对齐访问） */
    volatile uint16_t *dst16 = (volatile uint16_t *)dst8;
    uint16_t *src16 = (uint16_t *)pbuf;

    while (datalen >= 2)
    {
        *dst16++ = *src16++;
        datalen -= 2;
    }

    /* 尾部剩余字节 */
    dst8 = (volatile uint8_t *)dst16;
    pbuf = (uint8_t *)src16;
    while (datalen--)
    {
        *dst8++ = *pbuf++;
    }
}

/**
 * @brief   从 SRAM 指定地址读取指定长度数据
 * @param   pbuf    : 数据目的
 * @param   addr    : 读取起始地址
 * @param   datalen : 读取字节数
 */
void sram_read(uint8_t *pbuf, uint32_t addr, uint32_t datalen)
{
    volatile uint8_t *src8 = (volatile uint8_t *)(SRAM_BASE_ADDR + addr);

    /* 头部字节对齐到偶地址 */
    while (datalen && ((uint32_t)src8 & 1U))
    {
        *pbuf++ = *src8++;
        datalen--;
    }

    /* 双 4 字节对齐时按 32 位拷贝 */
    if ((((uint32_t)src8 & 3U) == 0) && (((uint32_t)pbuf & 3U) == 0))
    {
        volatile uint32_t *src32 = (volatile uint32_t *)src8;
        uint32_t *dst32 = (uint32_t *)pbuf;

        while (datalen >= 4)
        {
            *dst32++ = *src32++;
            datalen -= 4;
        }

        src8 = (volatile uint8_t *)src32;
        pbuf = (uint8_t *)dst32;
    }

    /* 主体 16 位拷贝 */
    volatile uint16_t *src16 = (volatile uint16_t *)src8;
    uint16_t *dst16 = (uint16_t *)pbuf;

    while (datalen >= 2)
    {
        *dst16++ = *src16++;
        datalen -= 2;
    }

    /* 尾部剩余字节 */
    src8 = (volatile uint8_t *)src16;
    pbuf = (uint8_t *)dst16;
    while (datalen--)
    {
        *pbuf++ = *src8++;
    }
}

/**
 * @brief   测试函数：向 SRAM 指定地址写入 1 字节
 */
void sram_test_write(uint32_t addr, uint8_t data)
{
    sram_write(&data, addr, 1);
}

/**
 * @brief   测试函数：从 SRAM 指定地址读取 1 字节
 */
uint8_t sram_test_read(uint32_t addr)
{
    uint8_t data;

    sram_read(&data, addr, 1);
    return data;
}
