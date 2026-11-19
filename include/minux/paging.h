#ifndef _MINUX_PAGING_H
#define _MINUX_PAGING_H

#include <minux/types.h>

struct memblock;
struct memblock_region;

/* 在 buddy 接管空闲页前建立启动映射，并保留页表占用的物理页。 */
void paging_init(struct memblock *map, const struct memblock_region *mmio,
		 size_t nr_mmio);

#endif /* _MINUX_PAGING_H */
