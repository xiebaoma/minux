#include <minux/block.h>
#include <minux/container_of.h>
#include <minux/errno.h>
#include <minux/ext2.h>
#include <minux/fs.h>
#include <minux/slab.h>
#include <minux/string.h>

#define EXT2_ROOT_INO 2
#define EXT2_DIRECT_BLOCKS 12
#define TYPE_MASK 0170000
#define TYPE_DIR  0040000
#define TYPE_REG  0100000

/* 字节偏移按 ext2 磁盘格式定义，与宿主架构的对齐无关。 */
enum ext2_super_offset {
	S_INODES = 0, S_BLOCKS = 4, S_FIRST_DATA_BLOCK = 20,
	S_LOG_BLOCK_SIZE = 24, S_LOG_FRAG_SIZE = 28, S_BLOCKS_PER_GROUP = 32,
	S_INODES_PER_GROUP = 40, S_MAGIC = 56, S_STATE = 58, S_REV = 76,
	S_INODE_SIZE = 88, S_COMPAT = 92, S_INCOMPAT = 96, S_RO_COMPAT = 100,
};

enum ext2_inode_offset {
	I_MODE = 0, I_SIZE = 4, I_DTIME = 20, I_LINKS = 26, I_BLOCK = 40, I_SIZE_HIGH = 108,
};

#define EXT2_FILETYPE 0x2U
#define EXT2_COMPAT_SUPPORTED 0x38U	/* ext_attr, resize_inode, dir_index */
#define EXT2_RO_SUPPORTED 0x3U		/* sparse_super, large_file */

struct ext2_fs {
	struct block_device *dev;
	u32 blocks, inodes, first_block, block_size;
	u32 blocks_per_group, inodes_per_group, inode_size;
	bool filetype;
	struct ext2_node *nodes;
};

struct ext2_node {
	struct inode inode;
	struct ext2_fs *fs;
	struct ext2_node *next;
	u32 ino, block[EXT2_DIRECT_BLOCKS + 1];
};

/* 磁盘字段允许非对齐，统一按小端解码。 */
static u16 get16(const u8 *p)
{
	return p[0] | (u16)p[1] << 8;
}

static u32 get32(const u8 *p)
{
	return get16(p) | (u32)get16(p + 2) << 16;
}

static int disk_bytes(struct block_device *dev, u64 pos, void *dst, size_t count)
{
	u8 sector[BLK_SECTOR_SIZE];
	u8 *p = dst;

	while (count) {
		size_t off = pos % BLK_SECTOR_SIZE;
		size_t n = BLK_SECTOR_SIZE - off;
		int err = blk_read_cached(dev, pos / BLK_SECTOR_SIZE, 1, sector);

		if (err)
			return err;
		if (n > count)
			n = count;
		memcpy(p, sector + off, n);
		pos += n;
		p += n;
		count -= n;
	}
	return 0;
}

//把文件内的逻辑块编号 index，转换成磁盘上的 ext2 数据块编号，写到 *out
static int data_block(const struct ext2_node *node, u32 index, u32 *out)
{
	struct ext2_fs *fs = node->fs;
	u32 block;

	if (index < EXT2_DIRECT_BLOCKS) {
		block = node->block[index];
	} else {
		u8 ptr[4];

		index -= EXT2_DIRECT_BLOCKS;
		if (index >= fs->block_size / sizeof(ptr))
			return -EFBIG;
		block = node->block[EXT2_DIRECT_BLOCKS];
		if (block >= fs->blocks)
			return -EIO;
		if (block) {
			int err = disk_bytes(fs->dev, (u64)block * fs->block_size + index * 4,
					     ptr, sizeof(ptr));
			if (err)
				return err;
			block = get32(ptr);
		}
	}
	if (block >= fs->blocks)
		return -EIO;
	*out = block;
	return 0;
}

/* 调用者将范围限制在 inode.size 以内。普通文件中的洞读作零。 */
static ssize_t ext2_read(const struct inode *inode, size_t offset, void *buf, size_t count)
{
	const struct ext2_node *node = container_of(inode, struct ext2_node, inode);
	struct ext2_fs *fs = node->fs;
	size_t done = 0;

	while (done < count) {
		size_t pos = offset + done;
		size_t within = pos % fs->block_size;
		size_t n = fs->block_size - within;
		u32 block;
		int err = data_block(node, pos / fs->block_size, &block);
		if (err)
			return done ? (ssize_t)done : err;
		if (n > count - done)
			n = count - done;
		if (!block)
			memset((u8 *)buf + done, 0, n);
		else {
			err = disk_bytes(fs->dev, (u64)block * fs->block_size + within,
					 (u8 *)buf + done, n);
			if (err)
				return done ? (ssize_t)done : err;
		}
		done += n;
	}
	return done;
}

static int load_inode(struct ext2_fs *fs, u32 ino, struct inode *parent, struct inode **out);

static int ext2_lookup(const struct inode *inode, const char *name, size_t len, struct inode **out)
{
	struct ext2_node *dir = container_of(inode, struct ext2_node, inode);
	struct ext2_fs *fs = dir->fs;

	for (size_t pos = 0; pos < inode->size;) {
		u8 header[8], entry_name[255];
		size_t left = fs->block_size - pos % fs->block_size;
		if (left < sizeof(header) || inode->size - pos < sizeof(header))
			return -EIO;
		ssize_t n = ext2_read(inode, pos, header, sizeof(header));
		if (n != sizeof(header))
			return n < 0 ? (int)n : -EIO;
		u32 ino = get32(header);
		u16 rec = get16(header + 4);
		u16 namesize = fs->filetype ? header[6] : get16(header + 6);
		if (rec < sizeof(header) || (rec & 3) || rec > left || rec > inode->size - pos ||
		    namesize > sizeof(entry_name) || namesize > rec - sizeof(header) || ino > fs->inodes)
			return -EIO;
		if (ino && namesize == len) {
			n = ext2_read(inode, pos + sizeof(header), entry_name, len);
			if (n != (ssize_t)len)
				return n < 0 ? (int)n : -EIO;
			if (!memcmp(name, entry_name, len))
				return load_inode(fs, ino, &dir->inode, out);
		}
		pos += rec;
	}
	return -ENOENT;
}

static int read_inode(struct ext2_fs *fs, u32 ino, u8 raw[128])
{
	u32 group = (ino - 1) / fs->inodes_per_group;
	u32 index = (ino - 1) % fs->inodes_per_group;
	u64 desc = (u64)(fs->first_block + 1) * fs->block_size + (u64)group * 32;
	u8 ptr[4];
	int err = disk_bytes(fs->dev, desc + 8, ptr, sizeof(ptr));
	if (err)
		return err;
	u32 table = get32(ptr);
	u64 start = fs->first_block + (u64)group * fs->blocks_per_group;
	u64 end = start + fs->blocks_per_group;
	if (end > fs->blocks)
		end = fs->blocks;
	u64 bytes = (u64)fs->inodes_per_group * fs->inode_size;
	if (!table || table < start || table >= end || bytes > (end - table) * fs->block_size)
		return -EIO;
	return disk_bytes(fs->dev, (u64)table * fs->block_size + (u64)index * fs->inode_size,
			  raw, 128);
}

static int load_inode(struct ext2_fs *fs, u32 ino, struct inode *parent, struct inode **out)
{
	u8 raw[128];

	for (struct ext2_node *n = fs->nodes; n; n = n->next) {
		if (n->ino == ino) {
			if (n->inode.lookup && parent != n->inode.parent)
				return -EIO;
			*out = &n->inode;
			return 0;
		}
	}
	int err = read_inode(fs, ino, raw);
	if (err)
		return err;
	u16 type = get16(raw + I_MODE) & TYPE_MASK;
	u32 size = get32(raw + I_SIZE);
	if (type != TYPE_DIR && type != TYPE_REG)
		return -ENOTSUP;
	if (!get16(raw + I_LINKS) || get32(raw + I_DTIME))
		return -EIO;
	if ((type == TYPE_REG && get32(raw + I_SIZE_HIGH)) ||
	    size > (EXT2_DIRECT_BLOCKS + fs->block_size / 4) * fs->block_size)
		return -EFBIG;
	if (type == TYPE_DIR && (!size || size % fs->block_size))
		return -EIO;
	struct ext2_node *node = kzalloc(sizeof(*node));
	if (!node)
		return -ENOMEM;
	node->ino = ino;
	node->fs = fs;
	node->inode.parent = parent;
	node->inode.size = size;
	if (type == TYPE_DIR)
		node->inode.lookup = ext2_lookup;
	else
		node->inode.read = ext2_read;
	for (unsigned int i = 0; i <= EXT2_DIRECT_BLOCKS; i++)
		node->block[i] = get32(raw + I_BLOCK + i * 4);
	node->next = fs->nodes;
	fs->nodes = node;
	*out = &node->inode;
	return 0;
}

static int superblock(struct ext2_fs *fs)
{
	u8 s[104];
	int err = disk_bytes(fs->dev, 1024, s, sizeof(s));
	if (err)
		return err;
	if (get16(s + S_MAGIC) != 0xef53)
		return -EINVAL;
	u32 rev = get32(s + S_REV), shift = get32(s + S_LOG_BLOCK_SIZE);
	if (rev > 1 || shift > 2 || get32(s + S_LOG_FRAG_SIZE) != shift)
		return -ENOTSUP;
	if (get16(s + S_STATE) != 1)
		return -EIO;
	if (rev && ((get32(s + S_COMPAT) & ~EXT2_COMPAT_SUPPORTED) || (get32(s + S_INCOMPAT) & ~EXT2_FILETYPE) ||
		    (get32(s + S_RO_COMPAT) & ~EXT2_RO_SUPPORTED)))
		return -ENOTSUP;
	fs->filetype = rev && (get32(s + S_INCOMPAT) & EXT2_FILETYPE);
	fs->block_size = 1024U << shift;
	fs->inode_size = rev ? get16(s + S_INODE_SIZE) : 128;
	fs->inodes = get32(s + S_INODES);
	fs->blocks = get32(s + S_BLOCKS);
	fs->first_block = get32(s + S_FIRST_DATA_BLOCK);
	fs->blocks_per_group = get32(s + S_BLOCKS_PER_GROUP);
	fs->inodes_per_group = get32(s + S_INODES_PER_GROUP);
	if (fs->inodes < EXT2_ROOT_INO || fs->first_block != (shift == 0) ||
	    fs->blocks <= fs->first_block + 1 ||
	    (u64)fs->blocks * (fs->block_size / BLK_SECTOR_SIZE) > fs->dev->sectors ||
	    !fs->blocks_per_group || fs->blocks_per_group > fs->block_size * 8 ||
	    !fs->inodes_per_group || fs->inodes_per_group > fs->block_size * 8 ||
	    fs->inode_size < 128 || fs->inode_size > fs->block_size ||
	    (fs->inode_size & (fs->inode_size - 1)))
		return -EIO;
	u32 groups = 1 + (fs->blocks - fs->first_block - 1) / fs->blocks_per_group;
	if (1 + (fs->inodes - 1) / fs->inodes_per_group != groups ||
	    (u64)groups * 32 > (u64)(fs->blocks - fs->first_block - 1) * fs->block_size)
		return -EIO;
	return 0;
}

static void free_fs(struct ext2_fs *fs)
{
	while (fs->nodes) {
		struct ext2_node *n = fs->nodes;
		fs->nodes = n->next;
		kfree(n);
	}
	kfree(fs);
}

int ext2_mount(struct block_device *dev, struct inode **out)
{
	struct inode *root;
	struct ext2_fs *fs = kzalloc(sizeof(*fs));
	if (!fs)
		return -ENOMEM;
	fs->dev = dev;
	int err = superblock(fs);
	if (!err)
		err = load_inode(fs, EXT2_ROOT_INO, NULL, &root);
	if (!err && !root->lookup)
		err = -EIO;
	if (err)
		free_fs(fs);
	else
		*out = root;
	return err;
}

void ext2_destroy(struct inode *root)
{
	free_fs(container_of(root, struct ext2_node, inode)->fs);
}
