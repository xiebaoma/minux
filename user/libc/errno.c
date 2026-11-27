#include <errno.h>
#include "syscall_internal.h"

int errno;

long __syscall_ret(long r)
{
	if ((unsigned long)r >= (unsigned long)-4095UL) {
		errno = (int)-r;
		return -1;
	}
	return r;
}
