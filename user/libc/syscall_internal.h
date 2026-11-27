#ifndef MINUX_LIBC_SYSCALL_INTERNAL_H
#define MINUX_LIBC_SYSCALL_INTERNAL_H

/* 返回内核原始返回值（失败时为负错误码），不修改 errno。 */
long __syscall6(long nr, long a0, long a1, long a2, long a3, long a4, long a5);

static inline long __syscall0(long nr)
{
	return __syscall6(nr, 0, 0, 0, 0, 0, 0);
}

static inline long __syscall1(long nr, long a0)
{
	return __syscall6(nr, a0, 0, 0, 0, 0, 0);
}

static inline long __syscall2(long nr, long a0, long a1)
{
	return __syscall6(nr, a0, a1, 0, 0, 0, 0);
}

static inline long __syscall3(long nr, long a0, long a1, long a2)
{
	return __syscall6(nr, a0, a1, a2, 0, 0, 0);
}

static inline long __syscall4(long nr, long a0, long a1, long a2, long a3)
{
	return __syscall6(nr, a0, a1, a2, a3, 0, 0);
}

static inline long __syscall5(long nr, long a0, long a1, long a2, long a3, long a4)
{
	return __syscall6(nr, a0, a1, a2, a3, a4, 0);
}

/* 若 r 落在 [-4095, -1]，将 errno 设为 -r 并返回 -1；否则原样返回 r。 */
long __syscall_ret(long r);

#endif
