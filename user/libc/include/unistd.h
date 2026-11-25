#ifndef MINUX_USER_UNISTD_H
#define MINUX_USER_UNISTD_H

typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ssize_t;

#define STDOUT_FILENO 1

/* 第一步沿用系统调用返回值，失败时返回负错误码。 */
ssize_t write(int fd, const void *buf, size_t count);
_Noreturn void _exit(int status);

#endif
