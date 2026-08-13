/**
 * @file    sram.c
 * @brief   外部 SRAM 驱动（FSMC 16 位总线）
 * @note    1. 引脚/片选配置见 bsp/bsp_sram.h，换板只需改 BSP 层
 *          2. FSMC 数据总线初始化由 HAL_SRAM_Init 回调完成（见 lcd.c 的 HAL_SRAM_MspInit），
 *             本文件同时直接初始化数据/地址总线引脚，保证单独编译本模块也可用
 *          3. 读写按 16 位对齐加速：FSMC 16 位总线下字节访问与半字访问占用相同的总线周期
 */
#include "sram.h"

SRAM_HandleTypeDef g_sram_handler;

/**
 * @brief   初始化外部 SRAM（FSMC）
 */
void sram_init(void)
{
    GPIO_InitTypeDef GPIO_Initure = {0};
    FSMC_NORSRAM_TimingTypeDef fsmc_readwritetim = {0};

    SRAM_CS_GPIO_CLK_ENABLE();
    SRAM_WR_GPIO_CLK_ENABLE();
    SRAM_RD_GPIO_CLK_ENABLE();
    __HAL_RCC_FSMC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* 控制信号：CS/WR/RD（复用推挽） */
    GPIO_Initure.Mode = GPIO_MODE_AF_PP;
    GPIO_Initure.Pull = GPIO_PULLUP;
    GPIO_Initure.Speed = GPIO_SPEED_FREQ_HIGH;

    GPIO_Initure.Pin = SRAM_CS_GPIO_PIN;
    HAL_GPIO_Init(SRAM_CS_GPIO_PORT, &GPIO_Initure);

    GPIO_Initure.Pin = SRAM_WR_GPIO_PIN;
    HAL_GPIO_Init(SRAM_WR_GPIO_PORT, &GPIO_Initure);

    GPIO_Initure.Pin = SRAM_RD_GPIO_PIN;
    HAL_GPIO_Init(SRAM_RD_GPIO_PORT, &GPIO_Initure);

    /* 数据/地址总线（FSMC_Dx/FSMC_Ax） */
    GPIO_Initure.Pin = SRAM_FSMC_D_GPIO_PINS;
    HAL_GPIO_Init(SRAM_FSMC_D_GPIO_PORT, &GPIO_Initure);

    GPIO_Initure.Pin = SRAM_FSMC_E_GPIO_PINS;
    HAL_GPIO_Init(SRAM_FSMC_E_GPIO_PORT, &GPIO_Initure);

    GPIO_Initure.Pin = SRAM_FSMC_F_GPIO_PINS;
    HAL_GPIO_Init(SRAM_FSMC_F_GPIO_PORT, &GPIO_Initure);

    GPIO_Initure.Pin = SRAM_FSMC_G_GPIO_PINS;
    HAL_GPIO_Init(SRAM_FSMC_G_GPIO_PORT, &GPIO_Initure);

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

    /* 时序（HCLK=72MHz，1 周期 ≈ 13.9ns）：
     * ADDSET=0（1 个 HCLK），DATAST=1（2 个 HCLK），模式 A */
    fsmc_readwritetim.AddressSetupTime = 0x00;
    fsmc_readwritetim.AddressHoldTime = 0x00;
    fsmc_readwritetim.DataSetupTime = 0x01;
    fsmc_readwritetim.BusTurnAroundDuration = 0x00;
    fsmc_readwritetim.AccessMode = FSMC_ACCESS_MODE_A;

    HAL_SRAM_Init(&g_sram_handler, &fsmc_readwritetim, &fsmc_readwritetim);
}

/**
 * @brief   向 SRAM 指定地址写入指定长度数据（16 位对齐加速）
 * @param   pbuf    : 数据源
 * @param   addr    : 写入起始地址
 * @param   datalen : 写入字节数
 */
void sram_write(uint8_t *pbuf, uint32_t addr, uint32_t datalen)
{
    volatile uint8_t *dst8 = (volatile uint8_t *)(SRAM_BASE_ADDR + addr);

    /* 头部字节对齐到偶地址 */
    while ((datalen != 0) && ((uint32_t)dst8 & 1U))
    {
        *dst8++ = *pbuf++;
        datalen--;
    }

    /* 主体 16 位拷贝：目的地址已对齐；源地址允许非对齐（M3 支持普通内存非对齐访问） */
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
    while (datalen != 0)
    {
        *dst8++ = *pbuf++;
        datalen--;
    }
}

/**
 * @brief   从 SRAM 指定地址读取指定长度数据（16 位对齐加速）
 * @param   pbuf    : 数据目的
 * @param   addr    : 读取起始地址
 * @param   datalen : 读取字节数
 */
void sram_read(uint8_t *pbuf, uint32_t addr, uint32_t datalen)
{
    volatile uint8_t *src8 = (volatile uint8_t *)(SRAM_BASE_ADDR + addr);

    /* 头部字节对齐到偶地址 */
    while ((datalen != 0) && ((uint32_t)src8 & 1U))
    {
        *pbuf++ = *src8++;
        datalen--;
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
    while (datalen != 0)
    {
        *pbuf++ = *src8++;
        datalen--;
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
