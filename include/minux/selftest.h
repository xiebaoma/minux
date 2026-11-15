/*
 * 启动自测入口。测试代码放在 tests/ 目录，只在 `make test` 时编译进内核。
 *
 * 这里用的是 Linux 的惯用法：功能关闭时提供一个空的 static inline 版本，
 * 调用方（start_kernel）就不需要写 #ifdef，编译器会把空调用整个优化掉。
 */
#ifndef _MINUX_SELFTEST_H
#define _MINUX_SELFTEST_H

#ifdef CONFIG_SELFTEST
void run_selftests(void);
#else
static inline void run_selftests(void) { }
#endif

#endif /* _MINUX_SELFTEST_H */
