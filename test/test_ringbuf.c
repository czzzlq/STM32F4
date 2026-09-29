/**
 * @file    test_ringbuf.c
 * @brief   ringbuf 环形缓冲区主机侧回归测试（mingw gcc 编译，PC 上运行）
 * @note    覆盖：单字节/批量读写、回绕、满空判定（容量 size-1）、
 *          read_contig 连续段、set_write 模拟 DMA 写指针、advance_read/discard_all
 * @note    用法：test/run_host_tests.sh
 */
#include <stdio.h>
#include <string.h>
#include "ringbuf.h"

#define RB_SIZE     8       /* 2 的幂，容量 7 */

static uint8_t s_buf[RB_SIZE];
static ringbuf_t s_rb;

static int test_byte_rw(void)
{
    uint8_t out = 0;

    ringbuf_init(&s_rb, s_buf, RB_SIZE);
    if (!ringbuf_is_empty(&s_rb)) return 1;
    if (ringbuf_is_full(&s_rb)) return 2;

    if (ringbuf_write_byte(&s_rb, 0xAA) != 1) return 3;
    if (ringbuf_available(&s_rb) != 1) return 4;
    if (ringbuf_read_byte(&s_rb, &out) != 1 || out != 0xAA) return 5;
    if (!ringbuf_is_empty(&s_rb)) return 6;

    return 0;
}

static int test_full_empty(void)
{
    uint8_t out = 0;
    int i;

    ringbuf_init(&s_rb, s_buf, RB_SIZE);

    /* 容量为 size-1：写入 7 字节后满 */
    for (i = 0; i < RB_SIZE - 1; i++)
    {
        if (ringbuf_write_byte(&s_rb, (uint8_t)i) != 1) return 1;
    }
    if (!ringbuf_is_full(&s_rb)) return 2;
    if (ringbuf_free(&s_rb) != 0) return 3;

    /* 满时写入应失败 */
    if (ringbuf_write_byte(&s_rb, 0xFF) != 0) return 4;

    /* 读 1 字节后空出 1 位，可再写 */
    if (ringbuf_read_byte(&s_rb, &out) != 1 || out != 0) return 5;
    if (ringbuf_is_full(&s_rb)) return 6;
    if (ringbuf_write_byte(&s_rb, 0xEE) != 1) return 7;

    return 0;
}

static int test_bulk_wrap(void)
{
    uint8_t src[16];
    uint8_t dst[16];
    uint8_t expect[7];
    uint16_t i;

    for (i = 0; i < sizeof(src); i++) src[i] = (uint8_t)(i * 3);

    /* 直读直写（未回绕）：一次写 6 字节读回一致 */
    ringbuf_init(&s_rb, s_buf, RB_SIZE);
    if (ringbuf_write(&s_rb, src, 6) != 6) return 1;
    memset(dst, 0, sizeof(dst));
    if (ringbuf_read(&s_rb, dst, 6) != 6) return 2;
    if (memcmp(dst, src, 6) != 0) return 3;

    /* 回绕：写 5 读 2，再写 4（写指针 5->6->7->0->1 越过末尾），
     * 最终按序读出 src[2..8] 共 7 字节 */
    ringbuf_init(&s_rb, s_buf, RB_SIZE);
    ringbuf_write(&s_rb, src, 5);                /* 写 src[0..4]，write=5 */
    ringbuf_read(&s_rb, dst, 2);                 /* 读走 src[0..1]，read=2 */
    if (ringbuf_write(&s_rb, &src[5], 4) != 4) return 4;   /* 写 src[5..8]，write=1 */

    if (ringbuf_available(&s_rb) != 7) return 5;

    memset(dst, 0, sizeof(dst));
    if (ringbuf_read(&s_rb, dst, 7) != 7) return 6;
    for (i = 0; i < 7; i++) expect[i] = src[2 + i];     /* src[2..8] */
    if (memcmp(dst, expect, 7) != 0) return 7;
    if (!ringbuf_is_empty(&s_rb)) return 8;

    return 0;
}

static int test_read_contig(void)
{
    uint16_t i;

    ringbuf_init(&s_rb, s_buf, RB_SIZE);

    /* read=0 write=5：连续段应为 5（未回绕） */
    for (i = 0; i < 5; i++) ringbuf_write_byte(&s_rb, (uint8_t)i);
    if (ringbuf_read_contig(&s_rb) != 5) return 1;

    /* 前移读指针制造回绕：read=2 write=5，连续段应为 3（5-2） */
    ringbuf_advance_read(&s_rb, 2);
    if (ringbuf_read_contig(&s_rb) != 3) return 2;

    /* 写满 -> 读 3 -> 再写 3，使 write=2 < read=3（越末尾回绕），
     * 连续段应 = size - read = 5 */
    ringbuf_init(&s_rb, s_buf, RB_SIZE);
    for (i = 0; i < RB_SIZE - 1; i++) ringbuf_write_byte(&s_rb, (uint8_t)i);  /* write=7 */
    ringbuf_advance_read(&s_rb, 3);                                            /* read=3 */
    for (i = 0; i < 3; i++) ringbuf_write_byte(&s_rb, (uint8_t)(0xA0 + i));   /* write=2 */
    if (ringbuf_available(&s_rb) != 7) return 3;
    if (ringbuf_read_contig(&s_rb) != 5) return 4;

    return 0;
}

static int test_set_write_discard(void)
{
    uint8_t out = 0;

    ringbuf_init(&s_rb, s_buf, RB_SIZE);

    /* 模拟 DMA 已写入 3 字节（NDTR 反推写指针） */
    s_buf[0] = 0x11; s_buf[1] = 0x22; s_buf[2] = 0x33;
    ringbuf_set_write(&s_rb, 3);
    if (ringbuf_available(&s_rb) != 3) return 1;
    if (ringbuf_read_byte(&s_rb, &out) != 1 || out != 0x11) return 2;

    /* 丢弃全部未读 */
    ringbuf_discard_all(&s_rb);
    if (ringbuf_available(&s_rb) != 0) return 3;
    if (!ringbuf_is_empty(&s_rb)) return 4;

    /* 写指针回绕：set_write(size) 应折返为 0（掩码取模） */
    ringbuf_set_write(&s_rb, RB_SIZE);
    if (s_rb.write != 0) return 5;

    return 0;
}

int main(void)
{
    int fail = 0;

    fail |= test_byte_rw();
    fail |= test_full_empty();
    fail |= test_bulk_wrap();
    fail |= test_read_contig();
    fail |= test_set_write_discard();

    if (fail)
    {
        printf("ringbuf: FAILED (code %d)\n", fail);
        return 1;
    }

    printf("ringbuf: ALL TESTS PASSED\n");
    return 0;
}
