
/*
 * panic：打印原因，然后停机。对照 Linux 的 kernel/panic.c。
 *
 * 当前为简化实现：
 *   - 支持 printf 风格的格式化消息
 *   - 使用 KERN_EMERG 输出 panic 信息
 *   - 防止递归调用 panic
 *   - 通过 machine_power_off 关闭虚拟机
 *
 * 暂不支持：
 *   - 多 CPU 并发 panic 处理
 *   - 内核调用栈打印
 *   - panic notifier
 *   - 自动重启
 *   - crash dump
 */

#include <minux/panic.h>
#include <minux/printk.h>
#include <minux/sprintf.h>
#include <minux/reboot.h>
#include <minux/stdarg.h>

/*
 * panic 消息缓冲区。
 *
 * 使用栈上缓冲区，避免多个调用共享格式化数据。
 * 当前 printk 的缓冲区为 512 字节，因此这里使用
 * 更小的缓冲区，为 panic 前缀预留空间。
 */
#define PANIC_BUF_SIZE 384

/*
 * 标记内核是否已经进入 panic。
 *
 * 防止 panic 过程中再次触发 panic，导致无限递归。
 *
 * 当前内核暂时只有单核执行环境，因此使用简单的
 * volatile 变量。后续支持 SMP 时需要改为原子操作。
 */
static volatile int panic_in_progress;

/*
 * panic - 报告不可恢复的内核错误并停机
 * @fmt: printf 风格的格式字符串
 * @...: 格式化参数
 *
 * 此函数永不返回。
 */
void panic(const char *fmt, ...)
{
	char buf[PANIC_BUF_SIZE];
	va_list args;
	int ret;

	/*
	 * 1. 防止递归 panic。
	 *
	 * 如果已经处于 panic 状态，说明停机过程中
	 * 再次发生了不可恢复的错误。
	 *
	 * 此时不能继续调用 printk，否则可能再次
	 * 触发 panic，形成无限递归。
	 */
	if (panic_in_progress)
		machine_power_off();

	panic_in_progress = 1;

	/*
	 * 2. 格式化 panic 消息。
	 *
	 * vsnprintf 最多写入 sizeof(buf) 个字节，
	 * 包括末尾的 '\0'。
	 *
	 * 过长的消息会被截断。
	 */
	va_start(args, fmt);
	ret = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	/*
	 * 3. 输出 panic 信息。
	 *
	 * 使用 KERN_EMERG（级别 0），这是内核
	 * 日志系统中的最高优先级。printk 仍会按 console_loglevel
	 * 过滤消息，所以先打开所有级别，确保关机前能看到原因。
	 */
	console_loglevel = CONSOLE_LOGLEVEL_ALL;
	if (ret < 0) {
		pr_emerg("Kernel panic - not syncing: "
			 "<formatting failed>\n");
	} else {
		pr_emerg("Kernel panic - not syncing: %s\n", buf);
	}

	/*
	 * 4. 停机。
	 *
	 * 当前选择关机而不是原地挂起。
	 * 在 QEMU 环境中，这样可以直接退出模拟器。
	 *
	 * machine_power_off 声明为 __noreturn，
	 * 因此 panic 不会返回调用方。
	 */
	machine_power_off();
}
