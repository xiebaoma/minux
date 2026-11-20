#ifndef _ASM_RISCV_SV39_H
#define _ASM_RISCV_SV39_H

#include <asm/page.h>

#define SV39_LEVELS 3
#define SV39_VPN_BITS 9
#define SV39_LOW_END (1UL << (PAGE_SHIFT + SV39_LEVELS * SV39_VPN_BITS - 1))
#define SV39_HIGH_START (-SV39_LOW_END)

enum sv39_prot {
	SV39_READ  = 1U << 1,
	SV39_WRITE = 1U << 2,
	SV39_EXEC  = 1U << 3,
};

struct arch_pgd;
struct memblock;
struct sv39_table;

static inline u64 sv39_level_size(unsigned int level)
{
	return PAGE_SIZE << (level * SV39_VPN_BITS);
}

struct sv39_table *sv39_alloc_root(struct memblock *map);
struct arch_pgd *sv39_alloc_user_root(const struct sv39_table *kernel);

/* 在尚未启用的页表中填一个叶项。冲突或分配失败时保留原页表与保留区。 */
bool sv39_map_leaf(struct sv39_table *root, struct memblock *map, uintptr_t va,
		   u64 pa, unsigned int level, unsigned int prot);
/* 解析页内的任意字节地址，pa 和 prot 均可传 NULL。 */
bool sv39_lookup(const struct sv39_table *root, uintptr_t va, u64 *pa,
		 unsigned int *prot);
bool sv39_enable(u64 root_pa);
void sv39_clear_low_half(struct sv39_table *root);

#endif /* _ASM_RISCV_SV39_H */
