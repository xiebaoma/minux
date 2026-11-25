#include <minux/block.h>
#include <minux/errno.h>
#include <minux/limits.h>
#include <minux/string.h>

static int read_cached_sector(struct block_device *dev, u64 sector, void *dst)
{
	size_t slot = sector % BLK_CACHE_SLOTS;
	struct blk_cache_entry *entry = &dev->cache[slot];

	if (!entry->valid || entry->sector != sector) {
		entry->valid = false;
		int err = dev->read_sector(dev, sector, entry->data);
		if (err)
			return err;
		entry->sector = sector;
		entry->valid = true;
	}
	memcpy(dst, entry->data, BLK_SECTOR_SIZE);
	return 0;
}

static int read_sectors(struct block_device *dev, u64 sector, size_t count, void *dst,
			bool cached)
{
	u8 *buf = dst;

	if (!dev || !dev->read_sector || (!dst && count) ||
	    count > SIZE_MAX / BLK_SECTOR_SIZE || sector > dev->sectors ||
	    count > dev->sectors - sector)
		return -EINVAL;

	for (size_t i = 0; i < count; i++, buf += BLK_SECTOR_SIZE) {
		unsigned long flags = spin_lock_irqsave(&dev->lock);
		int err = cached ? read_cached_sector(dev, sector + i, buf) :
				   dev->read_sector(dev, sector + i, buf);
		spin_unlock_irqrestore(&dev->lock, flags);
		if (err)
			return err;
	}
	return 0;
}

int blk_read(struct block_device *dev, u64 sector, size_t count, void *dst)
{
	return read_sectors(dev, sector, count, dst, false);
}

int blk_read_cached(struct block_device *dev, u64 sector, size_t count, void *dst)
{
	return read_sectors(dev, sector, count, dst, true);
}
