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
/* 共享 order-0 数据页。分配时持有一个引用，最后一次 put_page 归还物理页。 */
bool get_page(struct page *page);
void put_page(struct page *page);
unsigned int page_ref_count(const struct page *page);
u64 page_to_phys(const struct page *page);

/* 返回 RAM 线性映射中的页首地址。 */
void *page_address(const struct page *page);
/* 查找地址所在的单页。线性映射或 RAM 范围之外返回 NULL。 */
struct page *virt_to_page(const void *addr);

u64 buddy_free_pages(void);
u64 buddy_free_blocks(unsigned int order);

#endif /* _MINUX_BUDDY_H */
