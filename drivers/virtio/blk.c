#include <minux/block.h>
#include <minux/container_of.h>
#include <minux/errno.h>
#include <minux/io.h>
#include <minux/string.h>
#include <minux/virtio_blk.h>
#include "mmio.h"

struct virtio_blk {
	struct block_device dev;
	struct virtio_mmio transport;
	struct {
		u32 type, reserved;
		u64 sector;
	} header;
	u8 data[BLK_SECTOR_SIZE];
	u8 status;
};

/* 启动盘和 DMA 区在内核数据段内，生命周期覆盖整个设备运行期。 */
static struct virtio_blk disk;
_Static_assert(sizeof(disk.header) == 16, "virtio-blk request header layout");

static int read_sector(struct block_device *dev, u64 sector, void *dst)
{
	struct virtio_blk *d = container_of(dev, struct virtio_blk, dev);
	u32 written;
	int err;

	if (d->transport.failed)
		return -EIO;
	d->header.sector = cpu_to_le64(sector);
	d->status = 0xff;
	err = virtio_mmio_submit(&d->transport, &written);
	if (err)
		return err;
	if (written > sizeof(d->data) + 1 || !written)
		return virtio_mmio_fail(&d->transport, -EIO);
	if (d->status != 0)
		return -EIO;
	if (written != sizeof(d->data) + 1)
		return virtio_mmio_fail(&d->transport, -EIO);
	memcpy(dst, d->data, sizeof(d->data));
	return 0;
}

static bool set_desc(unsigned int index, const void *buf, u32 size, u16 flags)
{
	u64 address;
	if (!dma_address(buf, &address))
		return false;
	disk.transport.desc[index] = (struct virtq_desc){
		.address = cpu_to_le64(address), .length = cpu_to_le32(size),
		.flags = cpu_to_le16(flags), .next = cpu_to_le16(index + 1),
	};
	return true;
}

int virtio_blk_probe(uintptr_t regs, size_t size, u32 clock_hz, struct block_device **out)
{
	int err = virtio_mmio_match(regs, size, 2);
	if (err)
		return err;
	if (disk.transport.regs) {
		/* 一个队列只初始化一次，失败的 DMA 区也不能再复用。 */
		if (disk.transport.regs != regs || !disk.dev.read_sector || disk.transport.failed)
			return -EIO;
		*out = &disk.dev;
		return 0;
	}
	disk.transport.regs = regs;
	disk.transport.timeout_ticks = 2ULL * clock_hz;
	/* type=VIRTIO_BLK_T_IN (0)。设备只能写数据区和最后的状态字节。 */
	if (!set_desc(0, &disk.header, sizeof(disk.header), VQ_NEXT) ||
	    !set_desc(1, disk.data, sizeof(disk.data), VQ_NEXT | VQ_WRITE) ||
	    !set_desc(2, &disk.status, 1, VQ_WRITE))
		return -EIO;
	err = virtio_mmio_start(&disk.transport, 1U << 5); /* VIRTIO_BLK_F_RO */
	if (err)
		return err;
	err = virtio_mmio_config64(&disk.transport, 0, &disk.dev.sectors);
	if (err)
		return err;
	disk.dev.read_sector = read_sector;
	*out = &disk.dev;
	return 0;
}
