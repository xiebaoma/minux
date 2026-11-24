#include <minux/container_of.h>
#include <minux/errno.h>
#include <minux/ramfs.h>
#include <minux/limits.h>
#include <minux/slab.h>
#include <minux/string.h>
#include "ramfs_internal.h"

struct ramfs_node {
	struct inode inode;
	struct ramfs_node *children, *next;
	size_t namelen;
	const u8 *archive_data;				//指向内嵌 cpio 归档中的原始文件内容。刚挂载时直接从这里读取，无须复制内容。
	u8 *storage;						//指向后来分配的可写内存。文件被写入后，读取会优先使用它；新创建的文件也用它保存内容。
	size_t capacity;					//storage 已分配的容量。它可能大于 inode.size（实际文件长度），留出的空间可供后续写入。
	char name[];						//柔性数组成员，把文件名直接放在 ramfs_node 结构体末尾，而不是另分配一个名字缓冲区。名字长度保存在 namelen 中。
};

static struct ramfs_node *find_child(const struct ramfs_node *dir, const char *name, size_t len)
{
	for (struct ramfs_node *n = dir->children; n; n = n->next)
		if (n->namelen == len && !memcmp(n->name, name, len))
			return n;
	return NULL;
}

static int ramfs_lookup(const struct inode *dir, const char *name, size_t len,
			struct inode **out)
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
	const u8 *data = n->storage ? n->storage : n->archive_data;

	memcpy(buf, data + offset, count);
	return count;
}

static void ramfs_truncate(struct inode *inode)
{
	struct ramfs_node *n = container_of(inode, struct ramfs_node, inode);

	kfree(n->storage);
	n->storage = NULL;
	n->archive_data = NULL;
	n->capacity = 0;
	inode->size = 0;
}

static ssize_t ramfs_write(struct inode *inode, size_t offset, const void *buf, size_t count)
{
	struct ramfs_node *n = container_of(inode, struct ramfs_node, inode);
	size_t end;

	if (offset > LONG_MAX || count > LONG_MAX - offset)
		return -EFBIG;
	end = offset + count;
	if (end > n->capacity) {
		size_t required = end > inode->size ? end : inode->size;
		size_t capacity = n->capacity * 2;
		u8 *data;

		/* 优先倍增，内存紧张时只申请本次所需的空间。 */
		if (capacity < required)
			capacity = required;
		data = kmalloc(capacity);
		if (!data && capacity > required) {
			capacity = required;
			data = kmalloc(capacity);
		}
		if (!data)
			return -ENOMEM;
		if (inode->size)
			ramfs_read(inode, 0, data, inode->size);
		kfree(n->storage);
		n->storage = data;
		n->capacity = capacity;
	}
	/* 另一个打开者可能在本句柄的当前位置之前截断过文件。 */
	if (offset > inode->size)
		memset(n->storage + inode->size, 0, offset - inode->size);
	memcpy(n->storage + offset, buf, count);
	if (end > inode->size)
		inode->size = end;
	return count;
}

static int ramfs_create(struct inode *parent, const char *name, size_t len, struct inode **out);

static struct ramfs_node *add_child(struct ramfs_node *dir, const char *name, size_t len,
				    bool directory)
{
	struct ramfs_node *node = kzalloc(sizeof(*node) + len);

	if (!node)
		return NULL;
	node->inode.parent = &dir->inode;
	if (directory) {
		node->inode.lookup = ramfs_lookup;
		node->inode.create = ramfs_create;
	} else {
		node->inode.read = ramfs_read;
		node->inode.write = ramfs_write;
		node->inode.truncate = ramfs_truncate;
	}
	memcpy(node->name, name, len);
	node->namelen = len;
	node->next = dir->children;
	dir->children = node;
	return node;
}

static int ramfs_create(struct inode *parent, const char *name, size_t len, struct inode **out)
{
	struct ramfs_node *dir = container_of(parent, struct ramfs_node, inode);
	struct ramfs_node *node = add_child(dir, name, len, false);

	if (!node)
		return -ENOMEM;
	*out = &node->inode;
	return 0;
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
	node = add_child(dir, name, len, directory);
	if (!node)
		return -ENOMEM;
	node->archive_data = data;
	node->inode.size = size;
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
			kfree(node->storage);
			kfree(node);
			if (!parent)
				return;
			node = container_of(parent, struct ramfs_node, inode);
		}
	}
}

int ramfs_mount(const void *archive, size_t size, struct inode **out)
{
	struct ramfs_node *root = kzalloc(sizeof(*root));
	int err;

	if (!root)
		return -ENOMEM;
	root->inode.lookup = ramfs_lookup;
	root->inode.create = ramfs_create;
	err = initramfs_unpack(root, archive, size);
	if (err) {
		ramfs_destroy(&root->inode);
		return err;
	}
	*out = &root->inode;
	return 0;
}
