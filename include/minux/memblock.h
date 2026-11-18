#ifndef _MINUX_MEMBLOCK_H
#define _MINUX_MEMBLOCK_H

#include <minux/types.h>

#define MEMBLOCK_MAX_REGIONS 32

/* 物理字节区间 [start, end)，按地址排序，重叠或相邻的区间已合并。 */
struct memblock_region {
	u64 start;
	u64 end;
};

struct memblock_type {
	unsigned int count;
	struct memblock_region regions[MEMBLOCK_MAX_REGIONS];
};

struct memblock {
	struct memblock_type memory;
	struct memblock_type reserved;
};

extern struct memblock boot_memblock;

/* 从零初始化的表开始。区间无效或超出表容量时返回 false。 */
bool memblock_add(struct memblock *map, u64 start, u64 size);
bool memblock_reserve(struct memblock *map, u64 start, u64 size);

u64 memblock_memory_pages(const struct memblock *map);

/* 按地址访问连续空闲页，[start_pfn, end_pfn) 使用物理页帧号。 */
typedef void (*memblock_free_visitor)(u64 start_pfn, u64 end_pfn, void *data);
void memblock_for_each_free_range(const struct memblock *map,
				  memblock_free_visitor visit, void *data);

#endif /* _MINUX_MEMBLOCK_H */
