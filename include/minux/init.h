#ifndef _MINUX_INIT_H
#define _MINUX_INIT_H

#include <minux/compiler.h>

struct block_device;

/* 分页后探测启动盘，未找到可用磁盘时返回 NULL。 */
struct block_device *platform_probe_boot_disk(void);
/* 准备设备和时钟中断源。调用者随后打开本地总中断。 */
void platform_init_irqs(void);

/* 平台完成启动映射、保留启动内存后，在内核虚拟地址空间进入。 */
void start_kernel(void) __noreturn;

#endif
