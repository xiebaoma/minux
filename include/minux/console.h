/*
 * 控制台输出接口：通用代码（printk）通过它输出，具体怎么输出由架构实现。
 *
 * printk 目前仍使用 SBI 早期控制台，实现在 arch/riscv/early_console.c。
 * UART 驱动在接收中断中将字节交给控制台 TTY 行规程。
 */
#ifndef _MINUX_CONSOLE_H
#define _MINUX_CONSOLE_H

#include <minux/types.h>

extern const struct stream_operations console_operations;
/* 接收端在本地中断关闭时调用。 */
void tty_receive_char(u8 ch);
void early_console_write(const char *s, size_t n);

#endif /* _MINUX_CONSOLE_H */
