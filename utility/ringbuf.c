/**
 * @file    ringbuf.c
 * @brief   环形缓冲区实现（2 的幂掩码取模，容量 size-1）
 * @note    写指针为生产者独占，读指针为消费者独占，互不交叉更新，
 *          单生产者单消费者模型下无需加锁（与 usart.c 的用法一致）。
 */
#include "ringbuf.h"
#include <string.h>

/**
 * @brief   初始化环形缓冲区
 * @param   rb   : 缓冲区对象
 *   @arg   buf  : 底层存储（由调用方提供，DMA 可直接写入）
 *   @arg   size : 存储大小，必须是 2 的幂（掩码取模的前提）
 * @note    容量为 size-1：保留一个空位用来区分"满"和"空"两种状态，
 *          否则读写指针相等既可能是空也可能是满
 */
void ringbuf_init(ringbuf_t *rb, uint8_t *buf, uint16_t size)
{
    rb->buf = buf;
    rb->size = size;
    rb->mask = size - 1;
    rb->read = 0;
    rb->write = 0;
}

/**
 * @brief   当前可读字节数
 * @param   rb : 缓冲区对象
 * @retval  未读字节数（0 ~ size-1）
 */
uint16_t ringbuf_available(const ringbuf_t *rb)
{
    return (uint16_t)((rb->write - rb->read) & rb->mask);
}

/**
 * @brief   当前可写字节数
 * @param   rb : 缓冲区对象
 * @retval  剩余可写空间（0 ~ size-1）
 * @note    保留 1 空位判满：可写空间 = (read - write - 1)
 */
uint16_t ringbuf_free(const ringbuf_t *rb)
{
    return (uint16_t)((rb->read - rb->write - 1) & rb->mask);
}

/**
 * @brief   缓冲区是否已满
 * @param   rb : 缓冲区对象
 * @retval  1 已满；0 未满
 */
uint8_t ringbuf_is_full(const ringbuf_t *rb)
{
    return (((rb->write + 1) & rb->mask) == rb->read) ? 1 : 0;
}

/**
 * @brief   缓冲区是否为空
 * @param   rb : 缓冲区对象
 * @retval  1 空；0 非空
 */
uint8_t ringbuf_is_empty(const ringbuf_t *rb)
{
    return (rb->read == rb->write) ? 1 : 0;
}

/**
 * @brief   从读指针起的连续可读长度
 * @param   rb : 缓冲区对象
 * @retval  不跨越缓冲末尾的连续字节数
 * @note    供 DMA 一次性搬运：DMA 需要一段物理连续的内存，
 *          回绕部分必须留到下一轮再搬
 */
uint16_t ringbuf_read_contig(const ringbuf_t *rb)
{
    uint16_t avail = ringbuf_available(rb);
    uint16_t contig = (uint16_t)(rb->size - rb->read);

    return (avail < contig) ? avail : contig;
}

/**
 * @brief   写入 1 字节
 * @param   rb : 缓冲区对象
 *   @arg   b  : 待写入字节
 * @retval  1 写入成功；0 缓冲已满
 */
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

/**
 * @brief   读出 1 字节
 * @param   rb  : 缓冲区对象
 *   @arg   out : 读出字节的存放处
 * @retval  1 读取成功；0 缓冲为空
 */
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

/**
 * @brief   批量写入（自动处理回绕）
 * @param   rb  : 缓冲区对象
 *   @arg   src : 源数据
 *   @arg   len : 期望写入字节数
 * @retval  实际写入字节数（空间不足时小于 len，不会部分覆盖已读数据）
 */
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

/**
 * @brief   批量读出（自动处理回绕）
 * @param   rb  : 缓冲区对象
 *   @arg   dst : 目的缓冲
 *   @arg   len : 期望读取字节数
 * @retval  实际读取字节数（数据不足时小于 len）
 */
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

/**
 * @brief   消费指针前移 n 字节
 * @param   rb : 缓冲区对象
 *   @arg   n  : 前移字节数
 * @note    由 DMA 发送完成中断调用：数据已被 DMA 读走，但要等搬运结束
 *          才能推进读指针，否则会在传输途中被覆盖
 */
void ringbuf_advance_read(ringbuf_t *rb, uint16_t n)
{
    rb->read = (uint16_t)((rb->read + n) & rb->mask);
}

/**
 * @brief   直接设定生产指针
 * @param   rb  : 缓冲区对象
 *   @arg   idx : 新的写位置（按掩码取模）
 * @note    用于 RX 侧：DMA 在循环模式下自行推进，CPU 只能通过
 *          NDTR 剩余计数反推出它写到了哪里
 */
void ringbuf_set_write(ringbuf_t *rb, uint16_t idx)
{
    rb->write = (uint16_t)(idx & rb->mask);
}

/**
 * @brief   丢弃全部未读数据（读指针追平写指针）
 * @param   rb : 缓冲区对象
 */
void ringbuf_discard_all(ringbuf_t *rb)
{
    rb->read = rb->write;
}
