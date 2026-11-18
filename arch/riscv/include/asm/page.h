#ifndef _ASM_RISCV_PAGE_H
#define _ASM_RISCV_PAGE_H

#include <minux/types.h>

#define PAGE_SHIFT 12
#define PAGE_SIZE  (1UL << PAGE_SHIFT)

/* v0.3 尚未启用分页，RAM 使用恒等映射。 */
static inline void *arch_phys_to_virt(u64 phys)
{
	return (void *)(uintptr_t)phys;
}

static inline bool arch_virt_to_phys(const void *addr, u64 *phys)
{
	*phys = (uintptr_t)addr;
	return true;
}

#endif /* _ASM_RISCV_PAGE_H */
