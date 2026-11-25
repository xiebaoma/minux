#include <minux/block.h>
#include <minux/buddy.h>
#include <minux/errno.h>
#include <minux/ext2.h>
#include <minux/fcntl.h>
#include <minux/fs.h>
#include <minux/string.h>
#include "test.h"

static const char pattern[] = "/disk/share/probe/pattern";

static bool check_pattern(struct file *f, size_t pos, size_t count, size_t expected)
{
	u8 buf[1100];

	memset(buf, 0xac, sizeof(buf));
	if (vfs_read_at(f, pos, buf, count) != (ssize_t)expected)
		return false;
	for (size_t i = 0; i < expected; i++)
		if (buf[i] != (u8)((pos + i) * 17 + (pos + i) / 1024))
			return false;
	return expected == sizeof(buf) || buf[expected] == 0xac;
}

/* 在真实磁盘的只读视图上注入故障。每次重新挂载使用独立的扇区缓存。 */
enum fault { BAD_MAGIC, UNKNOWN_FEATURE, BAD_DIRENT };
static struct block_device overlay;
static enum fault fault;

static const u8 root_dot_entry[] = { 2, 0, 0, 0, 12, 0, 1, 2, '.' };

static int faulty_read(struct block_device *dev, u64 sector, void *dst)
{
	u8 *p = dst;
	(void)dev;
	int err = blk_read(boot_disk, sector, 1, p);
	if (err)
		return err;
	if (sector == 2 && fault == BAD_MAGIC)
		p[56] = 0;
	if (sector == 2 && fault == UNKNOWN_FEATURE)
		p[99] |= 0x80;
	/* 根目录首项是 inode 2 的 "."，使它的 rec_len 无法前进。 */
	if (fault == BAD_DIRENT && !memcmp(p, root_dot_entry, sizeof(root_dot_entry)))
		p[4] = 0;
	return 0;
}

static void test_bad_metadata(void)
{
	for (fault = BAD_MAGIC; fault <= BAD_DIRENT; fault++) {
		struct inode *root = NULL, *bin;
		u64 before = buddy_free_pages();
		memset(&overlay, 0, sizeof(overlay));
		overlay.read_sector = faulty_read;
		overlay.sectors = boot_disk->sectors;
		int err = ext2_mount(&overlay, &root);
		if (fault == BAD_DIRENT)
			check(!err && vfs_lookup(root, "/bin", &bin) == -EIO,
			      "ext2: reject zero-length directory entry");
		else
			check(err == (fault == BAD_MAGIC ? -EINVAL : -ENOTSUP) && !root,
			      "ext2: reject incompatible superblock before publishing root");
		if (root)
			ext2_destroy(root);
		check(buddy_free_pages() == before, "ext2: reclaim private mount and cached inodes");
	}
}

void test_ext2(void)
{
	struct inode *node;
	struct file f;
	int err = vfs_open(root_inode, pattern, &f);

	check(!err && f.inode->size == 17 * 1024 + 37, "ext2: lookup nested regular file");
	if (err)
		return;
	check(check_pattern(&f, 1001, 1073, 1073), "ext2: unaligned read across three direct blocks");
	check(check_pattern(&f, 12 * 1024 - 19, 1059, 1059), "ext2: direct to single-indirect transition");
	check(check_pattern(&f, f.inode->size - 47, 100, 47) &&
	      check_pattern(&f, f.inode->size, 8, 0), "ext2: short tail read and EOF");
	check(vfs_write_at(&f, 0, "x", 1) == -EBADF &&
	      vfs_open_flags(root_inode, pattern, O_WRONLY | O_TRUNC, &f) == -EROFS &&
	      vfs_open_flags(root_inode, "/disk/bin/new", O_CREAT | O_WRONLY, &f) == -EROFS,
	      "ext2: reject writes, truncation and creation");
	check(!vfs_lookup(root_inode, "/disk/share/probe/../../..", &node) && node == root_inode &&
	      vfs_open(root_inode, "/disk/bin", &f) == -EISDIR &&
	      vfs_open(root_inode, "/disk/bin/child/sub", &f) == -ENOTDIR,
	      "ext2: directory semantics and .. across the mount");
	check(vfs_open(root_inode, "/bin/child", &f) == -ENOENT &&
	      !vfs_open(root_inode, "/disk/bin/child", &f), "ext2: executable exists only on disk");
	test_bad_metadata();
}
