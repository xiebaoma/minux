#include <minux/fdt.h>

/*
 * DTB 是一整块连续的字节。开头 40 字节是头部，每项占 4 字节：
 *
 * 字节偏移   内容
 *    0       magic（判断这是不是 DTB）
 *    4       totalsize（整块 DTB 的字节数）
 *    8/12    节点结构区 / 属性名字符串区的起点
 *   16       内存保留区的起点
 *   20/24    当前版本 / 最低兼容版本
 *   28       启动 CPU 编号（本函数暂不使用）
 *   32/36    字符串区 / 节点结构区的字节数
 *
 * 所有“起点”都是相对 DTB 开头的字节偏移，不是内存地址。
 */
#define FDT_MAGIC       0xd00dfeedU
#define FDT_HEADER_SIZE 40U
#define FDT_VERSION     17U

#define FDT_BEGIN_NODE  1U
#define FDT_END_NODE    2U
#define FDT_PROP        3U
#define FDT_NOP         4U
#define FDT_END         9U

/*
 * DTB 的整数按大端存储：字节 d0 0d fe ed 表示 0xd00dfeed。
 * 逐字节读取并移到对应位置，避免依赖 RISC-V 的小端读取方式。
 */
static u32 fdt_be32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
	       ((u32)p[2] << 8) | (u32)p[3];
}

static u64 fdt_be64(const u8 *p)
{
	return ((u64)fdt_be32(p) << 32) | fdt_be32(p + 4);
}

bool fdt_read_u32(const void *value, u32 len, u32 *result)
{
	if (!value || !result || len != 4)
		return false;
	*result = fdt_be32(value);
	return true;
}

bool fdt_read_cells(const void *value, u32 len, u32 cells, u64 *result)
{
	const u8 *p = value;
	u64 number = 0;

	if (!p || !result || cells < 1 || cells > 2 || len != cells * 4)
		return false;
	for (u32 i = 0; i < cells; i++)
		number = (number << 32) | fdt_be32(p + i * 4);
	*result = number;
	return true;
}

/*
 * 判断从 off 开始、长 len 的区间是否位于 [0, total) 内。
 * 例如 total=100、off=90、len=20 时，20 > 100-90，因此越界。
 * 先确认 off <= total 才能做减法；不用 off+len，以免加法溢出。
 */
static bool fdt_range_ok(u32 off, u32 len, u32 total)
{
	return off <= total && len <= total - off;
}

bool fdt_check_header(const void *dtb, u32 *total_size)
{
	/* 转成 u8 指针后，p + N 就是从 DTB 开头向后数 N 个字节。 */
	const u8 *p = dtb;
	u32 size, struct_off, strings_off, reserve_off;
	u32 struct_size, strings_size;

	if (!p || !total_size)
		return false;
	/* 前 4 字节必须是 DTB 的固定标记。 */
	if (fdt_be32(p) != FDT_MAGIC)
		return false;

	/* 读取总长；连 40 字节头部都装不下的 DTB 不可能有效。 */
	size = fdt_be32(p + 4);
	if (size < FDT_HEADER_SIZE)
		return false;
	/*
	 * v17 头部提供了后面要用的两个区块长度。
	 * 当前版本至少为 17，且最低兼容版本不高于 17，才能按它解析。
	 */
	if (fdt_be32(p + 20) < FDT_VERSION ||
	    fdt_be32(p + 24) > FDT_VERSION)
		return false;

	/* 只取各区块的位置和长度；此时还没有读任何节点或属性。 */
	struct_off = fdt_be32(p + 8);
	strings_off = fdt_be32(p + 12);
	reserve_off = fdt_be32(p + 16);
	strings_size = fdt_be32(p + 32);
	struct_size = fdt_be32(p + 36);

	/*
	 * 保留区要从头部之后开始，起点按 8 字节对齐（& 7 为 0），
	 * 并且至少能放下一个 16 字节的结束标记。
	 */
	if (reserve_off < FDT_HEADER_SIZE || (reserve_off & 7) ||
	    !fdt_range_ok(reserve_off, 16, size))
		return false;
	/*
	 * 节点结构区按 4 字节对齐，长度至少够放一个 4 字节结束标记；
	 * 它要在保留区的最小范围之后，而且不能超出 DTB 总长。
	 */
	if ((struct_off & 3) || struct_size < 4 || (struct_size & 3) ||
	    struct_off < reserve_off + 16 ||
	    !fdt_range_ok(struct_off, struct_size, size))
		return false;
	/* 字符串区位于结构区之后；它可以为空，但不能越过 DTB 末尾。 */
	if (strings_off < struct_off + struct_size ||
	    !fdt_range_ok(strings_off, strings_size, size))
		return false;

	/* 只有全部检查通过，才把读到的总长写给调用者。 */
	*total_size = size;
	return true;
}

bool fdt_walk_reservations(const void *dtb, fdt_reservation_visitor visit)
{
	const u8 *p = dtb;
	u32 total_size, pos, end;

	if (!fdt_check_header(dtb, &total_size))
		return false;
	pos = fdt_be32(p + 16); /* 保留表起点 */
	end = fdt_be32(p + 8);  /* 后面的结构区起点，即保留表的边界 */

	/* 每项是 8 字节地址 + 8 字节长度；全零项标记结束。 */
	while (fdt_range_ok(pos, 16, end)) {
		u64 address = fdt_be64(p + pos);
		u64 size = fdt_be64(p + pos + 8);

		pos += 16;
		if (address == 0 && size == 0)
			return true;
		if (size == 0 || address > ~(u64)0 - size)
			return false;
		if (visit)
			visit(address, size);
	}
	return false; /* 到达结构区前仍未看到全零结束项。 */
}

/* 节点名是以 '\0' 结尾的字符串，后面补零到 4 字节边界。 */
static bool fdt_read_name(const u8 *block, u32 size, u32 *pos,
			  const char **name)
{
	u32 start = *pos;
	u32 padding;

	while (*pos < size && block[*pos] != '\0')
		(*pos)++;
	if (*pos == size)
		return false;

	*name = (const char *)(block + start);
	(*pos)++;  /* 跳过结尾的 '\0' */
	padding = (4 - (*pos & 3)) & 3;
	if (!fdt_range_ok(*pos, padding, size))
		return false;
	*pos += padding;
	return true;
}

/* nameoff 是相对字符串区开头的偏移；名字必须在区内以 '\0' 结尾。 */
static const char *fdt_property_name(const u8 *strings, u32 size, u32 nameoff)
{
	if (nameoff >= size)
		return NULL;
	for (u32 i = nameoff; i < size; i++) {
		if (strings[i] == '\0')
			return (const char *)(strings + nameoff);
	}
	return NULL;
}

bool fdt_walk(const void *dtb, fdt_node_visitor visit_node,
	      fdt_property_visitor visit_property)
{
	const u8 *p = dtb;
	const u8 *block, *strings;
	u32 total_size, size, strings_size, pos = 0;
	unsigned int depth = 0;
	bool saw_root = false;

	if (!fdt_check_header(dtb, &total_size))
		return false;

	block = p + fdt_be32(p + 8);
	size = fdt_be32(p + 36);
	strings = p + fdt_be32(p + 12);
	strings_size = fdt_be32(p + 32);
	while (fdt_range_ok(pos, 4, size)) {
		u32 token = fdt_be32(block + pos);

		pos += 4;
		switch (token) {
		case FDT_BEGIN_NODE: {
			const char *name;

			/* 只能有一个根节点；根节点闭合后不能再开始新节点。 */
			if (depth == 0 && saw_root)
				return false;
			if (!fdt_read_name(block, size, &pos, &name))
				return false;
			if (visit_node)
				visit_node(name, depth);
			depth++;
			saw_root = true;
			break;
		}
		case FDT_END_NODE:
			if (depth == 0)
				return false;
			depth--;
			break;
		case FDT_PROP: {
			const char *name;
			u32 len, nameoff, padding;

			/* 属性头部是 4 字节值长度和 4 字节名字偏移。 */
			if (depth == 0 || !fdt_range_ok(pos, 8, size))
				return false;
			len = fdt_be32(block + pos);
			nameoff = fdt_be32(block + pos + 4);
			name = fdt_property_name(strings, strings_size, nameoff);
			if (!name)
				return false;
			pos += 8;
			/* pos 现在指向属性值的第一个字节。 */
			if (!fdt_range_ok(pos, len, size))
				return false;
			padding = (4 - ((pos + len) & 3)) & 3;
			if (!fdt_range_ok(pos + len, padding, size))
				return false;
			if (visit_property)
				visit_property(name, block + pos, len, depth - 1);
			pos += len + padding;
			break;
		}
		case FDT_NOP:
			break;
		case FDT_END:
			return saw_root && depth == 0 && pos == size;
		default:
			return false;
		}
	}
	return false; /* 结构区用尽，却没有读到 FDT_END。 */
}
