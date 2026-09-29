/**
 * @file    ringbuf.h
 * @brief   环形缓冲区（字节流，2 的幂掩码取模）接口
 * @note    单生产者单消费者模型：读写指针由各自一侧独占更新，
 *          调用方需保证生产者/消费者各只有一个上下文访问。
 *          缓冲大小必须为 2 的幂，容量为 size-1（保留 1 空位判满）。
 */
#ifndef __RINGBUF_H__
#define __RINGBUF_H__

#include <stdint.h>
#include <stddef.h>

typedef struct
{
    uint8_t *buf;               /* 缓冲基地址（由调用方提供，DMA 可直接写入） */
    uint16_t size;              /* 缓冲大小（2 的幂） */
    uint16_t mask;              /* 掩码 = size - 1 */
    volatile uint16_t read;     /* 消费者读指针 */
    volatile uint16_t write;    /* 生产者写指针 */
} ringbuf_t;

/* 初始化（size 必须为 2 的幂） */
void ringbuf_init(ringbuf_t *rb, uint8_t *buf, uint16_t size);

/* 当前可读字节数 */
uint16_t ringbuf_available(const ringbuf_t *rb);

/* 当前可写字节数（保留 1 空位判满，故最多 size-1） */
uint16_t ringbuf_free(const ringbuf_t *rb);

/* 是否已满 / 是否为空 */
uint8_t ringbuf_is_full(const ringbuf_t *rb);
uint8_t ringbuf_is_empty(const ringbuf_t *rb);

/* 从读指针起的连续可读长度（供 DMA 一次性搬运到缓冲末尾） */
uint16_t ringbuf_read_contig(const ringbuf_t *rb);

/* 单字节读写：成功返回 1，满/空返回 0 */
uint8_t ringbuf_write_byte(ringbuf_t *rb, uint8_t b);
uint8_t ringbuf_read_byte(ringbuf_t *rb, uint8_t *out);

/* 批量读写：返回实际写入/读取的字节数（自动处理回绕） */
uint16_t ringbuf_write(ringbuf_t *rb, const uint8_t *src, uint16_t len);
uint16_t ringbuf_read(ringbuf_t *rb, uint8_t *dst, uint16_t len);

/* 消费指针前移 n 字节（如 DMA 发送完成后由完成中断调用） */
void ringbuf_advance_read(ringbuf_t *rb, uint16_t n);

/* 直接设定生产指针（如 RX 由 NDTR 计数反推 DMA 写入位置） */
void ringbuf_set_write(ringbuf_t *rb, uint16_t idx);

/* 丢弃全部未读数据（读指针追平写指针） */
void ringbuf_discard_all(ringbuf_t *rb);

#endif /* __RINGBUF_H__ */
