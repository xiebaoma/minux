/* Flattened Device Tree (DTB) 的早期读取接口。 */
#ifndef _MINUX_FDT_H
#define _MINUX_FDT_H

#include <minux/types.h>

/* 检查 v17 兼容头部及区块范围；尚不解析区块内部内容。 */
bool fdt_check_header(const void *dtb, u32 *total_size);

/* 一个 cell 是 4 字节大端整数；len 必须恰好为 4。 */
bool fdt_read_u32(const void *value, u32 len, u32 *result);

/* 将 1 或 2 个连续的大端 cell 合成一个最多 64 位的整数。 */
bool fdt_read_cells(const void *value, u32 len, u32 cells, u64 *result);

/* 头部指向的保留表：每条记录给出一段不可分配的物理地址范围。 */
typedef void (*fdt_reservation_visitor)(u64 address, u64 size);
bool fdt_walk_reservations(const void *dtb, fdt_reservation_visitor visit);

/* depth=0 表示根节点；name 指向 DTB 内已确认以 '\0' 结尾的名字。 */
typedef void (*fdt_node_visitor)(const char *name, unsigned int depth);

/*
 * 属性回调中的 depth 是所属节点的层级。
 * value 指向 DTB 内的原始字节，len 是字节数；回调不能假定它是字符串。
 */
typedef void (*fdt_property_visitor)(const char *name, const void *value,
				     u32 len, unsigned int depth);

/* 顺序走过节点和属性；不需要的回调可传 NULL。格式错误时返回 false。 */
bool fdt_walk(const void *dtb, fdt_node_visitor visit_node,
	      fdt_property_visitor visit_property);

#endif /* _MINUX_FDT_H */
