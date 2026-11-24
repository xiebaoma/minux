/*
 * 整数类型的取值范围（lp64）。对照 Linux 的 include/linux/limits.h。
 *
 * 最小值写成 (-MAX - 1)：直接写 -2147483648 时，2147483648 先被当成正数常量，
 * 超出 int 范围，类型会变成 long，再取负，结果的类型就不对了。
 */
#ifndef _MINUX_LIMITS_H
#define _MINUX_LIMITS_H

#define PIPE_BUF 4096

#define INT_MAX		2147483647
#define INT_MIN		(-INT_MAX - 1)
#define UINT_MAX	4294967295U

#define LONG_MAX	9223372036854775807L
#define LONG_MIN	(-LONG_MAX - 1)
#define ULONG_MAX	18446744073709551615UL

#define LLONG_MAX	9223372036854775807LL
#define LLONG_MIN	(-LLONG_MAX - 1)
#define ULLONG_MAX	18446744073709551615ULL

#define SIZE_MAX	ULONG_MAX

#endif /* _MINUX_LIMITS_H */
