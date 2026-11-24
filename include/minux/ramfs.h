#ifndef _MINUX_RAMFS_H
#define _MINUX_RAMFS_H

#include <minux/fs.h>

/* 挂载成功才发布根节点。归档字节在整个挂载期间必须有效且不可变。 */
int ramfs_mount(const void *archive, size_t size, struct inode **out);
/* 在尚未发布的 ramfs 目录中注册设备节点。 */
int ramfs_add_device(struct inode *dir, const char *name, const struct stream_operations *ops);
/* 调用者须先结束对这棵树的所有访问。 */
void ramfs_destroy(const struct inode *root);

#endif
