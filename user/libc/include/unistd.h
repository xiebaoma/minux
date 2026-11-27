#ifndef MINUX_USER_UNISTD_H
#define MINUX_USER_UNISTD_H

#include <stddef.h>
#include <sys/types.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* 失败时返回 -1 并设置 errno；成功时不修改 errno。 */
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
int dup3(int oldfd, int newfd, int flags);
int pipe(int fds[2]);
pid_t fork(void);
pid_t getpid(void);
int execve(const char *path, char *const argv[], char *const envp[]);
_Noreturn void _exit(int status);

#endif
