/*
 * 内存与字符串操作。签名与 C 标准库一致。
 * 注意：即使不显式调用，GCC 也可能为结构体赋值、大数组清零等
 * 自动生成对 memcpy/memset 的调用，所以这几个函数是内核的必需品。
 */
#ifndef _MINUX_STRING_H
#define _MINUX_STRING_H

#include <minux/types.h>

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);

#endif /* _MINUX_STRING_H */
