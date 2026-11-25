#include <minux/errno.h>
#include <minux/io.h>
#include "mmio.h"

enum {
	MAGIC = 0x00, VERSION = 0x04, DEVICE_ID = 0x08,
	DEVICE_FEATURES = 0x10, DEVICE_FEATURES_SEL = 0x14,
	DRIVER_FEATURES = 0x20, DRIVER_FEATURES_SEL = 0x24,
	QUEUE_SEL = 0x30, QUEUE_NUM_MAX = 0x34, QUEUE_NUM = 0x38, QUEUE_READY = 0x44,
	QUEUE_NOTIFY = 0x50, INTERRUPT_STATUS = 0x60, INTERRUPT_ACK = 0x64, STATUS = 0x70,
	QUEUE_DESC = 0x80, QUEUE_AVAIL = 0x90, QUEUE_USED = 0xa0,
	CONFIG_GENERATION = 0xfc, CONFIG = 0x100,
};

enum { ACK = 1, DRIVER = 2, DRIVER_OK = 4, FEATURES_OK = 8, NEEDS_RESET = 64, FAILED = 128 };

//检查某段 MMIO 地址是不是指定类型、且版本受支持的 virtio 设备。它只做识别，还没有初始化设备。
int virtio_mmio_match(uintptr_t regs, size_t size, u32 device_id)
{
	if (size < CONFIG + 8 || (regs & 3))
		return -EINVAL;
	if (mmio_read32(regs + MAGIC) != 0x74726976 || mmio_read32(regs + DEVICE_ID) != device_id)
		return -ENODEV;
	return mmio_read32(regs + VERSION) == 2 ? 0 : -ENOTSUP;
}

int virtio_mmio_fail(struct virtio_mmio *v, int err)
{
	/* 未完成的 DMA 可能仍到达。停止复用队列并保留其静态缓冲区。 */
	v->failed = true;
	mmio_write32(v->regs + STATUS, mmio_read32(v->regs + STATUS) | FAILED);
	return err;
}

static bool set_queue_address(uintptr_t reg, const void *ptr)
{
	u64 address;

	if (!dma_address(ptr, &address))
		return false;
	mmio_write32(reg, (u32)address);
	mmio_write32(reg + 4, (u32)(address >> 32));
	return true;
}

int virtio_mmio_start(struct virtio_mmio *v, u32 features_low)
{
	uintptr_t r = v->regs;
	u64 start = io_clock_ticks();

	mmio_write32(r + STATUS, 0);
	while (mmio_read32(r + STATUS))
		if (io_clock_ticks() - start >= v->timeout_ticks)
			return virtio_mmio_fail(v, -ETIMEDOUT);
	mmio_write32(r + STATUS, ACK);
	mmio_write32(r + STATUS, ACK | DRIVER);
	mmio_write32(r + DEVICE_FEATURES_SEL, 1);
	if (!(mmio_read32(r + DEVICE_FEATURES) & 1)) /* VIRTIO_F_VERSION_1, bit 32 */
		return virtio_mmio_fail(v, -ENOTSUP);
	mmio_write32(r + DRIVER_FEATURES_SEL, 1);
	mmio_write32(r + DRIVER_FEATURES, 1);
	mmio_write32(r + DEVICE_FEATURES_SEL, 0);
	mmio_write32(r + DRIVER_FEATURES_SEL, 0);
	mmio_write32(r + DRIVER_FEATURES, mmio_read32(r + DEVICE_FEATURES) & features_low);
	mmio_write32(r + STATUS, ACK | DRIVER | FEATURES_OK);
	if (!(mmio_read32(r + STATUS) & FEATURES_OK))
		return virtio_mmio_fail(v, -ENOTSUP);

	mmio_write32(r + QUEUE_SEL, 0);
	if (mmio_read32(r + QUEUE_READY) || mmio_read32(r + QUEUE_NUM_MAX) < VQ_SIZE)
		return virtio_mmio_fail(v, -ENOTSUP);
	v->avail.flags = cpu_to_le16(1); /* NO_INTERRUPT，完成由 used.idx 轮询。 */
	mmio_write32(r + QUEUE_NUM, VQ_SIZE);
	if (!set_queue_address(r + QUEUE_DESC, v->desc) ||
	    !set_queue_address(r + QUEUE_AVAIL, &v->avail) ||
	    !set_queue_address(r + QUEUE_USED, &v->used))
		return virtio_mmio_fail(v, -EIO);
	mmio_write32(r + QUEUE_READY, 1);
	mmio_write32(r + STATUS, ACK | DRIVER | FEATURES_OK | DRIVER_OK);
	return 0;
}

int virtio_mmio_config64(struct virtio_mmio *v, unsigned int offset, u64 *result)
{
	uintptr_t r = v->regs;
	u64 start = io_clock_ticks();

	do {
		u32 gen = mmio_read32(r + CONFIG_GENERATION);
		u32 low = mmio_read32(r + CONFIG + offset);
		u32 high = mmio_read32(r + CONFIG + offset + 4);
		if (gen == mmio_read32(r + CONFIG_GENERATION)) {
			*result = (u64)high << 32 | low;
			return 0;
		}
	} while (io_clock_ticks() - start < v->timeout_ticks);
	return virtio_mmio_fail(v, -ETIMEDOUT);
}

int virtio_mmio_submit(struct virtio_mmio *v, u32 *written)
{
	u16 current = v->last_used, next = current + 1;
	unsigned int slot = current % VQ_SIZE;
	u64 start = io_clock_ticks();

	if (v->failed)
		return -EIO;
	/* 一个在途请求，描述符链总从 0 开始。先发布链，再发布索引和通知。 */
	v->avail.ring[slot] = cpu_to_le16(0);
	io_barrier();
	v->avail.idx = cpu_to_le16(next);
	mmio_write32(v->regs + QUEUE_NOTIFY, 0);
	u16 used;
	while ((used = le16_to_cpu(v->used.idx)) == current) {
		if (io_clock_ticks() - start >= v->timeout_ticks)
			return virtio_mmio_fail(v, -ETIMEDOUT);
	}
	io_barrier();
	if (used != next || le32_to_cpu(v->used.ring[slot].id) != 0 ||
	    (mmio_read32(v->regs + STATUS) & NEEDS_RESET))
		return virtio_mmio_fail(v, -EIO);
	*written = le32_to_cpu(v->used.ring[slot].len);
	v->last_used = next;
	u32 pending = mmio_read32(v->regs + INTERRUPT_STATUS);
	if (pending)
		mmio_write32(v->regs + INTERRUPT_ACK, pending);
	return 0;
}
