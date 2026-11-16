/*
 * 控制台输出接口：通用代码（printk）通过它输出，具体怎么输出由架构实现。
 *
 * 现在只有一个“早期控制台”：RISC-V 上用 SBI 调用让 OpenSBI 代为输出，
 * 实现在 arch/riscv/early_console.c。v0.2 写好 UART 驱动后，会改成
 * Linux 那样可以注册多个控制台（struct console + register_console）。
 */
#ifndef _MINUX_CONSOLE_H
#define _MINUX_CONSOLE_H

#include <minux/types.h>

void early_console_write(const char *s, size_t n);

#endif /* _MINUX_CONSOLE_H */
