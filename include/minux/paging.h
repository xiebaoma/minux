#ifndef _MINUX_PAGING_H
#define _MINUX_PAGING_H

#include <minux/compiler.h>
#include <minux/types.h>

struct arch_pgd;
struct memblock;
struct memblock_region;
struct page;

/* 在 buddy 接管空闲页前建立启动映射，切换后从 next 继续启动。 */
void paging_init(struct memblock *map, const struct memblock_region *mmio,
		 size_t nr_mmio, void (*next)(void)) __noreturn;

/* 用户虚拟地址上界（不包含）。 */
uintptr_t arch_user_limit(void);

/* 运行时页表共享内核映射。只在页表未启用时修改或释放。 */
struct arch_pgd *arch_pgd_alloc(void);
/* 关本地中断时启用根表，并同步地址翻译。NULL 选择内核根表。 */
void arch_pgd_activate(struct arch_pgd *pgd);
/* 成功后接管一个 order-0 用户数据页，失败时页表和页归属不变。 */
bool arch_pgd_map(struct arch_pgd *pgd, uintptr_t va, struct page *page,
		  unsigned int vm_flags);
/* 释放根表、私有用户子表及其数据页。 */
void arch_pgd_free(struct arch_pgd *pgd);

#endif /* _MINUX_PAGING_H */
