#ifndef _MINUX_BLOCK_H
#define _MINUX_BLOCK_H

#include <minux/spinlock.h>

#define BLK_SECTOR_SIZE 512U
#define BLK_CACHE_SLOTS 8U

//缓存的是扇区 512 字节
struct blk_cache_entry {
	bool valid;
	u64 sector;
	u8 data[BLK_SECTOR_SIZE];
};

/* 发布前清零，再设置容量与回调。后端同步读取一个扇区，返回 0 或负 errno。 */
struct block_device {
	u64 sectors;
	int (*read_sector)(struct block_device *dev, u64 sector, void *dst);
	struct spinlock lock;
	struct blk_cache_entry cache[BLK_CACHE_SLOTS];
};

extern struct block_device *boot_disk;

/* 整段越界时不发 I/O。后端在中途报错时，前面的扇区可能已复制。 */
int blk_read(struct block_device *dev, u64 sector, size_t count, void *dst);
int blk_read_cached(struct block_device *dev, u64 sector, size_t count, void *dst);

#endif
