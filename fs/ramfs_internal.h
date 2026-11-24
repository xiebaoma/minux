#ifndef _RAMFS_INTERNAL_H
#define _RAMFS_INTERNAL_H

#include <minux/types.h>

struct ramfs_node;
int initramfs_unpack(struct ramfs_node *root, const u8 *archive, size_t size);
int ramfs_add(struct ramfs_node *root, const char *path, bool directory,
	      const u8 *data, size_t size);

#endif
