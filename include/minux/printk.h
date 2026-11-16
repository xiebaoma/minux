/*
 * 内核日志输出。对照 Linux 的 include/linux/printk.h、include/linux/kern_levels.h。
 *
 * 日志级别不是单独的参数，而是拼在格式串最前面的两个字符：
 *     printk(KERN_ERR "disk %d failed\n", n);
 * 预处理后相邻字符串会合并成  "\001" "3" "disk %d failed\n"
 * 即 "\0013disk %d failed\n"。printk 先识别并剥掉这个前缀，再决定是否输出。
 */
#ifndef _MINUX_PRINTK_H
#define _MINUX_PRINTK_H

#include <minux/types.h>
#include <minux/compiler.h>

#define KERN_SOH	"\001"		/* Start Of Header：级别前缀的起始字符 */
#define KERN_SOH_ASCII	'\001'

#define KERN_EMERG	KERN_SOH "0"	/* 系统不可用 */
#define KERN_ALERT	KERN_SOH "1"	/* 必须立刻处理 */
#define KERN_CRIT	KERN_SOH "2"	/* 严重错误 */
#define KERN_ERR	KERN_SOH "3"	/* 错误 */
#define KERN_WARNING	KERN_SOH "4"	/* 警告 */
#define KERN_NOTICE	KERN_SOH "5"	/* 正常但值得注意 */
#define KERN_INFO	KERN_SOH "6"	/* 一般信息 */
#define KERN_DEBUG	KERN_SOH "7"	/* 调试信息 */

/* 级别数字 < console_loglevel 的消息才会输出到控制台 */
#define CONSOLE_LOGLEVEL_DEFAULT	7	/* 输出除 DEBUG 以外的所有消息 */
#define CONSOLE_LOGLEVEL_ALL		8	/* 输出级别 0~7 的所有消息 */
#define MESSAGE_LOGLEVEL_DEFAULT	4	/* 没写级别的消息按 WARNING 处理 */

extern int console_loglevel;

/*
 * 如果 s 以合法的级别前缀开头（KERN_SOH_ASCII 后跟 '0'~'7'），
 * 返回那个级别字符（比如 '3'）；否则返回 0。
 */
static inline int printk_get_level(const char *s)
{
	if (s[0] == KERN_SOH_ASCII &&
	    s[1] >= '0' && s[1] <= '7')
		return s[1];

	return 0;
}

/* 如果 s 有级别前缀，返回跳过前缀之后的位置；否则原样返回 s */
static inline const char *printk_skip_level(const char *s)
{
	if (printk_get_level(s))
		return s + 2;

	return s;
}

/* 返回实际输出的字符数（被级别过滤掉时返回 0） */
int printk(const char *fmt, ...) __printf(1, 2);

/* 常用简写，和 Linux 一样 */
#define pr_emerg(fmt, ...)	printk(KERN_EMERG fmt, ##__VA_ARGS__)
#define pr_alert(fmt, ...)	printk(KERN_ALERT fmt, ##__VA_ARGS__)
#define pr_crit(fmt, ...)	printk(KERN_CRIT fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...)	printk(KERN_ERR fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...)	printk(KERN_WARNING fmt, ##__VA_ARGS__)
#define pr_notice(fmt, ...)	printk(KERN_NOTICE fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)	printk(KERN_INFO fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...)	printk(KERN_DEBUG fmt, ##__VA_ARGS__)

#endif /* _MINUX_PRINTK_H */
