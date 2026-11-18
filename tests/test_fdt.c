#include <minux/fdt.h>
#include <minux/string.h>
#include "test.h"

static void put_be32(u8 *p, u32 value)
{
	p[0] = value >> 24;
	p[1] = value >> 16;
	p[2] = value >> 8;
	p[3] = value;
}

static void put_be64(u8 *p, u64 value)
{
	put_be32(p, value >> 32);
	put_be32(p + 4, value);
}

/* 72 字节的最小树：40 字节头部、16 字节保留区、16 字节根节点。 */
static void make_fdt(u8 *dtb)
{
	memset(dtb, 0, 72);
	put_be32(dtb, 0xd00dfeed);
	put_be32(dtb + 4, 72);
	put_be32(dtb + 8, 56);	/* 结构区偏移 */
	put_be32(dtb + 12, 72);	/* 空字符串区偏移 */
	put_be32(dtb + 16, 40);	/* 保留区偏移 */
	put_be32(dtb + 20, 17);	/* 版本 */
	put_be32(dtb + 24, 16);	/* 最低兼容版本 */
	put_be32(dtb + 36, 16);	/* 结构区长度 */
	put_be32(dtb + 56, 1);	/* FDT_BEGIN_NODE */
	put_be32(dtb + 64, 2);	/* FDT_END_NODE */
	put_be32(dtb + 68, 9);	/* FDT_END */
}

/* 根节点有一个 reg 属性：名字在字符串区，4 字节值在结构区偏移 76。 */
static void make_property_fdt(u8 *dtb)
{
	memset(dtb, 0, 92);
	make_fdt(dtb);
	put_be32(dtb + 4, 92);
	put_be32(dtb + 12, 88);	/* 字符串区偏移 */
	put_be32(dtb + 32, 4);	/* "reg\0" 长度 */
	put_be32(dtb + 36, 32);	/* 结构区长度 */
	put_be32(dtb + 64, 3);	/* FDT_PROP */
	put_be32(dtb + 68, 4);	/* 值长度 */
	put_be32(dtb + 72, 0);	/* 属性名在字符串区的偏移 */
	put_be32(dtb + 76, 0x12345678);
	put_be32(dtb + 80, 2);	/* FDT_END_NODE */
	put_be32(dtb + 84, 9);	/* FDT_END */
	memcpy(dtb + 88, "reg", 4);
}

/* 在结构区前插入一条 16 字节的保留项，后面仍跟全零结束项。 */
static void make_reservation_fdt(u8 *dtb)
{
	make_fdt(dtb);
	memset(dtb + 56, 0, 32);
	put_be32(dtb + 4, 88);
	put_be32(dtb + 8, 72);
	put_be32(dtb + 12, 88);
	put_be64(dtb + 40, 0x80000000ULL);
	put_be64(dtb + 48, 0x200000ULL);
	put_be32(dtb + 72, 1);	/* FDT_BEGIN_NODE */
	put_be32(dtb + 80, 2);	/* FDT_END_NODE */
	put_be32(dtb + 84, 9);	/* FDT_END */
}

static unsigned int node_count;
static bool root_end_seen;
static bool root_is_empty_name;
static unsigned int property_count;
static bool property_ok;
static const void *expected_value;
static unsigned int reservation_count;
static bool reservation_ok;

static void count_node(const char *name, unsigned int depth)
{
	node_count++;
	if (depth == 0)
		root_is_empty_name = name[0] == '\0';
}

static void end_root_node(unsigned int depth)
{
	root_end_seen = depth == 0 && node_count == 1;
}

static void capture_property(const char *name, const void *value,
			     u32 len, unsigned int depth)
{
	const u8 *bytes = value;

	property_count++;
	property_ok = strcmp(name, "reg") == 0 && value == expected_value &&
		      len == 4 && depth == 0 && bytes[0] == 0x12 &&
		      bytes[1] == 0x34 && bytes[2] == 0x56 && bytes[3] == 0x78;
}

static void capture_reservation(u64 address, u64 size)
{
	reservation_count++;
	reservation_ok = address == 0x80000000ULL && size == 0x200000ULL;
}

void test_fdt(void)
{
	u8 dtb[92];
	const u8 cell[] = { 0x01, 0x02, 0x03, 0x04 };
	const u8 two_cells[] = {
		0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0
	};
	u32 size = 0;
	u32 number = 0;
	u64 wide_number = 0;

	check(fdt_read_u32(cell, 4, &number) && number == 0x01020304,
	      "FDT: read big-endian u32 cell");
	check(!fdt_read_u32(cell, 3, &number),
	      "FDT: reject cell with wrong length");
	check(fdt_read_cells(two_cells, 8, 2, &wide_number) &&
	      wide_number == 0x123456789abcdef0ULL,
	      "FDT: combine two big-endian cells");
	check(!fdt_read_cells(two_cells, 4, 2, &wide_number),
	      "FDT: reject incomplete cell group");

	make_fdt(dtb);
	check(fdt_check_header(dtb, &size) && size == 72,
	      "FDT: valid header and big-endian size");

	put_be32(dtb + 36, 0xfffffff0);
	check(!fdt_check_header(dtb, &size), "FDT: structure exceeds total size");
	make_fdt(dtb);
	put_be32(dtb + 12, 0xfffffff0);
	check(!fdt_check_header(dtb, &size), "FDT: strings offset exceeds total size");
	make_fdt(dtb);
	put_be32(dtb + 16, 44);
	check(!fdt_check_header(dtb, &size), "FDT: unaligned reserve offset");
	make_fdt(dtb);
	put_be32(dtb + 20, 16);
	check(!fdt_check_header(dtb, &size), "FDT: unsupported header version");
	make_fdt(dtb);
	put_be32(dtb, 0);
	check(!fdt_check_header(dtb, &size), "FDT: wrong magic");
	make_fdt(dtb);
	put_be32(dtb + 4, 39);
	check(!fdt_check_header(dtb, &size), "FDT: short header");

	make_fdt(dtb);
	node_count = 0;
	root_is_empty_name = false;
	check(fdt_walk(dtb, count_node, NULL, end_root_node) && node_count == 1 &&
	      root_is_empty_name && root_end_seen, "FDT: walk root node");
	make_fdt(dtb);
	memset(dtb + 60, 'x', 12);
	check(!fdt_walk(dtb, count_node, NULL, NULL), "FDT: unterminated node name");
	make_fdt(dtb);
	put_be32(dtb + 68, 0);
	check(!fdt_walk(dtb, count_node, NULL, NULL), "FDT: missing end token");

	make_property_fdt(dtb);
	property_count = 0;
	property_ok = false;
	expected_value = dtb + 76;
	check(fdt_walk(dtb, NULL, capture_property, NULL) && property_count == 1 &&
	      property_ok, "FDT: property name, value address, and length");
	make_property_fdt(dtb);
	put_be32(dtb + 72, 4);
	check(!fdt_walk(dtb, NULL, capture_property, NULL),
	      "FDT: property name offset outside strings block");
	make_property_fdt(dtb);
	memcpy(dtb + 88, "regs", 4);
	check(!fdt_walk(dtb, NULL, capture_property, NULL),
	      "FDT: unterminated property name");
	make_property_fdt(dtb);
	put_be32(dtb + 68, 100);
	check(!fdt_walk(dtb, NULL, capture_property, NULL),
	      "FDT: property value exceeds structure block");

	make_fdt(dtb);
	reservation_count = 0;
	check(fdt_walk_reservations(dtb, capture_reservation) &&
	      reservation_count == 0, "FDT: empty reservation block");
	make_reservation_fdt(dtb);
	reservation_count = 0;
	reservation_ok = false;
	check(fdt_walk_reservations(dtb, capture_reservation) &&
	      reservation_count == 1 && reservation_ok,
	      "FDT: read big-endian reservation range");
	make_reservation_fdt(dtb);
	put_be64(dtb + 56, 0x80200000ULL);
	put_be64(dtb + 64, 0x1000ULL);
	check(!fdt_walk_reservations(dtb, NULL),
	      "FDT: reservation block missing terminator");
	make_reservation_fdt(dtb);
	put_be64(dtb + 40, ~0ULL);
	check(!fdt_walk_reservations(dtb, NULL),
	      "FDT: reservation address overflow");
}
