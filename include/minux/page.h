#ifndef _MINUX_PAGE_H
#define _MINUX_PAGE_H

#include <minux/types.h>

/*
 * 架构提供页大小和 RAM 线性映射，映射须保持页内偏移及地址连续性，
 * 并在 buddy 初始化前建立，在分配器运行期间保持有效。
 * arch_virt_to_phys() 对映射窗口之外的地址返回 false，RAM 范围由 buddy 检查。
 */
#include <asm/page.h>

/* 先除再向上取整，避免接近物理地址上限时溢出。 */
static inline u64 pfn_up(u64 address)
{
	return address / PAGE_SIZE + (address % PAGE_SIZE != 0);
}

#endif /* _MINUX_PAGE_H */
