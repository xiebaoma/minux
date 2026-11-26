#ifndef MINUX_TEST_USER_SYSCALL_H
#define MINUX_TEST_USER_SYSCALL_H

#include <asm/unistd.h>

static long call(long nr, long x, long y, long z, long w)
{
	register long a0 asm("a0") = x;
	register long a1 asm("a1") = y;
	register long a2 asm("a2") = z;
	register long a3 asm("a3") = w;
	register long a7 asm("a7") = nr;
	asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a3), "r"(a7) : "memory");
	return a0;
}

#endif
