#ifndef _MINUX_BYTEORDER_H
#define _MINUX_BYTEORDER_H

#include <minux/types.h>

/* 磁盘、virtqueue 和 MMIO 都使用小端；同一转换也用于小端到 CPU。 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
static inline u16 cpu_to_le16(u16 n) { return n; }
static inline u32 cpu_to_le32(u32 n) { return n; }
static inline u64 cpu_to_le64(u64 n) { return n; }
#else
static inline u16 cpu_to_le16(u16 n) { return __builtin_bswap16(n); }
static inline u32 cpu_to_le32(u32 n) { return __builtin_bswap32(n); }
static inline u64 cpu_to_le64(u64 n) { return __builtin_bswap64(n); }
#endif
#define le16_to_cpu cpu_to_le16
#define le32_to_cpu cpu_to_le32

#endif
