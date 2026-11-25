#include <minux/block.h>
#include <minux/container_of.h>
#include <minux/errno.h>
#include <minux/limits.h>
#include <minux/string.h>
#include "test.h"

#define FIXTURE_SECTORS 32

static struct memory_disk {
	struct block_device dev;
	unsigned int reads;
	bool fail;
	u8 data[FIXTURE_SECTORS][BLK_SECTOR_SIZE];
} memory;

static int memory_read(struct block_device *dev, u64 sector, void *dst)
{
	struct memory_disk *disk = container_of(dev, struct memory_disk, dev);
	disk->reads++;
	if (disk->fail || sector >= FIXTURE_SECTORS) {
		memset(dst, 0xee, BLK_SECTOR_SIZE);
		return -EIO;
	}
	memcpy(dst, disk->data[sector], BLK_SECTOR_SIZE);
	return 0;
}

static u8 expected_byte(u64 sector, unsigned int offset)
{
	if (offset < 8)
		return sector >> (8 * offset);
	return sector * 37 + offset * 7 + offset / 256;
}

static bool matches(const u8 *data, u64 sector, size_t count)
{
	for (size_t i = 0; i < count; i++)
		for (unsigned int j = 0; j < BLK_SECTOR_SIZE; j++)
			if (data[i * BLK_SECTOR_SIZE + j] != expected_byte(sector + i, j))
				return false;
	return true;
}

static void check_device(struct block_device *dev, u64 expected_capacity)
{
	u8 bytes[3 * BLK_SECTOR_SIZE + 2];
	u8 *buf = bytes + 1; /* 任意内核缓冲区，包括非对齐地址。 */

	check(dev && dev->sectors == expected_capacity, "discover the full fixture capacity");
	if (!dev || dev->sectors != expected_capacity)
		return;
	u64 base = dev->sectors - FIXTURE_SECTORS; /* ext2 测试盘尾部保留原扇区样本。 */
	memset(bytes, 0xa5, sizeof(bytes));
	check(!blk_read(dev, base, 1, buf) && matches(buf, 0, 1), "raw read of first sector");
	check(!blk_read(dev, base + 4, 3, buf) && matches(buf, 4, 3), "three consecutive raw sectors");
	check(!blk_read_cached(dev, base + 29, 3, buf) && matches(buf, 29, 3), "cached read ending at capacity");
	check(!blk_read_cached(dev, base + 30, 2, buf) && matches(buf, 30, 2), "overlapping cached range");
	check(bytes[0] == 0xa5 && bytes[sizeof(bytes) - 1] == 0xa5, "reads respect caller buffer bounds");

	bool all_match = true;
	for (size_t i = 0; i < FIXTURE_SECTORS; i++) {
		u64 sector = (i * 13) % FIXTURE_SECTORS;
		if (blk_read(dev, base + sector, 1, buf) || !matches(buf, sector, 1))
			all_match = false;
	}
	check(all_match, "non-sequential reads across many queue ring wraps");

	memset(bytes, 0x5a, sizeof(bytes));
	check(blk_read(dev, dev->sectors, 1, buf) == -EINVAL &&
	      blk_read_cached(dev, dev->sectors - 1, 2, buf) == -EINVAL &&
	      blk_read(dev, ULLONG_MAX, 2, buf) == -EINVAL &&
	      blk_read_cached(dev, 1, SIZE_MAX, buf) == -EINVAL &&
	      buf[0] == 0x5a && buf[BLK_SECTOR_SIZE - 1] == 0x5a,
	      "reject out-of-range reads before copying any sector");
	check(!blk_read(dev, dev->sectors, 0, NULL) && blk_read(dev, base, 1, NULL) == -EINVAL,
	      "allow empty read at end and reject missing output buffer");
}

void test_memory_block(void)
{
	u8 buf[BLK_SECTOR_SIZE];
	unsigned int reads;

	memory.dev.read_sector = memory_read;
	memory.dev.sectors = FIXTURE_SECTORS;
	for (size_t sector = 0; sector < FIXTURE_SECTORS; sector++)
		for (size_t j = 0; j < BLK_SECTOR_SIZE; j++)
			memory.data[sector][j] = expected_byte(sector, j);
	check_device(&memory.dev, FIXTURE_SECTORS);

	reads = memory.reads;
	check(!blk_read_cached(&memory.dev, 0, 1, buf) &&
	      !blk_read_cached(&memory.dev, 0, 1, buf) && memory.reads == reads + 1,
	      "cache hit avoids a backend request");
	check(!blk_read_cached(&memory.dev, BLK_CACHE_SLOTS, 1, buf) &&
	      matches(buf, BLK_CACHE_SLOTS, 1) && !blk_read_cached(&memory.dev, 0, 1, buf) &&
	      matches(buf, 0, 1) && memory.reads == reads + 3,
	      "fixed slot evicts and reloads the original sector");
	memory.fail = true;
	memset(buf, 0x5a, sizeof(buf));
	check(blk_read_cached(&memory.dev, BLK_CACHE_SLOTS, 1, buf) == -EIO && buf[0] == 0x5a,
	      "failed cache fill does not publish partial backend data");
	memory.fail = false;
	check(!blk_read_cached(&memory.dev, 0, 1, buf) && matches(buf, 0, 1) &&
	      !blk_read_cached(&memory.dev, BLK_CACHE_SLOTS, 1, buf) && matches(buf, BLK_CACHE_SLOTS, 1),
	      "retry after error reloads both conflicting sectors");
	reads = memory.reads;
	check(blk_read(&memory.dev, 31, 2, buf) == -EINVAL && memory.reads == reads,
	      "range validation precedes backend I/O");
}

void test_virtio_block(void)
{
	check_device(boot_disk, (2 * 1024 * 1024) / BLK_SECTOR_SIZE + FIXTURE_SECTORS);
}
