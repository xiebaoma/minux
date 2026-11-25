#ifndef _MINUX_EXT2_H
#define _MINUX_EXT2_H

struct block_device;
struct inode;

int ext2_mount(struct block_device *dev, struct inode **out);
void ext2_destroy(struct inode *root);

#endif
