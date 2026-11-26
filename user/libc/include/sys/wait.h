#ifndef MINUX_USER_SYS_WAIT_H
#define MINUX_USER_SYS_WAIT_H

#include <sys/types.h>

pid_t waitpid(pid_t pid, int *status, int options);

#endif
