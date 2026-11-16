/*
 * 早期控制台：通过 SBI 让 OpenSBI 代为输出字符。
 * 不需要任何驱动，内核一启动就能用；缺点是每个字符一次 ecall，很慢。
 */
#include <minux/console.h>
#include <asm/sbi.h>

void early_console_write(const char *s, size_t n)
{
	/* 不用自己把 '\n' 换成 "\r\n"：OpenSBI 的 sbi_putc 已经做了 */
	for (size_t i = 0; i < n; i++)
		sbi_console_putchar(s[i]);
}
