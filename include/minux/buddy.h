#ifndef _MINUX_BUDDY_H
#define _MINUX_BUDDY_H

#include <minux/types.h>

/* order n 表示 2^n 个连续页，最大阶数包含在范围内。 */
#define BUDDY_MAX_ORDER 10

struct memblock;
struct page;

/* 启动时调用一次，先在 map 中保留页描述符，再接管剩余空闲页。 */
bool buddy_init(struct memblock *map);

/* 单 hart 普通内核上下文使用。返回自然对齐的连续块首帧，失败返回 NULL。 */
struct page *alloc_pages(unsigned int order);
/* 传入原块首帧及分配时的 order。参数无效时返回 false。 */
bool free_pages(struct page *page, unsigned int order);
u64 page_to_phys(const struct page *page);

u64 buddy_free_pages(void);
u64 buddy_free_blocks(unsigned int order);

#endif /* _MINUX_BUDDY_H */
