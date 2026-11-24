#include <minux/container_of.h>
#include <minux/errno.h>
#include <minux/ramfs.h>
#include <minux/slab.h>
#include <minux/string.h>
#include "ramfs_internal.h"

struct ramfs_node {
	struct inode inode;
	struct ramfs_node *children, *next;
	const char *name;
	size_t namelen;
	const u8 *data;
};

static struct ramfs_node *find_child(const struct ramfs_node *dir, const char *name, size_t len)
{
	for (struct ramfs_node *n = dir->children; n; n = n->next)
		if (n->namelen == len && !memcmp(n->name, name, len))
			return n;
	return NULL;
}

static int ramfs_lookup(const struct inode *dir, const char *name, size_t len,
			const struct inode **out)
{
	const struct ramfs_node *parent = container_of(dir, struct ramfs_node, inode);
	struct ramfs_node *child = find_child(parent, name, len);

	if (!child)
		return -ENOENT;
	*out = &child->inode;
	return 0;
}

static ssize_t ramfs_read(const struct inode *inode, size_t offset, void *buf, size_t count)
{
	const struct ramfs_node *n = container_of(inode, struct ramfs_node, inode);

	memcpy(buf, n->data + offset, count);
	return count;
}

int ramfs_add(struct ramfs_node *root, const char *path, bool directory,
	      const u8 *data, size_t size)
{
	struct ramfs_node *dir = root, *node;
	const char *name;
	size_t len;

	/* cpio 常用 ./ 前缀。只接收相对成员名，目录须出现在子项之前。 */
	while (path[0] == '.' && path[1] == '/')
		path += 2;
	if (directory && !strcmp(path, "."))
		return 0;
	for (;;) {
		name = path;
		while (*path && *path != '/')
			path++;
		len = path - name;
		if (!len || (len == 1 && *name == '.') ||
		    (len == 2 && !memcmp(name, "..", 2)))
			return -EINVAL;
		node = find_child(dir, name, len);
		if (!*path)
			break;
		if (!node)
			return -ENOENT;
		if (!node->inode.lookup)
			return -ENOTDIR;
		dir = node;
		path++;
	}
	if (node)
		return -EEXIST;
	node = kzalloc(sizeof(*node));
	if (!node)
		return -ENOMEM;
	node->inode.parent = &dir->inode;
	node->inode.size = size;
	if (directory)
		node->inode.lookup = ramfs_lookup;
	else
		node->inode.read = ramfs_read;
	node->name = name;
	node->namelen = len;
	node->data = data;
	node->next = dir->children;
	dir->children = node;
	return 0;
}

void ramfs_destroy(const struct inode *root)
{
	struct ramfs_node *node = container_of(root, struct ramfs_node, inode);

	/* 逐层摘下子节点，回到父节点时继续释放下一个兄弟。 */
	for (;;) {
		struct ramfs_node *child = node->children;
		const struct inode *parent = node->inode.parent;

		if (child) {
			node->children = child->next;
			node = child;
		} else {
			kfree(node);
			if (!parent)
				return;
			node = container_of(parent, struct ramfs_node, inode);
		}
	}
}

int ramfs_mount(const void *archive, size_t size, const struct inode **out)
{
	struct ramfs_node *root = kzalloc(sizeof(*root));
	int err;

	if (!root)
		return -ENOMEM;
	root->inode.lookup = ramfs_lookup;
	err = initramfs_unpack(root, archive, size);
	if (err) {
		ramfs_destroy(&root->inode);
		return err;
	}
	*out = &root->inode;
	return 0;
}
