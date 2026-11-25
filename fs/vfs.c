#include <minux/errno.h>
#include <minux/fcntl.h>
#include <minux/fs.h>

/* 串行化路径查找、inode 缓存更新和 ramfs 文件修改。磁盘读取当前采用轮询。 */
static struct spinlock fs_lock = SPINLOCK_INIT;

static int walk(struct inode *root, const char *path, int flags, struct inode **out)
{
	struct inode *node = root;
	const char *p = path;

	if (!*p)
		return -ENOENT;
	if (*p != '/')
		return -EINVAL;
	for (size_t i = 0; path[i]; i++)
		if (i == VFS_PATH_MAX - 1)
			return -ENAMETOOLONG;
	while (*p) {
		const char *name;
		size_t len;
		int err;

		if (!node->lookup)
			return -ENOTDIR;
		while (*p == '/')
			p++;
		name = p;
		while (*p && *p != '/')
			p++;
		len = p - name;
		if (!len || (len == 1 && *name == '.'))
			continue;
		if (len == 2 && name[0] == '.' && name[1] == '.') {
			if (node != root)
				node = node->parent;
			continue;
		}
		struct inode *dir = node;
		err = dir->lookup(dir, name, len, &node);
		if (err == -ENOENT && !*p && (flags & O_CREAT)) {
			if (!dir->create)
				return -EROFS;
			return dir->create(dir, name, len, out);
		}
		if (err)
			return err;
	}
	if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
		return -EEXIST;
	*out = node;
	return 0;
}

int vfs_lookup(struct inode *root, const char *path, struct inode **out)
{
	unsigned long irq = spin_lock_irqsave(&fs_lock);
	int err = walk(root, path, 0, out);

	spin_unlock_irqrestore(&fs_lock, irq);
	return err;
}

int vfs_open_flags(struct inode *root, const char *path, int flags, struct file *out)
{
	struct inode *node;
	int access = flags & O_ACCMODE;
	unsigned long irq;
	int err;

	if ((flags & ~(O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC)) || access == O_ACCMODE ||
	    ((flags & O_TRUNC) && access == O_RDONLY))
		return -EINVAL;
	irq = spin_lock_irqsave(&fs_lock);
	err = walk(root, path, flags, &node);
	if (err)
		goto out;
	if (node->lookup) {
		err = -EISDIR;
		goto out;
	}
	if (node->stream) {
		if ((access != O_WRONLY && !node->stream->read) ||
		    (access != O_RDONLY && !node->stream->write)) {
			err = -EACCES;
			goto out;
		}
	} else if ((access != O_RDONLY && !node->write) || ((flags & O_TRUNC) && !node->truncate)) {
		err = -EROFS;
		goto out;
	}
	if ((flags & O_TRUNC) && node->truncate)
		node->truncate(node);
	*out = (struct file){ .inode = node, .stream = node->stream, .access = access, .refs = 1 };
out:
	spin_unlock_irqrestore(&fs_lock, irq);
	return err;
}

int vfs_open(struct inode *root, const char *path, struct file *out)
{
	return vfs_open_flags(root, path, O_RDONLY, out);
}

ssize_t vfs_read_at(const struct file *file, size_t offset, void *buf, size_t count)
{
	const struct inode *node = file->inode;
	unsigned long irq = spin_lock_irqsave(&fs_lock);
	ssize_t n = 0;

	if (file->access == O_WRONLY)
		n = -EBADF;
	else if (file->stream)
		n = -ESPIPE;
	else if (count && offset < node->size) {
		if (count > node->size - offset)
			count = node->size - offset;
		n = node->read(node, offset, buf, count);
	}
	spin_unlock_irqrestore(&fs_lock, irq);
	return n;
}

ssize_t vfs_write_at(struct file *file, size_t offset, const void *buf, size_t count)
{
	struct inode *node = file->inode;
	unsigned long irq = spin_lock_irqsave(&fs_lock);
	ssize_t n = 0;

	if (file->access == O_RDONLY)
		n = -EBADF;
	else if (file->stream)
		n = -ESPIPE;
	else if (count)
		n = node->write(node, offset, buf, count);
	spin_unlock_irqrestore(&fs_lock, irq);
	return n;
}
