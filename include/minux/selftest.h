/*
 * 启动自测入口。测试代码放在 tests/ 目录，只在 `make test` 时编译进内核。
 *
 * 这里用的是 Linux 的惯用法：功能关闭时提供一个空的 static inline 版本，
 * 调用方（start_kernel）就不需要写 #ifdef，编译器会把空调用整个优化掉。
 */
#ifndef _MINUX_SELFTEST_H
#define _MINUX_SELFTEST_H

#ifdef CONFIG_SELFTEST
#include <minux/types.h>

void run_selftests(void);
void run_rootfs_selftests(void);
void run_runtime_selftests(void);
long selftest_user_copy(uintptr_t to, uintptr_t from, size_t size);
long selftest_user_wait(unsigned long action);
#else
static inline void run_selftests(void) { }
static inline void run_rootfs_selftests(void) { }
static inline void run_runtime_selftests(void) { }
#endif

#endif /* _MINUX_SELFTEST_H */
