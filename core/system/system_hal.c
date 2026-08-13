/**
 * @file    system_hal.c
 * @brief   系统时钟与核心功能实现（HSE + PLL，72MHz）
 */
#include "system_hal.h"

/* STM32F103ZET6 存储相关常量 */
#define FLASH_SIZE      (512 * 1024U)   /* Flash 大小 */
#define RAM_BASE        0X20000000U     /* RAM 起始地址 */
#define RAM_SIZE        (64 * 1024U)    /* RAM 大小（内部 SRAM） */

/* F1 系列 96 位唯一 ID 基地址 */
#define UID_BASE_ADDR   0x1FFFF7E8U

static SystemClockConfig_t s_clockConfig;

/**
 * @brief   系统时钟初始化（HSE -> PLL -> 总线预分频）
 * @param   pllMultipler : PLL 倍频系数（9 表示 8MHz × 9 = 72MHz）
 */
static void systemCoreClockInit(uint32_t pllMultipler)
{
    RCC_OscInitTypeDef oscInit = {0};
    RCC_ClkInitTypeDef clkInit = {0};

    /* 配置振荡器：HSE + PLL */
    oscInit.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscInit.HSEState       = RCC_HSE_ON;
    oscInit.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    oscInit.PLL.PLLState   = RCC_PLL_ON;
    oscInit.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    oscInit.PLL.PLLMUL     = pllMultipler;
    HAL_RCC_OscConfig(&oscInit);

    /* 配置总线时钟：SYSCLK=PLLCLK，AHB 不分频，APB1 二分频，APB2 不分频 */
    clkInit.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                        RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clkInit.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clkInit.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clkInit.APB1CLKDivider = RCC_HCLK_DIV2;
    clkInit.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&clkInit, FLASH_LATENCY_2);

    /* 记录时钟配置 */
    s_clockConfig.pll_multipliter = pllMultipler;
    s_clockConfig.system_freq = HSE_VALUE * pllMultipler;
    s_clockConfig.hclk_freq = s_clockConfig.system_freq;
    s_clockConfig.pclk1_freq = s_clockConfig.system_freq / 2;
    s_clockConfig.pclk2_freq = s_clockConfig.system_freq;
}

/**
 * @brief   系统初始化：HAL 时基 + 72MHz 系统时钟 + SysTick
 */
void systemCoreInit(void)
{
    HAL_Init();
    systemCoreClockInit(RCC_PLL_MUL9);      /* 系统时钟 72MHz */
    HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq() / 1000);
    SystemCoreClockUpdate();
}

/**
 * @brief   时钟反初始化（复位 RCC 并清零时钟配置，为系统复位做准备）
 */
void systemCoreClockDeinit(void)
{
    HAL_RCC_DeInit();

    s_clockConfig.system_freq = 0;
    s_clockConfig.hclk_freq = 0;
    s_clockConfig.pclk1_freq = 0;
    s_clockConfig.pclk2_freq = 0;
    s_clockConfig.pll_multipliter = 0;
}

/**
 * @brief   获取当前时钟配置
 * @retval  时钟配置结构体指针
 */
const SystemClockConfig_t* systemCoreGetClockConfig(void)
{
    return &s_clockConfig;
}

/**
 * @brief   设置中断向量表基地址
 * @param   baseAddr : 向量表基地址
 * @note    1. F1 系列要求 128 字节对齐
 *          2. FreeRTOS 重定位到 RAM 时直接传 RAM 基地址
 *          3. 地址非法时触发 assert
 */
void systemCoreSetVectorTable(uint32_t baseAddr)
{
    uint32_t alignedAddr = baseAddr & 0x1FFFFF80;   /* 128 字节对齐掩码 */

    if ((alignedAddr >= FLASH_BASE && alignedAddr < (FLASH_BASE + FLASH_SIZE)) ||
        (alignedAddr >= RAM_BASE && alignedAddr < (RAM_BASE + RAM_SIZE)))
    {
        SCB->VTOR = alignedAddr;
        __DSB();    /* 数据同步屏障 */
        __ISB();    /* 指令同步屏障，强制刷新指令流水线 */
    }
    else
    {
        assert_param(0);    /* 地址非法 */
    }
}

/**
 * @brief   进入待机模式
 * @note    掉电后系统复位
 */
void systemCoreEnterStandby(void)
{
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);  /* 清除唤醒标志 */
    HAL_PWR_EnterSTANDBYMode();
}

/**
 * @brief   系统软复位
 */
void systemCoreSoftReset(void)
{
    NVIC_SystemReset();
}

/**
 * @brief   获取 CPU ID
 * @retval  CPU ID 寄存器值
 */
uint32_t systemCoreGetCPUID(void)
{
    return SCB->CPUID;
}

/**
 * @brief   获取芯片 96 位唯一 ID
 * @param   uid_buf : 长度 3 的 uint32_t 数组
 * @note    顺序：uid_buf[0]=0x1FFFF7E8, uid_buf[1]=0x1FFFF7EC, uid_buf[2]=0x1FFFF7F0
 */
void System_GetUID(uint32_t uid_buf[3])
{
    if (uid_buf == NULL)
    {
        return;     /* 防空指针 */
    }

    /* volatile 读取防止编译器优化 */
    uid_buf[0] = *(volatile uint32_t *)(UID_BASE_ADDR + 0);
    uid_buf[1] = *(volatile uint32_t *)(UID_BASE_ADDR + 4);
    uid_buf[2] = *(volatile uint32_t *)(UID_BASE_ADDR + 8);
}
