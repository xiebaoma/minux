#ifndef _VIRTIO_MMIO_H
#define _VIRTIO_MMIO_H

#include <minux/types.h>

#define VQ_SIZE 4
#define VQ_NEXT 1
#define VQ_WRITE 2

struct virtq_desc {
    u64 address;     // 缓冲区的物理地址，供设备 DMA 访问
    u32 length;      // 缓冲区长度，单位是字节
    u16 flags;       // 使用方式
    u16 next;        // 下一个描述符在数组中的编号
};

_Static_assert(sizeof(struct virtq_desc) == 16, "split virtqueue descriptor layout");

struct virtio_mmio {
	uintptr_t regs;
	u64 timeout_ticks;
	bool failed;
	u16 last_used;
	struct virtq_desc desc[VQ_SIZE] __attribute__((aligned(16)));
	struct {
		u16 flags, idx, ring[VQ_SIZE], used_event;
	} avail;
	struct {
		u16 flags;
		volatile u16 idx;
		struct { u32 id, len; } ring[VQ_SIZE];
		u16 avail_event;
	} used;
};

int virtio_mmio_match(uintptr_t regs, size_t size, u32 device_id);
int virtio_mmio_start(struct virtio_mmio *v, u32 features_low);
int virtio_mmio_config64(struct virtio_mmio *v, unsigned int offset, u64 *result);
int virtio_mmio_submit(struct virtio_mmio *v, u32 *written);
int virtio_mmio_fail(struct virtio_mmio *v, int err);

#endif
