#ifndef _ASM_RISCV_PAGE_H
#define _ASM_RISCV_PAGE_H

#include <minux/types.h>

#define PAGE_SHIFT 12
#define PAGE_SIZE  (1UL << PAGE_SHIFT)

/* 启动时与启用 Sv39 后，内核可访问的 RAM 使用恒等地址。 */
static inline void *arch_phys_to_virt(u64 phys)
{
	return (void *)(uintptr_t)phys;
}

bool arch_virt_to_phys(const void *addr, u64 *phys);

#endif /* _ASM_RISCV_PAGE_H */
