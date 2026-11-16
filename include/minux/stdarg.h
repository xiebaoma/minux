/*
 * 可变参数。对照 Linux 的 include/linux/stdarg.h。
 *
 * 可变参数怎么取，取决于调用约定（参数放在哪些寄存器、栈上怎么排），
 * 只有编译器知道，所以这些全部是 GCC 的内建功能，不依赖任何库。
 */
#ifndef _MINUX_STDARG_H
#define _MINUX_STDARG_H

typedef __builtin_va_list va_list;

#define va_start(ap, last)	__builtin_va_start(ap, last)
#define va_arg(ap, type)	__builtin_va_arg(ap, type)
#define va_end(ap)		__builtin_va_end(ap)
#define va_copy(dst, src)	__builtin_va_copy(dst, src)

#endif /* _MINUX_STDARG_H */
