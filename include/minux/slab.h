#ifndef _MINUX_SLAB_H
#define _MINUX_SLAB_H

#include <minux/types.h>

/* 启动时在 buddy_init() 之后调用。 */
void slab_init(void);

/*
 * 单 hart 普通内核上下文使用。返回至少 16 字节对齐的地址。
 * 零长度、超出 buddy 最大块或分配失败时返回 NULL。
 */
void *kmalloc(size_t size);
/* 清零请求的 size 字节。 */
void *kzalloc(size_t size);

/* ptr 是 kmalloc()/kzalloc() 返回的尚未释放的地址，或 NULL。 */
void kfree(const void *ptr);

#endif /* _MINUX_SLAB_H */
