#ifndef MINUX_USER_SYS_MMAN_H
#define MINUX_USER_SYS_MMAN_H

#include <minux/mman.h>
#include <stddef.h>
#include <sys/types.h>

#define MAP_FAILED ((void *)-1)

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t len);

#endif
