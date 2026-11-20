#ifndef _ASM_RISCV_PAGE_H
#define _ASM_RISCV_PAGE_H

#include <minux/types.h>

#define PAGE_SHIFT 12
#define PAGE_SIZE  (1UL << PAGE_SHIFT)

/* 分页前使用物理地址，切换后使用高半区线性映射。 */
void *arch_phys_to_virt(u64 phys);
bool arch_virt_to_phys(const void *addr, u64 *phys);

#endif /* _ASM_RISCV_PAGE_H */
