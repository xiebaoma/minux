/*
 * panic：内核遇到无法继续运行的错误时调用，打印原因后停机，永不返回。
 * 对照 Linux 的 include/linux/panic.h，实现在 kernel/panic.c。
 */
#ifndef _MINUX_PANIC_H
#define _MINUX_PANIC_H

#include <minux/compiler.h>

void panic(const char *fmt, ...) __printf(1, 2) __noreturn;

#endif /* _MINUX_PANIC_H */
