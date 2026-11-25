#ifndef _MINUX_VIRTIO_BLK_H
#define _MINUX_VIRTIO_BLK_H

#include <minux/types.h>

struct block_device;
/* 启动时选第一块磁盘；空槽或其他设备返回 -ENODEV。 */
int virtio_blk_probe(uintptr_t regs, size_t size, u32 clock_hz, struct block_device **out);

#endif
