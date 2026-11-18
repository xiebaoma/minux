#ifndef _MINUX_PAGE_INTERNAL_H
#define _MINUX_PAGE_INTERNAL_H

#include <minux/list.h>

enum page_state {
	PAGE_RESERVED,		/* buddy 不管理的保留页 */
	PAGE_BLOCK_TAIL,	/* 空闲或已分配块的内部页 */
	PAGE_FREE_HEAD,		/* 空闲块首帧 */
	PAGE_ALLOCATED_HEAD,	/* 已分配块首帧 */
};

enum page_owner {
	PAGE_OWNER_BUDDY,	/* alloc_pages() 的直接调用者 */
	PAGE_OWNER_SLAB,	/* 小对象共享的 order-0 页 */
	PAGE_OWNER_KMALLOC,	/* kmalloc() 的大对象 */
};

struct page {
	/* 空闲块首帧使用链表节点，已分配块首帧使用 owner。 */
	union {
		struct list_head buddy_link;
		enum page_owner owner;
	};
	u64 pfn;
	enum page_state state;
	unsigned int order;
};

#endif /* _MINUX_PAGE_INTERNAL_H */
