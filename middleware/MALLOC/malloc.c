/**
 * @file    malloc.c
 * @brief   分块内存管理实现（首次适配算法）
 */
#include "malloc.h"
#include "bsp_sram.h"

#if !(__ARMCC_VERSION >= 6010050)   /* AC5 编译器 */

/* 内存池（64 字节对齐） */
static __align(64) uint8_t mem1base[MEM1_MAX_SIZE];                                     /* 内部 SRAM 内存池 */
static __align(64) uint8_t mem2base[MEM2_MAX_SIZE] __attribute__((at(SRAM_BASE_ADDR))); /* 外部 SRAM 内存池 */

/* 内存状态表 */
static MT_TYPE mem1mapbase[MEM1_ALLOC_TABLE_SIZE];                                                      /* 内部 SRAM 状态表 */
static MT_TYPE mem2mapbase[MEM2_ALLOC_TABLE_SIZE] __attribute__((at(SRAM_BASE_ADDR + MEM2_MAX_SIZE)));  /* 外部 SRAM 状态表 */

#else                               /* AC6 编译器（不支持 __attribute__((at))，用固定 section 名） */

#if SRAM_BASE_ADDR != 0x68000000
#error SRAM_BASE_ADDR changed! Please recalculate!
#endif

#if (SRAM_BASE_ADDR + MEM2_MAX_SIZE) != 0X68000020
#error SRAM_BASE_ADDR + MEM2_MAX_SIZE changed! Please recalculate!
#endif

/* 内存池（64 字节对齐） */
static __ALIGNED(64) uint8_t mem1base[MEM1_MAX_SIZE];                                                       /* 内部 SRAM 内存池 */
static __ALIGNED(64) uint8_t mem2base[MEM2_MAX_SIZE] __attribute__((section(".bss.ARM.__at_0X68000000")));  /* 外部 SRAM 内存池 */

/* 内存状态表 */
static MT_TYPE mem1mapbase[MEM1_ALLOC_TABLE_SIZE];                                                          /* 内部 SRAM 状态表 */
static MT_TYPE mem2mapbase[MEM2_ALLOC_TABLE_SIZE] __attribute__((section(".bss.ARM.__at_0X68000020")));     /* 外部 SRAM 状态表 */

#endif

/* 内存池参数表 */
const uint32_t memtblsize[SRAMBANK] = {MEM1_ALLOC_TABLE_SIZE, MEM2_ALLOC_TABLE_SIZE};
const uint32_t memblksize[SRAMBANK] = {MEM1_BLOCK_SIZE, MEM2_BLOCK_SIZE};
const uint32_t memsize[SRAMBANK] = {MEM1_MAX_SIZE, MEM2_MAX_SIZE};

/* 内存管理结构体 */
struct _m_mallco_dev mallco_dev =
{
    my_mem_init,                    /* 内存池初始化 */
    my_mem_perused,                 /* 内存使用率 */
    mem1base, mem2base,             /* 内存池基地址 */
    mem1mapbase, mem2mapbase,       /* 内存状态表 */
    0, 0,                           /* 就绪标志 */
};

/**
 * @brief   内存拷贝（32 位对齐加速）
 * @param   des : 目的地址
 * @param   src : 源地址
 * @param   n   : 字节数
 */
void my_mem_copy(void *des, void *src, uint32_t n)
{
    uint8_t *d8 = (uint8_t *)des;
    uint8_t *s8 = (uint8_t *)src;

    /* 头部对齐目的地址到 4 字节边界（源允许非对齐，M3 支持普通内存非对齐 LDR） */
    while (n && ((uint32_t)d8 & 3U))
    {
        *d8++ = *s8++;
        n--;
    }

    /* 主体 32 位拷贝 */
    uint32_t *d32 = (uint32_t *)d8;
    uint32_t *s32 = (uint32_t *)s8;

    while (n >= 4)
    {
        *d32++ = *s32++;
        n -= 4;
    }

    /* 尾部剩余字节 */
    d8 = (uint8_t *)d32;
    s8 = (uint8_t *)s32;
    while (n--)
    {
        *d8++ = *s8++;
    }
}

/**
 * @brief   内存设置（32 位写入加速）
 * @param   s     : 内存首地址
 * @param   c     : 要设置的值（低 8 位有效）
 * @param   count : 字节数
 */
void my_mem_set(void *s, uint8_t c, uint32_t count)
{
    uint8_t *x8 = (uint8_t *)s;
    const uint32_t word = (uint32_t)c * 0x01010101UL;   /* 字节值扩展为 32 位 */

    /* 头部对齐到 4 字节边界 */
    while (count && ((uint32_t)x8 & 3U))
    {
        *x8++ = c;
        count--;
    }

    /* 主体 32 位写入 */
    uint32_t *x32 = (uint32_t *)x8;
    while (count >= 4)
    {
        *x32++ = word;
        count -= 4;
    }

    /* 尾部剩余字节 */
    x8 = (uint8_t *)x32;
    while (count--)
    {
        *x8++ = c;
    }
}

/**
 * @brief   内存池初始化（状态表清零）
 * @param   memx : 内存池编号
 */
void my_mem_init(uint8_t memx)
{
    my_mem_set(mallco_dev.memmap[memx], 0, memtblsize[memx] * sizeof(MT_TYPE));
    mallco_dev.memrdy[memx] = 1;
}

/**
 * @brief   获取内存使用率
 * @param   memx : 内存池编号
 * @retval  使用率（0~1000，对应 0.0%~100.0%）
 */
uint16_t my_mem_perused(uint8_t memx)
{
    uint32_t used = 0;

    for (uint32_t i = 0; i < memtblsize[memx]; i++)
    {
        if (mallco_dev.memmap[memx][i])
        {
            used++;
        }
    }

    return (uint16_t)((used * 1000) / memtblsize[memx]);
}

/**
 * @brief   内存分配（内部函数，首次适配算法）
 * @param   memx : 内存池编号
 * @param   size : 分配字节数
 * @retval  分配到的偏移地址；0xFFFFFFFF 表示分配失败
 */
static uint32_t my_mem_malloc(uint8_t memx, uint32_t size)
{
    uint32_t nmemb;     /* 需要的内存块数 */
    uint32_t cmemb = 0; /* 连续空闲块计数 */

    if (!mallco_dev.memrdy[memx])
    {
        mallco_dev.init(memx);
    }

    if (size == 0)
    {
        return 0xFFFFFFFF;
    }

    nmemb = size / memblksize[memx];
    if (size % memblksize[memx])
    {
        nmemb++;
    }

    /* 从表尾向表头搜索（可复用刚释放的高地址块） */
    for (int32_t offset = (int32_t)memtblsize[memx] - 1; offset >= 0; offset--)
    {
        if (!mallco_dev.memmap[memx][offset])
        {
            cmemb++;                /* 连续空闲块 +1 */
        }
        else
        {
            cmemb = 0;              /* 遇到占用块，清零 */
        }

        if (cmemb == nmemb)         /* 找到 nmemb 块连续空闲内存 */
        {
            for (uint32_t i = 0; i < nmemb; i++)
            {
                mallco_dev.memmap[memx][offset + i] = (MT_TYPE)nmemb;
            }
            return (uint32_t)offset * memblksize[memx];
        }
    }

    return 0xFFFFFFFF;              /* 未找到足够大的连续内存 */
}

/**
 * @brief   内存释放（内部函数）
 * @param   memx   : 内存池编号
 * @param   offset : 内存偏移地址
 * @retval  0 释放成功；1 内存池未初始化；2 偏移超界
 */
static uint8_t my_mem_free(uint8_t memx, uint32_t offset)
{
    if (!mallco_dev.memrdy[memx])
    {
        mallco_dev.init(memx);
        return 1;
    }

    if (offset < memsize[memx])
    {
        uint32_t index = offset / memblksize[memx];         /* 偏移对应的内存块号 */
        uint32_t nmemb = mallco_dev.memmap[memx][index];    /* 该块记录的连续块数 */

        for (uint32_t i = 0; i < nmemb; i++)
        {
            mallco_dev.memmap[memx][index + i] = 0;
        }
        return 0;
    }

    return 2;   /* 偏移超界 */
}

/**
 * @brief   释放内存（外部接口）
 * @param   memx : 内存池编号
 * @param   ptr  : 内存首地址
 */
void myfree(uint8_t memx, void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }

    my_mem_free(memx, (uint32_t)ptr - (uint32_t)mallco_dev.membase[memx]);
}

/**
 * @brief   分配内存（外部接口）
 * @param   memx : 内存池编号
 * @param   size : 分配字节数
 * @retval  内存首地址；NULL 表示分配失败
 */
void *mymalloc(uint8_t memx, uint32_t size)
{
    uint32_t offset = my_mem_malloc(memx, size);

    if (offset == 0xFFFFFFFF)
    {
        return NULL;
    }

    return (void *)((uint32_t)mallco_dev.membase[memx] + offset);
}

/**
 * @brief   重新分配内存（外部接口：分配新块 -> 拷贝 -> 释放旧块）
 * @param   memx : 内存池编号
 * @param   ptr  : 旧内存首地址
 * @param   size : 新分配字节数
 * @retval  新内存首地址；NULL 表示分配失败
 */
void *myrealloc(uint8_t memx, void *ptr, uint32_t size)
{
    uint32_t offset = my_mem_malloc(memx, size);

    if (offset == 0xFFFFFFFF)
    {
        return NULL;
    }

    my_mem_copy((void *)((uint32_t)mallco_dev.membase[memx] + offset), ptr, size);
    myfree(memx, ptr);
    return (void *)((uint32_t)mallco_dev.membase[memx] + offset);
}
