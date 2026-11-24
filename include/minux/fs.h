#ifndef _MINUX_FS_H
#define _MINUX_FS_H

#include <minux/types.h>

/* 包括字符串结尾；与 execve 复制用户路径时使用同一个上限。 */
#define VFS_PATH_MAX 128

struct inode {
	const struct inode *parent;
	size_t size;
	/* 目录提供 lookup，普通文件提供 read。节点在挂载期间保持有效。 */
	int (*lookup)(const struct inode *dir, const char *name, size_t len,
		      const struct inode **out);
	ssize_t (*read)(const struct inode *node, size_t offset, void *buf, size_t count);
};

/* 只读挂载上的借用句柄，放在调用者栈上，按显式偏移读取。 */
struct file {
	const struct inode *inode;
};

extern const struct inode *root_inode;
int rootfs_init(void);
int vfs_lookup(const struct inode *root, const char *path, const struct inode **out);
int vfs_open(const struct inode *root, const char *path, struct file *out);
ssize_t vfs_read_at(const struct file *file, size_t offset, void *buf, size_t count);

#endif
