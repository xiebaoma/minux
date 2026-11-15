/*
 * 内核基本类型。内核不能用 libc 的 <stdint.h>/<stddef.h>，所以自己定义。
 *
 * 我们的 ABI 是 lp64：int 4 字节，long / long long / 指针 8 字节。
 * 定长类型的写法与 Linux 的 include/uapi/asm-generic/int-ll64.h 一致：
 * 64 位一律用 long long，这样在 32/64 位平台上 u64 都是同一个类型。
 */
#ifndef _MINUX_TYPES_H
#define _MINUX_TYPES_H

typedef unsigned char		u8;
typedef signed char		s8;
typedef unsigned short		u16;
typedef signed short		s16;
typedef unsigned int		u32;
typedef signed int		s32;
typedef unsigned long long	u64;
typedef signed long long	s64;

/* 与指针等宽的整数：lp64 下 long 和指针都是 8 字节 */
typedef unsigned long		size_t;
typedef signed long		ssize_t;
typedef unsigned long		uintptr_t;

typedef _Bool			bool;
enum { false = 0, true = 1 };

#define NULL ((void *)0)

/* 编译期自检：类型大小不对时直接编译失败，而不是运行时出怪问题 */
_Static_assert(sizeof(u8)  == 1, "u8 must be 1 byte");
_Static_assert(sizeof(u16) == 2, "u16 must be 2 bytes");
_Static_assert(sizeof(u32) == 4, "u32 must be 4 bytes");
_Static_assert(sizeof(u64) == 8, "u64 must be 8 bytes");
_Static_assert(sizeof(size_t) == sizeof(void *), "size_t must match pointer size");

#endif /* _MINUX_TYPES_H */
