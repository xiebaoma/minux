/*
 * 格式化到字符串。对照 Linux 的 include/linux/sprintf.h，实现在 lib/vsprintf.c。
 */
#ifndef _MINUX_SPRINTF_H
#define _MINUX_SPRINTF_H

#include <minux/types.h>
#include <minux/stdarg.h>
#include <minux/compiler.h>

/*
 * 把格式化结果写进 buf，最多写 size 个字节（包括结尾的 '\0'）。
 * 返回值：如果 buf 足够大，“本应”写出的字符数（不含 '\0'）。
 * 所以返回值 >= size 就说明被截断了。这是 C99 的语义，Linux 也一样。
 */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
int snprintf(char *buf, size_t size, const char *fmt, ...) __printf(3, 4);

#endif /* _MINUX_SPRINTF_H */
