#ifndef _MINUX_FS_H
#define _MINUX_FS_H

#include <minux/types.h>
#include <minux/spinlock.h>

/* 包括字符串结尾；与 execve 复制用户路径时使用同一个上限。 */
#define VFS_PATH_MAX 128

struct file;
/* 流设备和管道直接与用户缓冲区传输，不使用普通文件的位置锁。 */
struct stream_operations {
	ssize_t (*read)(struct file *file, uintptr_t buf, size_t count);
	ssize_t (*write)(struct file *file, uintptr_t buf, size_t count);
	void (*release)(struct file *file);
};

struct inode {
	const struct stream_operations *stream;
	struct inode *parent;
	size_t size;
	/* 目录提供 lookup，普通文件提供 read。节点在挂载期间保持有效。 */
	int (*lookup)(const struct inode *dir, const char *name, size_t len,
		      struct inode **out);
	int (*create)(struct inode *dir, const char *name, size_t len, struct inode **out);
	void (*truncate)(struct inode *node);
	ssize_t (*write)(struct inode *node, size_t offset, const void *buf, size_t count);
	ssize_t (*read)(const struct inode *node, size_t offset, void *buf, size_t count);
};

/* inode 由挂载持有；fd 表持有打开文件的引用，fork 后共享 pos。 */
struct file {
	const struct stream_operations *stream;
	void *private_data;
	struct inode *inode;
	size_t pos;
	unsigned int refs;
	int access;
	struct spinlock pos_lock;
};

extern struct inode *root_inode;
struct block_device;
int rootfs_init(struct block_device *disk);
int vfs_lookup(struct inode *root, const char *path, struct inode **out);
int vfs_open(struct inode *root, const char *path, struct file *out);
int vfs_open_flags(struct inode *root, const char *path, int flags, struct file *out);
ssize_t vfs_write_at(struct file *file, size_t offset, const void *buf, size_t count);
ssize_t vfs_read_at(const struct file *file, size_t offset, void *buf, size_t count);

#endif
