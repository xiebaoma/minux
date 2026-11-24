#include <minux/ramfs.h>
#include <minux/console.h>

extern const u8 initramfs_start[], initramfs_end[];
struct inode *root_inode;

int rootfs_init(void)
{
	struct inode *root, *dev;
	size_t size = (uintptr_t)initramfs_end - (uintptr_t)initramfs_start;
	int err = ramfs_mount(initramfs_start, size, &root);

	if (err)
		return err;
	err = vfs_lookup(root, "/dev", &dev);
	if (!err)
		err = ramfs_add_device(dev, "console", &console_operations);
	if (err)
		ramfs_destroy(root);
	else
		root_inode = root;
	return err;
}
