#ifndef MINUX_USER_UNISTD_H
#define MINUX_USER_UNISTD_H

#include <stddef.h>

typedef __PTRDIFF_TYPE__ ssize_t;

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* 沿用系统调用返回值，失败时返回负错误码。 */
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
_Noreturn void _exit(int status);

#endif
