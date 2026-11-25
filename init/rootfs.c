#include <minux/ramfs.h>
#include <minux/block.h>
#include <minux/ext2.h>
#include <minux/console.h>

extern const u8 initramfs_start[], initramfs_end[];
struct inode *root_inode;

static int mount_disk(struct inode *root)
{
	struct inode *dir, *disk;
	int err = vfs_lookup(root, "/disk", &dir);
	if (err)
		return err;
	err = ext2_mount(boot_disk, &disk);
	if (err)
		return err;
	err = ramfs_attach(dir, disk, ext2_destroy);
	if (err)
		ext2_destroy(disk);
	return err;
}

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
	if (!err && boot_disk)
		err = mount_disk(root);
	if (err)
		ramfs_destroy(root);
	else
		root_inode = root;
	return err;
}
