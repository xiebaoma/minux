#ifndef _MINUX_IO_H
#define _MINUX_IO_H

#include <minux/byteorder.h>
#include <asm/io.h>

static inline u32 mmio_read32(uintptr_t addr)
{
	u32 value = *(volatile u32 *)addr;
	io_barrier();
	return le32_to_cpu(value);
}

static inline void mmio_write32(uintptr_t addr, u32 value)
{
	io_barrier();
	*(volatile u32 *)addr = cpu_to_le32(value);
	io_barrier();
}

#endif
