#ifndef _ASM_RISCV_PAGE_H
#define _ASM_RISCV_PAGE_H

#include <minux/types.h>

#define PAGE_SHIFT 12
#define PAGE_SIZE  (1UL << PAGE_SHIFT)

/* 先除再向上取整，避免接近物理地址上限时溢出。 */
static inline u64 pfn_up(u64 address)
{
	return address / PAGE_SIZE + (address % PAGE_SIZE != 0);
}

#endif /* _ASM_RISCV_PAGE_H */
