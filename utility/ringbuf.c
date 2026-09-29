/**
 * @file    ringbuf.c
 * @brief   环形缓冲区实现（2 的幂掩码取模，容量 size-1）
 * @note    写指针为生产者独占，读指针为消费者独占，互不交叉更新，
 *          单生产者单消费者模型下无需加锁（与 usart.c 的用法一致）。
 */
#include "ringbuf.h"
#include <string.h>

void ringbuf_init(ringbuf_t *rb, uint8_t *buf, uint16_t size)
{
    rb->buf = buf;
    rb->size = size;
    rb->mask = size - 1;
    rb->read = 0;
    rb->write = 0;
}

uint16_t ringbuf_available(const ringbuf_t *rb)
{
    return (uint16_t)((rb->write - rb->read) & rb->mask);
}

uint16_t ringbuf_free(const ringbuf_t *rb)
{
    /* 保留 1 空位判满：可写空间 = (read - write - 1) */
    return (uint16_t)((rb->read - rb->write - 1) & rb->mask);
}

uint8_t ringbuf_is_full(const ringbuf_t *rb)
{
    return (((rb->write + 1) & rb->mask) == rb->read) ? 1 : 0;
}

uint8_t ringbuf_is_empty(const ringbuf_t *rb)
{
    return (rb->read == rb->write) ? 1 : 0;
}

uint16_t ringbuf_read_contig(const ringbuf_t *rb)
{
    uint16_t avail = ringbuf_available(rb);
    uint16_t contig = (uint16_t)(rb->size - rb->read);

    return (avail < contig) ? avail : contig;
}

uint8_t ringbuf_write_byte(ringbuf_t *rb, uint8_t b)
{
    if (ringbuf_is_full(rb))
    {
        return 0;
    }

    rb->buf[rb->write] = b;
    rb->write = (uint16_t)((rb->write + 1) & rb->mask);

    return 1;
}

uint8_t ringbuf_read_byte(ringbuf_t *rb, uint8_t *out)
{
    if (ringbuf_is_empty(rb))
    {
        return 0;
    }

    *out = rb->buf[rb->read];
    rb->read = (uint16_t)((rb->read + 1) & rb->mask);

    return 1;
}

uint16_t ringbuf_write(ringbuf_t *rb, const uint8_t *src, uint16_t len)
{
    uint16_t free = ringbuf_free(rb);
    uint16_t n = (len < free) ? len : free;
    uint16_t firstSeg;

    if (n == 0)
    {
        return 0;
    }

    /* 环形回绕：先搬到缓冲末尾，剩余回绕到缓冲头 */
    firstSeg = (uint16_t)(rb->size - rb->write);
    if (n > firstSeg)
    {
        memcpy(&rb->buf[rb->write], src, firstSeg);
        memcpy(rb->buf, &src[firstSeg], (size_t)(n - firstSeg));
    }
    else
    {
        memcpy(&rb->buf[rb->write], src, n);
    }

    rb->write = (uint16_t)((rb->write + n) & rb->mask);

    return n;
}

uint16_t ringbuf_read(ringbuf_t *rb, uint8_t *dst, uint16_t len)
{
    uint16_t avail = ringbuf_available(rb);
    uint16_t n = (len < avail) ? len : avail;
    uint16_t firstSeg;

    if (n == 0)
    {
        return 0;
    }

    firstSeg = (uint16_t)(rb->size - rb->read);
    if (n > firstSeg)
    {
        memcpy(dst, &rb->buf[rb->read], firstSeg);
        memcpy(&dst[firstSeg], rb->buf, (size_t)(n - firstSeg));
    }
    else
    {
        memcpy(dst, &rb->buf[rb->read], n);
    }

    rb->read = (uint16_t)((rb->read + n) & rb->mask);

    return n;
}

void ringbuf_advance_read(ringbuf_t *rb, uint16_t n)
{
    rb->read = (uint16_t)((rb->read + n) & rb->mask);
}

void ringbuf_set_write(ringbuf_t *rb, uint16_t idx)
{
    rb->write = (uint16_t)(idx & rb->mask);
}

void ringbuf_discard_all(ringbuf_t *rb)
{
    rb->read = rb->write;
}
