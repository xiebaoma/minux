/*
 * 控制台输出接口：通用代码（printk）通过它输出，具体怎么输出由架构实现。
 *
 * printk 目前仍使用 SBI 早期控制台，实现在 arch/riscv/early_console.c。
 * v0.2 的 UART 驱动负责接收中断和输入回显；后续再引入可注册控制台。
 */
#ifndef _MINUX_CONSOLE_H
#define _MINUX_CONSOLE_H

#include <minux/types.h>

extern const struct stream_operations console_operations;
void early_console_write(const char *s, size_t n);

#endif /* _MINUX_CONSOLE_H */
