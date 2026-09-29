#ifndef __SYSTEM_HAL_H__
#define __SYSTEM_HAL_H__

#ifdef __cplusplus
extern "C"{
#endif

#include "stm32f4xx.h"

#define SYSTEM_INLINE   inline //定义系统内联函数为强制内联

#define SYS_SUPPORT_OS          1

/*
    时钟配置结构体
*/
typedef struct{
    uint32_t system_freq;       /*系统时钟频率 */
    uint32_t hclk_freq;         /* AHB时钟频率 */
    uint32_t pclk1_freq;        /* APB1时钟频率 */
    uint32_t pclk2_freq;        /* APB2时钟频率 */
    uint32_t pllMDemultiplier;  /* PLL预分频系数 */
    uint32_t pllNMultiplier;    /* PLL倍频系数N */
    uint32_t pllPDemultiplier;  /* PLL倍频系数P */
    uint32_t pllQDemultiplier;  /* PLL倍频系数Q */
}SystemClockConfig_t;           

/* 函数声明 ----------------------------------------------------------------*/
void systemCoreInit(void);
void systemCoreClockDeinit(void);
const SystemClockConfig_t* systemCoreGetClockConfig(void);
void systemCoreSetVectorTable(uint32_t baseAddr);
void systemCoreEnterStandby(void);
void systemCoreSoftReset(void);
uint32_t systemCoreGetCPUID(void);
void System_GetUID(uint32_t uid_buf[3]);

SYSTEM_INLINE void systemCoreWaitForIRQ(void){
    __WFI();    //等待中断
}

SYSTEM_INLINE void systemCoreWaiteForEvent(void){
    __WFE();    //等待事件
}

#ifdef __cplusplus
}
#endif

#endif /* __SYSTEM_HAL_H__ */











