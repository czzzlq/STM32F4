/**
  ******************************************************************************
  * @file       system_hal.c
  * @author     czzzlq
  * @version    V0.1
  * @date       2025年12月13日
  * @brief      system驱动
  * @attention  仅供学习使用
  * @note       仅适用于STM32F103ZET6
  * @version    
  *             -V0.1
  ******************************************************************************
**/
#include "system_hal.h"

/*
    针对STM32F03XET6定义内存相关的宏
*/
#define FLASH_SIZE  (512 * 1024U)
#define RAM_BASE    0X20000000U     //RAM起始地址
#define RAM_SIZE    (64 * 1024U)

static SystemClockConfig_t g_systemClockConfigStruct;   //文件私有变量

/**
 * @brief   使能HSE时钟
 * @param   NULL
 * @retval  NULL
 */
static SYSTEM_INLINE void systemCoreEnableHSE(void){
    RCC->CR |= RCC_CR_HSEON;    //开启HSE振荡器
    while(!(RCC->CR & RCC_CR_HSERDY));  //循环检验HSRON位
}

static SYSTEM_INLINE void systemCoreEnablePLL(uint8_t pllMultiplier){
    // 1. 先禁用PLL（关键步骤：确保PLL处于关闭状态才能修改参数）
    RCC->CR &= ~RCC_CR_PLLON;  // 清除PLLON位，禁用PLL
    while (RCC->CR & RCC_CR_PLLRDY);  // 等待PLLRDY位清零（确认PLL已关闭）

    // 2. 修改PLL参数（倍频系数、时钟源）
    RCC->CFGR &= ~RCC_CFGR_PLLMULL;   // 清除旧的倍频配置
    RCC->CFGR |= (pllMultiplier - 2) << 18;  // 设置新的倍频系数（位值=倍频数-2）
    RCC->CFGR |= RCC_CFGR_PLLSRC;   // 选择HSE作为PLL输入时钟

    // 3. 重新使能PLL，并等待锁定
    RCC->CR |= RCC_CR_PLLON;   // 使能PLL
    while (!(RCC->CR & RCC_CR_PLLRDY));  // 等待PLL锁定（PLLRDY置位）
}

/**
 * @brief   将系统时钟切换值PLLCLK
 * @param   NULL
 * @retval  NULL
 */
static SYSTEM_INLINE void systemCoreSwitchToPll(void){
    RCC->CFGR &= ~RCC_CFGR_SW;  //清除SW位段
    RCC->CFGR |= RCC_CFGR_SW_PLL;   //设置SW位段，将PLL输出作为系统时钟
    while((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SW_PLL);   //读取SWS位段检验SW位段
}

/**
 * @brief   配置振荡器和时钟树
 * @param   NULL
 * @retval  NULL
 */
static void systemCoreClockInit(uint32_t pllMultipler){
    RCC_OscInitTypeDef RCC_OscInitStructure = {0};                  //Osc振荡器初始化配置
    RCC_ClkInitTypeDef RCC_ClkInitStructure = {0};                  //时钟树初始化配置

    //配置HSE
    RCC_OscInitStructure.OscillatorType = RCC_OSCILLATORTYPE_HSE;   //配置HSE振荡器
    RCC_OscInitStructure.HSEState       = RCC_HSE_ON;               //开启HSE振荡器
    RCC_OscInitStructure.HSEPredivValue = RCC_HSE_PREDIV_DIV1;      //HSE上电默认不分频
    RCC_OscInitStructure.PLL.PLLState   = RCC_PLL_ON;               //开启PLL
    RCC_OscInitStructure.PLL.PLLSource  = RCC_PLLSOURCE_HSE;        //PLL的时钟源为HSE
    RCC_OscInitStructure.PLL.PLLMUL     = pllMultipler;             //PLL倍频系数
    HAL_RCC_OscConfig(&RCC_OscInitStructure);                       //初始化RCC振荡器

    //配置时钟树
    RCC_ClkInitStructure.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | 
                                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;            
    RCC_ClkInitStructure.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;    //将PLLCLK设置为系统时钟源
    RCC_ClkInitStructure.AHBCLKDivider = RCC_SYSCLK_DIV1;           //将AHB预分频器设置为1分频即不分频
    RCC_ClkInitStructure.APB1CLKDivider = RCC_HCLK_DIV2;            //APB1预分频器设置为2分频
    RCC_ClkInitStructure.APB2CLKDivider = RCC_HCLK_DIV1;            //APB2预分频设置为1分频
    HAL_RCC_ClockConfig(&RCC_ClkInitStructure,FLASH_LATENCY_2);

    //更新配置
    g_systemClockConfigStruct.pll_multipliter = pllMultipler;
    g_systemClockConfigStruct.system_freq = HSE_VALUE * pllMultipler;
    g_systemClockConfigStruct.hclk_freq = g_systemClockConfigStruct.system_freq;
    g_systemClockConfigStruct.pclk1_freq = g_systemClockConfigStruct.system_freq / 2;
    g_systemClockConfigStruct.pclk2_freq = g_systemClockConfigStruct.system_freq;
}

/**
 * @brief   系统初始化
 * @param   NULL
 * @retval  NULL
 */
void systemCoreInit(void){
    HAL_Init(); //HAL库初始化
    systemCoreClockInit(RCC_PLL_MUL9);  //配置系统时钟为72MHz
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
    g_systemClockConfigStruct.hclk_freq = 0;
    g_systemClockConfigStruct.pclk1_freq = 0;
    g_systemClockConfigStruct.pclk2_freq = 0;
    g_systemClockConfigStruct.pll_multipliter = 0;
    g_systemClockConfigStruct.system_freq = 0;
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

    vtorMask = 0x1FFFFF80; // 128字节对齐（低7位全部置零）

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
 * @brief 获取STM32F1芯片完整96位唯一ID
 * @param uid_buf 指向长度为3的uint32_t数组，用于存储96位UID（3个32位）
 * @note 数组顺序：uid_buf[0]=0x1FFFF7E8, uid_buf[1]=0x1FFFF7EC, uid_buf[2]=0x1FFFF7F0
 */
void System_GetUID(uint32_t uid_buf[3]) {
    if (uid_buf == NULL) {
        return; // 防止空指针
    }
    // 宏定义UID地址，提升可读性（F1专属）
    #define STM32F1_UID_BASE_ADDR 0x1FFFF7E8U
    // 读取3个32位，组成完整96位UID（volatile防止编译器优化）
    uid_buf[0] = *(volatile uint32_t*)(STM32F1_UID_BASE_ADDR + 0);
    uid_buf[1] = *(volatile uint32_t*)(STM32F1_UID_BASE_ADDR + 4);
    uid_buf[2] = *(volatile uint32_t*)(STM32F1_UID_BASE_ADDR + 8);
}
