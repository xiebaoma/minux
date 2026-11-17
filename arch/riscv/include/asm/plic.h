/* QEMU virt 上的 PLIC：把外部设备中断送到当前 S 模式 hart。 */
#ifndef _ASM_RISCV_PLIC_H
#define _ASM_RISCV_PLIC_H

#include <asm/virt.h>

/*
 * 记住 DTB 找到的 PLIC 地址、S 模式上下文和中断源数量，并把该上下文的
 * 优先级阈值设为 0。这里只准备中断控制器，还没有开启任何具体设备的 IRQ。
 */
void plic_init(const struct virt_irq_devices *devices);

/*
 * 让编号为 irq 的设备中断可以送到当前 S 模式 hart：将其优先级设为 1，
 * 再打开该上下文对应的 enable 位。UART 在当前 QEMU virt 上使用 IRQ 10。
 */
void plic_enable_irq(u32 irq);

/*
 * 从当前 S 模式上下文的 claim 寄存器认领一个待处理中断。
 * 返回设备 IRQ 编号；返回 0 表示目前没有可认领的中断。
 */
u32 plic_claim(void);

/*
 * 设备处理函数消除中断原因后，把刚才认领的 irq 写回完成寄存器。
 * 若遗漏这一步，PLIC 不会把该次中断视为处理完成。
 */
void plic_complete(u32 irq);

#endif /* _ASM_RISCV_PLIC_H */
