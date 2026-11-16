
/*
 * 断言类宏。对照 Linux 的 include/asm-generic/bug.h。
 *
 *   BUG()          “不可能走到这里”：打印位置后 panic
 *   BUG_ON(cond)   cond 为真时 BUG()
 *   WARN_ON(cond)  cond 为真时打印警告和位置，但继续运行；
 *                  它本身是个表达式，值为 cond 的真假，所以可以写：
 *                      if (WARN_ON(!ptr))
 *                              return -EINVAL;
 *
 * Linux 的原则：能 WARN 就别 BUG。BUG 会让整个系统停下来。
 */
#ifndef _MINUX_BUG_H
#define _MINUX_BUG_H

#include <minux/compiler.h>
#include <minux/printk.h>
#include <minux/panic.h>

/*
 * BUG - 报告不可恢复的内核错误
 *
 * 打印发生错误的文件、行号和函数名，然后调用 panic。
 *
 * __FILE__：当前源文件名
 * __LINE__：当前源代码行号
 * __func__：当前函数名
 *
 * 必须使用宏，以便获取 BUG() 调用位置的信息，
 * 而不是某个公共辅助函数内部的位置。
 *
 * panic() 已经负责输出 "Kernel panic - not syncing:" 前缀。
 */
#define BUG()                                                   \
	do {                                                    \
		panic("BUG: failure at %s:%d/%s()!",           \
		      __FILE__, __LINE__, __func__);            \
	} while (0)

/*
 * BUG_ON - 条件成立时触发 BUG
 * @cond: 需要检查的条件
 *
 * unlikely() 提示编译器该条件通常为假，
 * 有利于优化正常执行路径。
 *
 * cond 只会被求值一次。
 */
#define BUG_ON(cond)                                            \
	do {                                                    \
		if (unlikely(cond))                             \
			BUG();                                  \
	} while (0)

/*
 * WARN_ON - 条件成立时打印警告，但不停止内核
 * @cond: 需要检查的条件
 *
 * 返回值：
 *   0：条件为假
 *   1：条件为真，已触发警告
 *
 * 使用 GNU C 语句表达式 ({ ... })，
 * 使宏可以作为普通表达式使用。
 *
 * 先保存条件结果，避免 cond 被重复求值。
 */
#define WARN_ON(cond)                                           \
	({                                                      \
		int __warn_on_cond = !!(cond);                 \
		if (unlikely(__warn_on_cond)) {                \
			pr_warn("WARNING: at %s:%d %s()\n",   \
				__FILE__, __LINE__, __func__); \
		}                                               \
		__warn_on_cond;                                 \
	})

#endif /* _MINUX_BUG_H */
