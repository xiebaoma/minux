/* QEMU virt 单 hart 平台中，与外部中断有关的设备树信息。 */
#ifndef _ASM_RISCV_VIRT_H
#define _ASM_RISCV_VIRT_H

#include <minux/types.h>

struct virt_irq_devices {
    uintptr_t plic_base;   // PLIC 寄存器的起始地址
    size_t plic_size;      // PLIC 占用的地址范围
    u32 plic_ndev;         // PLIC 支持的设备中断号上限
    u32 plic_s_context;    // 当前 CPU 的 S 模式 PLIC 上下文编号
    uintptr_t uart_base;   // UART 寄存器的起始地址
    size_t uart_size;      // UART 占用的地址范围
    u32 uart_irq;          // UART 在 PLIC 中的中断号
};

void virt_probe_irq_devices(const void *dtb, unsigned long hartid,
			    struct virt_irq_devices *devices);

#endif /* _ASM_RISCV_VIRT_H */
