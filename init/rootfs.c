#include <minux/ramfs.h>

extern const u8 initramfs_start[], initramfs_end[];
struct inode *root_inode;

int rootfs_init(void)
{
	return ramfs_mount(initramfs_start, (uintptr_t)initramfs_end - (uintptr_t)initramfs_start,
			   &root_inode);
}
