#include <minux/errno.h>
#include <minux/fs.h>
#include <minux/string.h>
#include "ramfs_internal.h"

/* newc 的 6 字节 magic 后是 13 个字段，各用 8 个 ASCII 十六进制字符表示，与 CPU 字节序无关。 */
enum { MODE = 1, NLINK = 4, FILESIZE = 6, NAMESIZE = 11, CHECKSUM = 12, NFIELDS };
#define HEADER_SIZE (6 + 8 * NFIELDS)
#define MODE_TYPE 0170000
#define MODE_DIR  0040000
#define MODE_REG  0100000

static bool hex_field(const u8 *s, u32 *out)
{
	u32 value = 0;

	for (unsigned int i = 0; i < 8; i++) {
		u8 c = s[i];
		unsigned int digit;

		if (c >= '0' && c <= '9')
			digit = c - '0';
		else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f')
			digit = (c | 0x20) - 'a' + 10;
		else
			return false;
		value = value * 16 + digit;
	}
	*out = value;
	return true;
}

/* 以归档起点为基准对齐，先检查剩余长度，再推进游标。 */
static bool skip_padding(size_t *pos, size_t size)
{
	size_t pad = (-*pos) & 3;

	if (pad > size - *pos)
		return false;
	*pos += pad;
	return true;
}

int initramfs_unpack(struct ramfs_node *root, const u8 *archive, size_t size)
{
	size_t pos = 0;

	while (size - pos >= HEADER_SIZE) {
		const u8 *header = archive + pos;
		u32 fields[NFIELDS];
		const char *name;
		const u8 *data;
		size_t len;
		int err;

		if (memcmp(header, "070701", 6))
			return -EINVAL;
		for (unsigned int i = 0; i < NFIELDS; i++)
			if (!hex_field(header + 6 + 8 * i, &fields[i]))
				return -EINVAL;
		pos += HEADER_SIZE;
		len = fields[NAMESIZE];
		if (!len || len > size - pos || fields[CHECKSUM])
			return -EINVAL;
		name = (const char *)archive + pos;
		pos += len;
		if (name[len - 1] || !skip_padding(&pos, size))
			return -EINVAL;
		/* newc 允许文件名结尾有多个 NUL。 */
		size_t namelen = strlen(name);
		for (size_t i = namelen + 1; i < len; i++)
			if (name[i])
				return -EINVAL;
		if (namelen > VFS_PATH_MAX - 2)
			return -ENAMETOOLONG;
		data = archive + pos;
		if (fields[FILESIZE] > size - pos)
			return -EINVAL;
		pos += fields[FILESIZE];
		if (!skip_padding(&pos, size))
			return -EINVAL;
		if (!strcmp(name, "TRAILER!!!")) {
			if (fields[FILESIZE])
				return -EINVAL;
			while (pos < size)
				if (archive[pos++])
					return -EINVAL;
			return 0;
		}
		u32 type = fields[MODE] & MODE_TYPE;
		if ((type != MODE_REG && type != MODE_DIR) ||
		    (type == MODE_DIR && fields[FILESIZE]) ||
		    (type == MODE_REG && fields[NLINK] != 1))
			return -EINVAL;
		err = ramfs_add(root, name, type == MODE_DIR, data, fields[FILESIZE]);
		if (err)
			return err;
	}
	return -EINVAL; /* 此版本要求有 TRAILER!!!。 */
}
