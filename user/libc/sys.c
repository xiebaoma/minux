#include <asm/unistd.h>
#include <fcntl.h>
#include <minux/signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "syscall_internal.h"

int open(const char *path, int flags, ...)
{
	unsigned int mode = 0;

	if (flags & O_CREAT) {
		__builtin_va_list ap;

		__builtin_va_start(ap, flags);
		mode = __builtin_va_arg(ap, unsigned int);
		__builtin_va_end(ap);
	}
	return __syscall_ret(__syscall4(__NR_openat, AT_FDCWD, (long)path, flags, mode));
}

ssize_t read(int fd, void *buf, size_t count)
{
	return __syscall_ret(__syscall3(__NR_read, fd, (long)buf, count));
}

ssize_t write(int fd, const void *buf, size_t count)
{
	return __syscall_ret(__syscall3(__NR_write, fd, (long)buf, count));
}

int close(int fd)
{
	return __syscall_ret(__syscall1(__NR_close, fd));
}

int dup3(int oldfd, int newfd, int flags)
{
	return __syscall_ret(__syscall3(__NR_dup3, oldfd, newfd, flags));
}

int pipe(int fds[2])
{
	return __syscall_ret(__syscall2(__NR_pipe2, (long)fds, 0));
}

pid_t fork(void)
{
	return __syscall_ret(__syscall5(__NR_clone, SIGCHLD, 0, 0, 0, 0));
}

pid_t getpid(void)
{
	return __syscall0(__NR_getpid);
}

int execve(const char *path, char *const argv[], char *const envp[])
{
	return __syscall_ret(__syscall3(__NR_execve, (long)path, (long)argv, (long)envp));
}

pid_t waitpid(pid_t pid, int *status, int options)
{
	return __syscall_ret(__syscall4(__NR_wait4, pid, (long)status, options, 0));
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
	return (void *)__syscall_ret(__syscall6(__NR_mmap, (long)addr, len, prot,
						flags, fd, offset));
}

int munmap(void *addr, size_t len)
{
	return __syscall_ret(__syscall2(__NR_munmap, (long)addr, len));
}
