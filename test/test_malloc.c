/**
 * @file    test_malloc.c
 * @brief   malloc 分配器主机侧回归测试（mingw gcc 编译，PC 上运行）
 * @note    覆盖：
 *          1. 按字扫描分配器 vs 朴素逐块首次适配：随机分配/释放序列
 *             结果（偏移+状态表）逐位一致
 *          2. my_mem_copy / my_mem_set 与 libc 参考实现对拍（含非对齐）
 *          3. magic 越界检测：破坏块头 -> 释放被拒 -> 计数递增
 *          用法：test/run_host_tests.sh
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "malloc.h"

#define TEST_ROUNDS     200000
#define ALLOC_TRACK     64      /* 同时跟踪的已分配块数 */

/* 与驱动状态表等价的参考模型（朴素从尾向头逐块扫描） */
static MT_TYPE s_refMap[MEM1_ALLOC_TABLE_SIZE];

static uint32_t naive_alloc(uint32_t nmemb)
{
    uint32_t cmemb = 0;

    for (int32_t offset = (int32_t)MEM1_ALLOC_TABLE_SIZE - 1; offset >= 0; offset--)
    {
        if (s_refMap[offset] == 0)
        {
            cmemb++;
        }
        else
        {
            cmemb = 0;
        }

        if (cmemb == nmemb)
        {
            for (uint32_t i = 0; i < nmemb; i++)
            {
                s_refMap[offset + i] = (MT_TYPE)nmemb;
            }
            return (uint32_t)offset * MEM1_BLOCK_SIZE;
        }
    }

    return 0xFFFFFFFF;
}

static void naive_free(uint32_t offset)
{
    uint32_t index = offset / MEM1_BLOCK_SIZE;
    uint32_t nmemb = s_refMap[index];

    for (uint32_t i = 0; i < nmemb; i++)
    {
        s_refMap[index + i] = 0;
    }
}

static uint32_t driver_map_compare(void)
{
    return memcmp(s_refMap, mallco_dev.memmap[SRAMIN],
                  MEM1_ALLOC_TABLE_SIZE * sizeof(MT_TYPE));
}

static int test_allocator_property(void)
{
    struct { uint8_t *ptr; uint32_t refOffset; uint8_t used; } track[ALLOC_TRACK] = {0};
    uint32_t allocCount = 0;
    uint32_t freeCount = 0;

    memset(s_refMap, 0, sizeof(s_refMap));

    for (uint32_t round = 0; round < TEST_ROUNDS; round++)
    {
        if ((rand() % 10) < 7 || allocCount == 0)    /* 70% 分配 */
        {
            uint32_t size = (uint32_t)(rand() % 600) + 1;
            uint32_t allocSize = size + 4;          /* 含 magic 头（与驱动一致） */
            uint32_t refOff = naive_alloc(allocSize / MEM1_BLOCK_SIZE +
                                          ((allocSize % MEM1_BLOCK_SIZE) ? 1 : 0));
            uint8_t *p = (uint8_t *)mymalloc(SRAMIN, size);

            if (refOff == 0xFFFFFFFF)
            {
                if (p != NULL) return 1;    /* 参考失败而驱动成功 */
                continue;                   /* 都失败：一致 */
            }

            if (p == NULL) return 2;        /* 驱动失败而参考成功 */

            /* 驱动用户指针 = 池基 + 偏移 + 4（magic 头） */
            uint32_t drvOff = (uint32_t)((uintptr_t)p -
                                         (uintptr_t)mallco_dev.membase[SRAMIN] - 4);
            if (drvOff != refOff) return 3;

            /* 状态表逐位一致 */
            if (driver_map_compare() != 0) return 4;

            /* 记录以便随机释放；跟踪表满时立即释放（两模型同步），
             * 避免产生未跟踪的合法泄漏干扰最终清零校验 */
            uint32_t slot = ALLOC_TRACK;
            for (uint32_t i = 0; i < ALLOC_TRACK; i++)
            {
                if (!track[i].used)
                {
                    slot = i;
                    break;
                }
            }

            if (slot < ALLOC_TRACK)
            {
                track[slot].used = 1;
                track[slot].ptr = p;
                track[slot].refOffset = refOff;
                allocCount++;
            }
            else
            {
                naive_free(refOff);
                myfree(SRAMIN, p);
                freeCount++;
            }
        }
        else                                            /* 30% 释放 */
        {
            uint32_t idx = (uint32_t)(rand() % ALLOC_TRACK);
            if (track[idx].used)
            {
                naive_free(track[idx].refOffset);
                myfree(SRAMIN, track[idx].ptr);
                track[idx].used = 0;
                allocCount--;
                freeCount++;

                if (driver_map_compare() != 0) return 5;
            }
        }
    }

    /* 释放全部剩余块，状态表应清零 */
    for (uint32_t i = 0; i < ALLOC_TRACK; i++)
    {
        if (track[i].used)
        {
            naive_free(track[i].refOffset);
            myfree(SRAMIN, track[i].ptr);
            track[i].used = 0;
        }
    }

    if (driver_map_compare() != 0) return 6;
    if (my_mem_perused(SRAMIN) != 0) return 7;

    printf("allocator property: PASS (%u rounds, %u frees)\n", TEST_ROUNDS, freeCount);
    return 0;
}

static int test_mem_copy_set(void)
{
    uint8_t src[300] __attribute__((aligned(8)));
    uint8_t dst[300] __attribute__((aligned(8)));

    for (uint32_t round = 0; round < 50000; round++)
    {
        uint32_t len = (uint32_t)(rand() % 280) + 1;
        uint32_t srcOff = (uint32_t)(rand() % 4);       /* 非对齐源 */
        uint32_t dstOff = (uint32_t)(rand() % 4);       /* 非对齐目的 */

        for (uint32_t i = 0; i < sizeof(src); i++)
        {
            src[i] = (uint8_t)(rand() & 0xFF);
        }

        my_mem_copy(dst + dstOff, src + srcOff, len);
        if (memcmp(dst + dstOff, src + srcOff, len) != 0) return 1;

        memset(dst, 0x00, sizeof(dst));
        my_mem_set(dst + dstOff, 0x5A, len);
        for (uint32_t i = 0; i < len; i++)
        {
            if (dst[dstOff + i] != 0x5A) return 2;
        }
        for (uint32_t i = 0; i < dstOff; i++)
        {
            if (dst[i] != 0x00) return 3;   /* 前缀未被破坏 */
        }
    }

    printf("mem_copy/set vs libc: PASS\n");
    return 0;
}

static int test_corrupt_detect(void)
{
    uint32_t before = g_mallocCorruptCount;
    uint8_t *p = (uint8_t *)mymalloc(SRAMIN, 32);

    if (p == NULL) return 1;

    *(uint32_t *)(p - 4) = 0;               /* 模拟越界写破坏块头 */
    myfree(SRAMIN, p);                      /* 应被拒绝释放 */

    if (g_mallocCorruptCount != before + 1) return 2;
    if (my_mem_perused(SRAMIN) == 0) return 3;   /* 被拒绝的块应仍占用 */

    printf("corrupt detect: PASS\n");
    return 0;
}

int main(void)
{
    int fail = 0;

    srand(20260813);
    my_mem_init(SRAMIN);

    fail |= test_allocator_property();
    fail |= test_mem_copy_set();
    fail |= test_corrupt_detect();

    if (fail)
    {
        printf("FAILED: %d\n", fail);
        return 1;
    }

    printf("ALL HOST TESTS PASSED\n");
    return 0;
}
