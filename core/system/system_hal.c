#include "system_hal.h"

/*
    针对STM32F407ZGT6定义内存相关的宏
*/
#define FLASH_SIZE  (1024 * 1024U)
#define RAM_BASE    0X20000000U     //RAM起始地址
#define RAM_SIZE    (128 * 1024U)

static SystemClockConfig_t g_systemClockConfigStruct;   //文件私有变量

/* 说明：下面 3 个寄存器级时钟辅助函数是 F1→F4 迁移期的实现，
 * 当前系统时钟统一由 HAL_RCC_OscConfig/HAL_RCC_ClockConfig 配置（见 systemCoreClockInit），
 * 保留备用故加 unused 属性，避免编译告警淹没真实问题 */

/**
 * @brief   使能HSE时钟
 * @param   NULL
 * @retval  NULL
 */
__attribute__((unused))
static SYSTEM_INLINE void systemCoreEnableHSE(void){
    RCC->CR |= RCC_CR_HSEON;    //开启HSE振荡器
    while(!(RCC->CR & RCC_CR_HSERDY));  //循环检验HSRON位
}

/**
 * @brief   配置PLL（STM32F4系列）
 * @param   pllMDemultiplier：PLL预分频系数（2~63，VCO输入 = 源时钟 / PLLM）
 * @param   pllNMultiplier：  PLL倍频系数（192~432，VCO输出 = VCO输入 × PLLN）
 * @param   pllPDemultiplier：PLL系统时钟分频（仅支持2/4/6/8，SYSCLK = VCO输出 / PLLP）
 * @param   pllQDemultiplier：PLL48MHz时钟分频（2~15，PLL48CK = VCO输出 / PLLQ）
 * @retval  0：配置成功；1：参数无效；2：PLL锁定失败
 */
__attribute__((unused))
static uint8_t systemCoreEnablePLL(uint32_t pllMDemultiplier, uint32_t pllNMultiplier,
                                  uint32_t pllPDemultiplier, uint32_t pllQDemultiplier)
{
    // 1. 参数有效性检查（避免无效配置）
    if (pllMDemultiplier < 2 || pllMDemultiplier > 63) return 1;  // PLLM范围：2~63
    if (pllNMultiplier < 192 || pllNMultiplier > 432) return 1;   // PLLN范围（F4系列）：192~432
    if (pllPDemultiplier != 2 && pllPDemultiplier != 4 && 
        pllPDemultiplier != 6 && pllPDemultiplier != 8) return 1;  // PLLP仅支持2/4/6/8
    if (pllQDemultiplier < 2 || pllQDemultiplier > 15) return 1;   // PLLQ范围：2~15

    // 2. 禁用PLL（确保修改参数前PLL已关闭）
    RCC->CR &= ~RCC_CR_PLLON;                  // 清除PLLON位，禁用PLL
    while (RCC->CR & RCC_CR_PLLRDY);           // 等待PLLRDY位清零（PLL完全关闭）

    // 3. 清除PLL旧配置（PLLM/PLLN/PLLP/PLLQ）
    RCC->PLLCFGR &= ~(RCC_PLLCFGR_PLLM_Msk | 
                      RCC_PLLCFGR_PLLN_Msk | 
                      RCC_PLLCFGR_PLLP_Msk | 
                      RCC_PLLCFGR_PLLQ_Msk);

    // 4. 配置新参数
    RCC->PLLCFGR |= pllMDemultiplier;                          // 配置PLLM（bit0~5）
    RCC->PLLCFGR |= (pllNMultiplier << 6);                      // 配置PLLN（bit6~14）
    RCC->PLLCFGR |= ((pllPDemultiplier / 2 - 1) << 16);         // 配置PLLP（bit16~17）
    RCC->PLLCFGR |= (pllQDemultiplier << 24);                   // 配置PLLQ（bit24~27）
    RCC->PLLCFGR |= RCC_PLLCFGR_PLLSRC_HSE;                     // 选择HSE作为PLL输入源（bit22）

    // 5. 使能PLL并等待锁定
    RCC->CR |= RCC_CR_PLLON;                   // 置位PLLON，使能PLL
    uint32_t timeout = 0xFFFF;                 // 超时保护（避免死循环）
    while (!(RCC->CR & RCC_CR_PLLRDY) && timeout--);
    if (timeout == 0) return 2;                // PLL锁定失败

    return 0;  // 配置成功
}

/**
 * @brief   将系统时钟切换值PLLCLK
 * @param   NULL
 * @retval  NULL
 */
__attribute__((unused))
static SYSTEM_INLINE void systemCoreSwitchToPll(void){
    RCC->CFGR &= ~RCC_CFGR_SW;  //清除SW位段
    RCC->CFGR |= RCC_CFGR_SW_PLL;   //设置SW位段，将PLL输出作为系统时钟
    while((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SW_PLL);   //读取SWS位段检验SW位段
    //设置时钟等待周期
    FLASH->ACR &= ~FLASH_ACR_LATENCY;
    FLASH->ACR |= FLASH_ACR_LATENCY_5WS;   
}

/**
 * @brief   配置振荡器和时钟树
 * @param   NULL
 * @retval  NULL
 */
static void systemCoreClockInit(uint32_t pllMDemultiplier, uint32_t pllNMultiplier,
                                  uint32_t pllPDemultiplier, uint32_t pllQDemultiplier)
{
    RCC_OscInitTypeDef RCC_OscInitStructure = {0};                  //Osc振荡器初始化配置
    RCC_ClkInitTypeDef RCC_ClkInitStructure = {0};                  //时钟树初始化配置

    __HAL_RCC_PWR_CLK_ENABLE();                                     //使能PWR时钟
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);  //配置调压器

    //配置HSE
    RCC_OscInitStructure.OscillatorType = RCC_OSCILLATORTYPE_HSE;   //配置HSE振荡器
    RCC_OscInitStructure.HSEState       = RCC_HSE_ON;               //开启HSE振荡器
    RCC_OscInitStructure.PLL.PLLState   = RCC_PLL_ON;               //开启PLL
    RCC_OscInitStructure.PLL.PLLSource  = RCC_PLLSOURCE_HSE;        //PLL的时钟源为HSE
    RCC_OscInitStructure.PLL.PLLM       = pllMDemultiplier;         //PLL预分频系数
    RCC_OscInitStructure.PLL.PLLN       = pllNMultiplier;           //PLL倍频系数
    RCC_OscInitStructure.PLL.PLLP       = pllPDemultiplier;         //PLL分频系数P
    RCC_OscInitStructure.PLL.PLLQ       = pllQDemultiplier;         //PLL分频系数Q

    HAL_RCC_OscConfig(&RCC_OscInitStructure);                       //初始化RCC振荡器

    //配置时钟树
    RCC_ClkInitStructure.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | 
                                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;            
    RCC_ClkInitStructure.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;    //将PLLCLK设置为系统时钟源
    RCC_ClkInitStructure.AHBCLKDivider = RCC_SYSCLK_DIV1;           //将AHB预分频器设置为1分频即不分频
    RCC_ClkInitStructure.APB1CLKDivider = RCC_HCLK_DIV4;            //APB1预分频器设置为2分频
    RCC_ClkInitStructure.APB2CLKDivider = RCC_HCLK_DIV2;            //APB2预分频设置为1分频
    HAL_RCC_ClockConfig(&RCC_ClkInitStructure,FLASH_LATENCY_5);

    /* STM32F405x/407X/415x/417x支持预期功能 */
    if(HAL_GetDEVID() == 0x1001){
        __HAL_FLASH_PREFETCH_BUFFER_ENABLE();                       //使能FLASH预取
    }

    //更新配置
    g_systemClockConfigStruct.pllMDemultiplier = pllMDemultiplier;
    g_systemClockConfigStruct.pllNMultiplier = pllNMultiplier;
    g_systemClockConfigStruct.pllPDemultiplier = pllPDemultiplier;
    g_systemClockConfigStruct.pllQDemultiplier = pllQDemultiplier;
    g_systemClockConfigStruct.system_freq = HSE_VALUE / pllMDemultiplier * pllNMultiplier / pllPDemultiplier;
    g_systemClockConfigStruct.hclk_freq = g_systemClockConfigStruct.system_freq;
    g_systemClockConfigStruct.pclk1_freq = g_systemClockConfigStruct.system_freq / 4;
    g_systemClockConfigStruct.pclk2_freq = g_systemClockConfigStruct.system_freq / 2;
}

/**
 * @brief   系统初始化
 * @param   NULL
 * @retval  NULL
 */
void systemCoreInit(void){
    HAL_Init(); //HAL库初始化
    systemCoreClockInit(8,336,2,7);  //配置系统时钟为168MHz
    HAL_SYSTICK_Config(HAL_RCC_GetHCLKFreq() / 1000);   //配置SysTick相关寄存器，初始化SysTick时基
    SystemCoreClockUpdate();    //配置时钟树必须更新系统时钟频率
}

/**
 * @brief   时钟反初始化
 * @param   NULL
 * @retval  NULL
 * @note    复位所有外设，为系统复位或低功耗做准备
 */
void systemCoreClockDeinit(void){
    //复位RCC配置
    HAL_RCC_DeInit();

    //清除时钟配置
    g_systemClockConfigStruct.pllMDemultiplier = 0;
    g_systemClockConfigStruct.pllNMultiplier = 0;
    g_systemClockConfigStruct.pllPDemultiplier = 0;
    g_systemClockConfigStruct.pllQDemultiplier = 0;
    g_systemClockConfigStruct.system_freq = 0;
    g_systemClockConfigStruct.hclk_freq = 0;
    g_systemClockConfigStruct.pclk1_freq = 0;
    g_systemClockConfigStruct.pclk2_freq = 0;
}

/**
 * @brief   获取时钟配置
 * @param   NULL
 * @retval  时钟配置结构体指针
 * @note    NULL
 */
const SystemClockConfig_t* systemCoreGetClockConfig(void){
    return &g_systemClockConfigStruct;
}

/**
 * @brief   设置中断向量表
 * @param   baseAddr:向量表基地址
 * @retval  NULL
 * @note    1.自动适配F1/F4/L4/H7等系列的对齐要求
 *          2.FreeRTOS重定位到RAM时，直接传RAM基地址即可
 *          3.包含地址合法性检查和同步屏障，避免异常
 */
void systemCoreSetVectorTable(uint32_t baseAddr){
    uint32_t vtorMask;  //对齐掩码

    vtorMask = 0x1FFFFF00; // 256字节对齐（低8位全部置零）

    //地址对齐
    uint32_t alignedAddr = baseAddr & vtorMask;

    //合法性检查
    if((alignedAddr >= FLASH_BASE && alignedAddr < (FLASH_BASE + FLASH_SIZE)) ||
        (alignedAddr >= RAM_BASE && alignedAddr < (RAM_BASE + RAM_SIZE)))
    {
        SCB->VTOR = alignedAddr;
        __DSB();    //数据同步屏障
        __ISB();    //指令同步屏障，强制刷新指令流水线
    }else{
        assert_param(0);    //地址非法，触发断言
    }
}

/**
 * @brief   进入待机模式
 * @param   NULL
 * @retval  NULL
 * @note    唤醒后系统将复位
 */
void systemCoreEnterStandby(void){
    //清除唤醒标志
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    //进入待机模式
    HAL_PWR_EnterSTANDBYMode();
}

/**
 * @brief   系统软复位
 * @param   NULL
 * @retval  NULL
 * @note    
 */
void systemCoreSoftReset(void){
    NVIC_SystemReset(); //系统复位
}

/**
 * @brief   获取CPU ID
 * @param   NULL
 * @retval  CPU ID
 * @note    
 */
uint32_t systemCoreGetCPUID(void){
    return SCB->CPUID;
}

/**
 * @brief 获取STM32F4芯片完整96位唯一ID
 * @param uid_buf 指向长度为3的uint32_t数组，用于存储96位UID（3个32位）
 * @note 
 */
void System_GetUID(uint32_t uid_buf[3]) {
    if (uid_buf == NULL) {
        return; // 防止空指针
    }
    // 宏定义UID地址，提升可读性（STM32F4 的 96 位唯一 ID 基地址）
    #define STM32F4_UID_BASE_ADDR 0x1FFF7A10U
    // 读取3个32位，组成完整96位UID（volatile防止编译器优化）
    uid_buf[0] = *(volatile uint32_t*)(STM32F4_UID_BASE_ADDR + 0);
    uid_buf[1] = *(volatile uint32_t*)(STM32F4_UID_BASE_ADDR + 4);
    uid_buf[2] = *(volatile uint32_t*)(STM32F4_UID_BASE_ADDR + 8);
}
