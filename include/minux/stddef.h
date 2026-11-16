/*
 * offsetof：结构体成员相对于结构体起始地址的字节偏移。
 * 对照 Linux 的 include/linux/stddef.h。
 */
#ifndef _MINUX_STDDEF_H
#define _MINUX_STDDEF_H

#include <minux/types.h>

#undef offsetof
#if defined(__GNUC__) || defined(__clang__)
/*
 * 编译器内建：结果是“整数常量表达式”，可以用在 _Static_assert、
 * case 标签、数组长度里；也没有未定义行为。GCC 和 Clang 都支持。
 */
#define offsetof(TYPE, MEMBER)	__builtin_offsetof(TYPE, MEMBER)
#else
/*
 * 经典写法：把 0 当成一个 TYPE 对象的地址，取 MEMBER 的地址，
 * 得到的“地址”数值就是偏移量。只算地址，不读写内存。
 */
#define offsetof(TYPE, MEMBER)	((size_t)&((TYPE *)0)->MEMBER)
#endif

#endif /* _MINUX_STDDEF_H */