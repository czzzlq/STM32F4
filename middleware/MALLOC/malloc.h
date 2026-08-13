/**
 * @file    malloc.h
 * @brief   分块内存管理接口（内部/外部 SRAM 内存池）
 * @note    内存池大小配置见本文件；外部 SRAM 基地址见 bsp/bsp_sram.h
 */
#ifndef __MALLOC_H__
#define __MALLOC_H__

#include "system_hal.h"
#include "common_types.h"
#include <stddef.h>

/* 未定义 NULL 时兜底（兼容不同标准库实现） */
#ifndef NULL
#define NULL 0
#endif

/* 内存池编号 */
#define SRAMIN      0       /* 内部 SRAM */
#define SRAMEX      1       /* 外部 SRAM */

#define SRAMBANK    2       /* 支持的内存池数量 */

/* 内存块类型：块计数超过 65535 时需改为 uint32_t（内部 SRAM 用 uint16_t 可省内存） */
#define MT_TYPE     uint16_t

/* 内存池大小计算：
 * 总占用 = 池大小 + (池大小 / 块大小) * sizeof(MT_TYPE)
 * mem1：内部 SRAM（F103 共 64KB，留出全局变量/栈空间） */
#define MEM1_BLOCK_SIZE         32
#define MEM1_MAX_SIZE           (30 * 1024)
#define MEM1_ALLOC_TABLE_SIZE   (MEM1_MAX_SIZE / MEM1_BLOCK_SIZE)

/* mem2：外部 SRAM（按实际容量配置） */
#define MEM2_BLOCK_SIZE         32
#define MEM2_MAX_SIZE           (1 * 32)
#define MEM2_ALLOC_TABLE_SIZE   (MEM2_MAX_SIZE / MEM2_BLOCK_SIZE)

/* 内存管理结构体 */
struct _m_mallco_dev
{
    void (*init)(uint8_t);          /* 初始化 */
    uint16_t (*perused)(uint8_t);   /* 内存使用率 */
    uint8_t *membase[SRAMBANK];     /* 内存池基地址 */
    MT_TYPE *memmap[SRAMBANK];      /* 内存块状态表 */
    uint8_t  memrdy[SRAMBANK];      /* 内存池就绪标志 */
};

extern struct _m_mallco_dev mallco_dev;

/* 越界检测：分配块头部放置 magic number，释放时校验。
 * 检测到破坏时拒绝释放该块（防止状态表错乱）并递增 g_mallocCorruptCount，
 * 同时回调 my_mem_corrupt_hook（应用层可重写记录现场） */
#define MY_MALLOC_MAGIC_ENABLE      1

extern volatile uint32_t g_mallocCorruptCount;  /* 越界破坏累计计数 */
__WEAK void my_mem_corrupt_hook(uint8_t memx, void *ptr);

/* 用户接口（分配/释放/统计均经临界区保护，多任务安全；不可在中断中调用） */
void my_mem_init(uint8_t memx);                     /* 内存池初始化 */
uint16_t my_mem_perused(uint8_t memx);              /* 内存使用率（0~1000，对应 0.0%~100.0%） */
void my_mem_set(void *s, uint8_t c, uint32_t count);/* 内存设置 */
void my_mem_copy(void *des, void *src, uint32_t n); /* 内存拷贝 */

void myfree(uint8_t memx, void *ptr);                       /* 内存释放 */
void *mymalloc(uint8_t memx, uint32_t size);                /* 内存分配 */
void *myrealloc(uint8_t memx, void *ptr, uint32_t size);    /* 重新分配内存 */

#endif /* __MALLOC_H__ */
