#ifndef _ASM_RISCV_IO_H
#define _ASM_RISCV_IO_H

#include <asm/page.h>

//告诉设备缓冲区在哪
/* QEMU virt 的 DMA 一致且与 RAM 物理地址相同。缓冲区由驱动连续分配。 */
static inline bool dma_address(const void *ptr, u64 *address)
{
	return arch_virt_to_phys(ptr, address);
}

//保证读写顺序
static inline void io_barrier(void)
{
	asm volatile("fence iorw, iorw" ::: "memory");
}

//读取硬件时间计数
static inline u64 io_clock_ticks(void)
{
	u64 now;
	asm volatile("rdtime %0" : "=r"(now));
	return now;
}

#endif
