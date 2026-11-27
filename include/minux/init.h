#ifndef _MINUX_INIT_H
#define _MINUX_INIT_H

#include <minux/compiler.h>

/* 平台完成启动映射、保留启动内存后，在内核虚拟地址空间进入。 */
void start_kernel(void) __noreturn;

#endif
