#include <minux/errno.h>
#include <minux/fs.h>

int vfs_lookup(const struct inode *root, const char *path, const struct inode **out)
{
	const struct inode *node = root;
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
		while (*p && *p != '/') {
			if (++p - path >= VFS_PATH_MAX)
				return -ENAMETOOLONG;
		}
		len = p - name;
		if (!len || (len == 1 && *name == '.'))
			continue;
		if (len == 2 && name[0] == '.' && name[1] == '.') {
			if (node != root)
				node = node->parent;
			continue;
		}
		err = node->lookup(node, name, len, &node);
		if (err)
			return err;
	}
	*out = node;
	return 0;
}

int vfs_open(const struct inode *root, const char *path, struct file *out)
{
	const struct inode *node;
	int err = vfs_lookup(root, path, &node);

	if (err)
		return err;
	if (node->lookup)
		return -EISDIR;
	out->inode = node;
	return 0;
}

ssize_t vfs_read_at(const struct file *file, size_t offset, void *buf, size_t count)
{
	const struct inode *node = file->inode;

	if (!count || offset >= node->size)
		return 0;
	if (count > node->size - offset)
		count = node->size - offset;
	return node->read(node, offset, buf, count);
}
