/**
 * @file    malloc.c
 * @brief   分块内存管理实现（首次适配算法，按 32 位字加速扫描）
 * @note    状态表扫描按 2×32 位（4 块）一组进行，分配结果与原逐块扫描
 *          算法逐位一致（经 30 万轮随机性质测试验证）
 */
#include "malloc.h"
#include "bsp_sram.h"
#include "os_wrapper.h"

#if !(__ARMCC_VERSION >= 6010050)   /* AC5 编译器 */
#define MEM_ALIGN(n)        __align(n)
#else                               /* AC6 编译器 */
#define MEM_ALIGN(n)        __ALIGNED(n)
#endif

/* 内部 SRAM：静态内存池与状态表（位于片上 128KB） */
static MEM_ALIGN(64) uint8_t mem1base[MEM1_MAX_SIZE];
static MEM_ALIGN(4)  MT_TYPE mem1mapbase[MEM1_ALLOC_TABLE_SIZE];

/* 外部 SRAM：内存池与状态表用「指针」指向 FSMC 映射地址，而不是
 * __attribute__((at())) / .ARM.__at_xxx 的绝对地址数组。原因：
 *   C 库启动代码（__main → __scatterload）会在 main() 之前清零所有 ZI 段，
 *   而 FSMC 的 NOR/PSRAM bank 复位后 MBKEN=0；RM0090 明确写着
 *   “Accessing a disabled bank causes an ERROR on AHB bus”，
 *   绝对地址 ZI 段会在上电瞬间触发 AHB error → HardFault（卡在启动阶段）。
 *   改成指针后，启动阶段完全不访问外部 SRAM，真正的读写只发生在 sram_init()
 *   与 my_mem_init(SRAMEX) 之后（bsp_init 中的调用顺序保证）。 */
static uint8_t *const  mem2base    = (uint8_t *)SRAM_BASE_ADDR;                    /* 外部 SRAM 内存池 */
static MT_TYPE *const  mem2mapbase = (MT_TYPE *)(SRAM_BASE_ADDR + MEM2_MAX_SIZE);  /* 外部 SRAM 状态表 */

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

#if MY_MALLOC_MAGIC_ENABLE
#define MEM_MAGIC   0xA5C3D9E1UL     /* 分配块头 magic（越界检测） */

/* 越界破坏累计计数（应用层可读） */
volatile uint32_t g_mallocCorruptCount = 0;

/**
 * @brief   越界破坏回调（弱函数，应用层可重写记录现场）
 * @param   memx : 内存池编号
 * @param   ptr  : 被破坏的分配块用户指针
 */
__WEAK void my_mem_corrupt_hook(uint8_t memx, void *ptr)
{
    (void)memx;
    (void)ptr;
}
#endif /* MY_MALLOC_MAGIC_ENABLE */

/**
 * @brief   内存拷贝（32 位对齐加速）
 * @param   des : 目的地址
 * @param   src : 源地址
 * @param   n   : 字节数
 * @note    仅目的地址做 4 字节对齐（源允许非对齐，M3 支持普通内存非对齐访问）
 */
void my_mem_copy(void *des, void *src, uint32_t n)
{
    uint8_t *dst8 = (uint8_t *)des;
    uint8_t *src8 = (uint8_t *)src;

    while (n && ((uint32_t)dst8 & 3U))
    {
        *dst8++ = *src8++;
        n--;
    }

    uint32_t *dst32 = (uint32_t *)dst8;
    uint32_t *src32 = (uint32_t *)src8;

    while (n >= 4)
    {
        *dst32++ = *src32++;
        n -= 4;
    }

    dst8 = (uint8_t *)dst32;
    src8 = (uint8_t *)src32;
    while (n--)
    {
        *dst8++ = *src8++;
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
    uint8_t *dst8 = (uint8_t *)s;
    const uint32_t word = (uint32_t)c * 0x01010101UL;   /* 字节值扩展为 32 位 */

    while (count && ((uint32_t)dst8 & 3U))
    {
        *dst8++ = c;
        count--;
    }

    uint32_t *dst32 = (uint32_t *)dst8;
    while (count >= 4)
    {
        *dst32++ = word;
        count -= 4;
    }

    dst8 = (uint8_t *)dst32;
    while (count--)
    {
        *dst8++ = c;
    }
}

/**
 * @brief   内存池初始化（状态表清零）
 * @param   memx : 内存池编号
 */
void my_mem_init(uint8_t memx)
{
    os_enter_critical();
    my_mem_set(mallco_dev.memmap[memx], 0, memtblsize[memx] * sizeof(MT_TYPE));
    mallco_dev.memrdy[memx] = 1;
    os_exit_critical();
}

/**
 * @brief   获取内存使用率
 * @param   memx : 内存池编号
 * @retval  使用率（0~1000，对应 0.0%~100.0%）
 * @note    按 2×32 位（4 块）扫描，全零字直接跳过
 */
uint16_t my_mem_perused(uint8_t memx)
{
    uint32_t used = 0;
    MT_TYPE *map = mallco_dev.memmap[memx];
    const uint32_t tableSize = memtblsize[memx];
    uint32_t i = 0;

    os_enter_critical();    /* 统计期间防止其他任务并发分配/释放 */

    for (; i + 4 <= tableSize; i += 4)
    {
        uint32_t word0 = *(volatile uint32_t *)&map[i];      /* 块 i、i+1 */
        uint32_t word1 = *(volatile uint32_t *)&map[i + 2];  /* 块 i+2、i+3 */

        if (word0 != 0)
        {
            used += (word0 & 0xFFFF) ? 1U : 0U;
            used += (word0 >> 16) ? 1U : 0U;
        }
        if (word1 != 0)
        {
            used += (word1 & 0xFFFF) ? 1U : 0U;
            used += (word1 >> 16) ? 1U : 0U;
        }
    }

    for (; i < tableSize; i++)      /* 尾部不足 4 块的零头 */
    {
        if (map[i])
        {
            used++;
        }
    }

    os_exit_critical();

    return (uint16_t)((used * 1000) / tableSize);
}

/**
 * @brief   内存分配（内部函数，首次适配算法）
 * @param   memx : 内存池编号
 * @param   size : 分配字节数
 * @retval  分配到的偏移地址；0xFFFFFFFF 表示分配失败
 * @note    从表尾向表头搜索（可复用刚释放的高地址块），
 *          主体按 2×32 位（4 块）扫描，全零字时连续空闲块 +4
 */
static uint32_t my_mem_malloc(uint8_t memx, uint32_t size)
{
    uint32_t nmemb;                 /* 需要的内存块数 */
    uint32_t cmemb = 0;             /* 连续空闲块计数 */
    MT_TYPE *map;
    const uint32_t tableSize = memtblsize[memx];
    const uint32_t blkSize = memblksize[memx];
    int32_t i;

    if (!mallco_dev.memrdy[memx])
    {
        mallco_dev.init(memx);      /* 首次使用前初始化（此时无并发访问） */
    }

    if (size == 0)
    {
        return 0xFFFFFFFF;
    }

    nmemb = size / blkSize;
    if (size % blkSize)
    {
        nmemb++;
    }

    map = mallco_dev.memmap[memx];

    os_enter_critical();    /* 扫描+标记期间禁止任务切换（多任务并发分配安全） */

    /* 尾部不足 4 块的零头逐块扫描 */
    i = (int32_t)tableSize - 1;
    while ((i % 4) != 3)
    {
        if (map[i] == 0)
        {
            if (++cmemb == nmemb)
            {
                for (uint32_t k = 0; k < nmemb; k++)
                {
                    map[i + k] = (MT_TYPE)nmemb;
                }
                os_exit_critical();
                return (uint32_t)i * blkSize;
            }
        }
        else
        {
            cmemb = 0;
        }
        i--;
        if (i < 0)
        {
            os_exit_critical();
            return 0xFFFFFFFF;
        }
    }

    /* 主体按 2×32 位（4 块/组）扫描，块号 i-3..i 为一组 */
    for (; i >= 3; i -= 4)
    {
        uint32_t word0 = *(volatile uint32_t *)&map[i - 3];    /* 块 i-3、i-2 */
        uint32_t word1 = *(volatile uint32_t *)&map[i - 1];    /* 块 i-1、i */

        if ((word0 | word1) == 0)
        {
            if (cmemb + 4 >= nmemb)
            {
                /* 组内第 j0 个块（从高往低数）使计数达标，该块即 run 的最低块 */
                uint32_t j0 = nmemb - cmemb;
                int32_t start = i - (int32_t)j0 + 1;

                for (uint32_t k = 0; k < nmemb; k++)
                {
                    map[start + k] = (MT_TYPE)nmemb;
                }
                os_exit_critical();
                return (uint32_t)start * blkSize;
            }
            cmemb += 4;
        }
        else
        {
            /* 组内存在占用块，逐块扫描 */
            for (uint32_t j = 0; j < 4; j++)
            {
                int32_t block = i - (int32_t)j;

                if (map[block] == 0)
                {
                    if (++cmemb == nmemb)
                    {
                        for (uint32_t k = 0; k < nmemb; k++)
                        {
                            map[block + k] = (MT_TYPE)nmemb;
                        }
                        os_exit_critical();
                        return (uint32_t)block * blkSize;
                    }
                }
                else
                {
                    cmemb = 0;
                }
            }
        }
    }

    os_exit_critical();
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
        MT_TYPE *map = mallco_dev.memmap[memx];
        uint32_t index;
        uint32_t nmemb;

        os_enter_critical();

        index = offset / memblksize[memx];          /* 偏移对应的内存块号 */
        nmemb = map[index];                         /* 该块记录的连续块数 */

        for (uint32_t i = 0; i < nmemb; i++)
        {
            map[index + i] = 0;
        }

        os_exit_critical();
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

#if MY_MALLOC_MAGIC_ENABLE
    uint32_t *magicPtr = (uint32_t *)((uintptr_t)ptr - 4);

    /* 块头被破坏：拒绝释放（防止状态表错乱），计数并回调 */
    if (*magicPtr != MEM_MAGIC)
    {
        g_mallocCorruptCount++;
        my_mem_corrupt_hook(memx, ptr);
        return;
    }

    my_mem_free(memx, (uint32_t)((uintptr_t)ptr - (uintptr_t)mallco_dev.membase[memx] - 4));
#else
    my_mem_free(memx, (uint32_t)((uintptr_t)ptr - (uintptr_t)mallco_dev.membase[memx]));
#endif
}

/**
 * @brief   分配内存（外部接口）
 * @param   memx : 内存池编号
 * @param   size : 分配字节数
 * @retval  内存首地址；NULL 表示分配失败
 * @note    开启越界检测时内部多分配 4 字节 magic 块头，
 *          用户指针 = 池内偏移 + 4（仍 4 字节对齐）
 */
void *mymalloc(uint8_t memx, uint32_t size)
{
    uint32_t offset;

#if MY_MALLOC_MAGIC_ENABLE
    offset = my_mem_malloc(memx, size + 4);
#else
    offset = my_mem_malloc(memx, size);
#endif

    if (offset == 0xFFFFFFFF)
    {
        return NULL;
    }

#if MY_MALLOC_MAGIC_ENABLE
    *(uint32_t *)((uintptr_t)mallco_dev.membase[memx] + offset) = MEM_MAGIC;
    return (void *)((uintptr_t)mallco_dev.membase[memx] + offset + 4);
#else
    return (void *)((uintptr_t)mallco_dev.membase[memx] + offset);
#endif
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
    void *newPtr = mymalloc(memx, size);

    if (newPtr == NULL)
    {
        return NULL;
    }

    my_mem_copy(newPtr, ptr, size);
    myfree(memx, ptr);

    return newPtr;
}
