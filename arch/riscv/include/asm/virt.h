/* QEMU virt 单 hart 平台的启动资源。 */
#ifndef _ASM_RISCV_VIRT_H
#define _ASM_RISCV_VIRT_H

#include <minux/memblock.h>

struct virt_irq_devices {
    uintptr_t plic_base;   // PLIC 寄存器的起始地址
    size_t plic_size;      // PLIC 占用的地址范围
    u32 plic_ndev;         // PLIC 支持的设备中断号上限
    u32 plic_s_context;    // 当前 CPU 的 S 模式 PLIC 上下文编号
    uintptr_t uart_base;   // UART 寄存器的起始地址
    size_t uart_size;      // UART 占用的地址范围
    u32 uart_irq;          // UART 在 PLIC 中的中断号
};

/* QEMU virt 提供 8 个 virtio-mmio 槽，前两段映射留给 PLIC 与 UART。 */
#define VIRT_IRQ_MMIO_COUNT 2
struct virt_devices {
	struct virt_irq_devices irq;
	struct memblock_region mmio[VIRT_IRQ_MMIO_COUNT + 8];
	size_t nr_mmio;
};

struct block_device;
void virt_probe_devices(const void *dtb, unsigned long hartid, struct virt_devices *devices);
int virt_probe_boot_disk(const struct virt_devices *devices, u32 clock_hz,
			 struct block_device **out);

#endif
