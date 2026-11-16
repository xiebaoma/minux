/*
 * printk：内核的 printf。对照 Linux 的 kernel/printk/printk.c。
 *
 * 当前为简化实现：
 *   - 支持 KERN_EMERG ~ KERN_DEBUG 日志级别
 *   - 支持 console_loglevel 过滤
 *   - 使用 vsnprintf 格式化字符串
 *   - 通过 early_console_write 输出到早期控制台
 *   - 使用栈上缓冲区，避免静态缓冲区的重入问题
 *
 * 暂不支持：
 *   - 环形日志缓冲区
 *   - 多控制台注册
 *   - 并发输出保护
 *   - 日志时间戳
 */

#include <minux/printk.h>
#include <minux/sprintf.h>
#include <minux/console.h>
#include <minux/stdarg.h>

/*
 * 控制台日志级别：
 * 只有 level < console_loglevel 的消息才会输出。
 *
 * 默认值为 7，即输出级别 0~6 的消息，
 * 过滤 KERN_DEBUG（级别 7）。
 */
int console_loglevel = CONSOLE_LOGLEVEL_DEFAULT;

/*
 * printk 格式化缓冲区大小。
 *
 * 内核栈只有 16 KiB，因此不能分配过大的局部数组。
 * 当前使用 512 字节，超过缓冲区容量的消息将被截断。
 */
#define PRINTK_BUF_SIZE 512

/*
 * printk - 格式化并输出内核日志
 * @fmt: 格式字符串，可以包含 KERN_* 日志级别前缀
 * @...: 格式化参数
 *
 * 返回值：
 *   > 0：实际输出的字符数
 *   = 0：消息被过滤，或者没有字符输出
 *   < 0：格式化失败
 */
int printk(const char *fmt, ...)
{
	char buf[PRINTK_BUF_SIZE];
	va_list args;
	int level;
	int ret;
	size_t len;

	/*
	 * 1. 解析日志级别。
	 *
	 * printk_get_level 返回字符 '0'~'7'，
	 * 需要减去 '0' 转换为整数。
	 *
	 * 没有显式级别时，使用默认消息级别。
	 */
	level = printk_get_level(fmt);

	if (level)
		level -= '0';
	else
		level = MESSAGE_LOGLEVEL_DEFAULT;

	/*
	 * 2. 根据控制台日志级别过滤。
	 *
	 * 数值越小，日志优先级越高。
	 * 不需要输出的消息直接返回，避免格式化开销。
	 */
	if (level >= console_loglevel)
		return 0;

	/*
	 * 3. 跳过格式字符串开头的日志级别前缀。
	 *
	 * 例如：
	 *   "\0013disk failed\n"
	 * 变为：
	 *   "disk failed\n"
	 */
	fmt = printk_skip_level(fmt);

	/*
	 * 4. 格式化日志。
	 *
	 * vsnprintf 保证在 size > 0 时，
	 * 不会写出缓冲区边界，并以 '\0' 结尾。
	 *
	 * 返回值是本应生成的字符数，不包含 '\0'。
	 */
	va_start(args, fmt);
	ret = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	if (ret <= 0)
		return ret;

	/*
	 * 5. 处理缓冲区截断。
	 *
	 * 如果 ret >= sizeof(buf)，说明格式化结果
	 * 超出了缓冲区容量。
	 *
	 * 实际最多只有 sizeof(buf) - 1 个有效字符，
	 * 最后一个字节留给 '\0'。
	 */
	len = (size_t)ret;

	if (len >= sizeof(buf))
		len = sizeof(buf) - 1;

	/*
	 * 6. 输出到早期控制台。
	 *
	 * early_console_write 接收明确的长度，
	 * 因此不会输出末尾的 '\0'。
	 */
	early_console_write(buf, len);

	return (int)len;
}