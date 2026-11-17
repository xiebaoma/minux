/* QEMU virt 的 ns16550a 串口，仅支持接收中断与字符回显。 */
#ifndef _ASM_RISCV_UART_H
#define _ASM_RISCV_UART_H

#include <asm/virt.h>

void uart_init(const struct virt_irq_devices *devices);
u32 uart_irq_number(void);
void uart_handle_interrupt(void);

#endif /* _ASM_RISCV_UART_H */
