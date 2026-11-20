#ifndef _MINUX_PAGING_H
#define _MINUX_PAGING_H

#include <minux/compiler.h>
#include <minux/types.h>

struct memblock;
struct memblock_region;

/* 在 buddy 接管空闲页前建立启动映射，切换后从 next 继续启动。 */
void paging_init(struct memblock *map, const struct memblock_region *mmio,
		 size_t nr_mmio, void (*next)(void)) __noreturn;

#endif /* _MINUX_PAGING_H */
