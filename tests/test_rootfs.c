#include <minux/buddy.h>
#include <minux/errno.h>
#include <minux/limits.h>
#include <minux/ramfs.h>
#include <minux/sprintf.h>
#include <minux/string.h>
#include "test.h"

void test_rootfs(void)
{
	const struct inode *node;
	struct file file;
	char path[VFS_PATH_MAX + 1], buf[8];

	check(!vfs_lookup(root_inode, "/../../", &node) && node == root_inode,
	      "vfs: .. at root stays at root");
	check(vfs_open(root_inode, "/bin", &file) == -EISDIR &&
	      vfs_open(root_inode, "/bin/child/", &file) == -ENOTDIR &&
	      vfs_open(root_inode, "/init/../bin", &file) == -ENOTDIR,
	      "vfs: require directories for intermediate components and trailing slash");
	check(vfs_open(root_inode, "/bin/chil", &file) == -ENOENT &&
	      vfs_open(root_inode, "", &file) == -ENOENT &&
	      vfs_open(root_inode, "init", &file) == -EINVAL,
	      "vfs: lookup uses whole components and absolute paths");
	memset(path, '/', sizeof(path) - 1);
	path[sizeof(path) - 1] = 0;
	check(vfs_lookup(root_inode, path, &node) == -ENAMETOOLONG, "vfs: enforce path limit");
	path[VFS_PATH_MAX - 1] = 0;
	check(!vfs_lookup(root_inode, path, &node) && node == root_inode,
	      "vfs: accept the longest supported path");

	int err = vfs_open(root_inode, "//bin/./../test//truncated", &file);
	check(!err, "vfs: walk nested directories in the boot archive");
	if (err)
		return;
	memset(buf, '-', sizeof(buf));
	check(file.inode->size == 4 && vfs_read_at(&file, 1, buf, sizeof(buf)) == 3 &&
	      !memcmp(buf, "ELF-", 4), "ramfs: short read at EOF preserves the rest of the buffer");
	check(vfs_read_at(&file, 4, buf, 1) == 0 &&
	      vfs_read_at(&file, SIZE_MAX, buf, sizeof(buf)) == 0 &&
	      vfs_read_at(&file, 1, buf, 0) == 0,
	      "ramfs: EOF, oversized offset and zero length");
	check(vfs_read_at(&file, 0, buf, 2) == 2 && !memcmp(buf, "\177E", 2),
	      "ramfs: reads use the supplied offset");
}

/* 小型独立样本可精确截断在头、名字、数据或对齐区。 */
static size_t member(u8 *dst, const char *name, unsigned int mode, const char *data)
{
	unsigned int namesize = strlen(name) + 1, size = strlen(data);
	size_t start = (110 + namesize + 3) & ~3UL;
	size_t end = (start + size + 3) & ~3UL;

	memset(dst, 0, end);
	snprintf((char *)dst, 111,
		 "07070100000001%08x00000000000000000000000100000000%08x"
		 "00000000000000000000000000000000%08x00000000", mode, size, namesize);
	memcpy(dst + 110, name, namesize);
	memcpy(dst + start, data, size);
	return end;
}

static void expect_rejected(const u8 *data, size_t size, int expected, const char *why)
{
	u64 before = buddy_free_pages();
	const struct inode *root = NULL;
	int err = ramfs_mount(data, size, &root);

	check(err == expected && !root && buddy_free_pages() == before, why);
	if (!err)
		ramfs_destroy(root);
}

void test_initramfs(void)
{
	u8 archive[512];
	const struct inode *root;
	struct file file;
	char buf[3];
	u64 before = buddy_free_pages();
	size_t dir = member(archive, "d", 0040755, "");
	size_t members = dir + member(archive + dir, "d/f", 0100644, "abc");
	size_t total = members + member(archive + members, "TRAILER!!!", 0, "");
	int err = ramfs_mount(archive, total, &root);

	check(!err, "newc: mount a directory and a padded regular file");
	if (!err) {
		check(!vfs_open(root, "/d/f", &file) && vfs_read_at(&file, 0, buf, 3) == 3 &&
		      !memcmp(buf, "abc", 3), "newc: read nested member data");
		ramfs_destroy(root);
	}
	check(buddy_free_pages() == before, "ramfs: release the whole tree");

	expect_rejected(archive, 5, -EINVAL, "newc: truncated header");
	expect_rejected(archive, dir + 111, -EINVAL, "newc: truncated name");
	expect_rejected(archive, members - 2, -EINVAL, "newc: truncated data");
	expect_rejected(archive, members - 1, -EINVAL, "newc: truncated alignment");
	expect_rejected(archive, members, -EINVAL, "newc: missing trailer rolls back prior members");
	archive[total] = 1;
	expect_rejected(archive, total + 1, -EINVAL, "newc: trailing garbage");
	archive[dir + 110 + 3] = 'x';
	expect_rejected(archive, total, -EINVAL, "newc: unterminated member name");
	archive[dir + 110 + 3] = 0;
	archive[dir + 54] = 'G';
	expect_rejected(archive, total, -EINVAL, "newc: malformed hexadecimal size field");
	archive[dir + 54] = '0';
	archive[0] = '1';
	expect_rejected(archive, total, -EINVAL, "newc: invalid magic");

	static const struct {
		const char *name;
		unsigned int mode;
		int error;
	} invalid[] = {
		{ "d", 0040755, -EEXIST }, { "d/../file", 0100644, -EINVAL },
		{ "/file", 0100644, -EINVAL }, { "d/f/sub", 0100644, -ENOTDIR },
		{ "absent/f", 0100644, -ENOENT }, { "link", 0120777, -EINVAL },
	};
	archive[0] = '0';
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		size_t end = members + member(archive + members, invalid[i].name, invalid[i].mode, "");
		end += member(archive + end, "TRAILER!!!", 0, "");
		expect_rejected(archive, end, invalid[i].error, invalid[i].name);
	}
}
