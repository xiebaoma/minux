#include <asm/sbi.h>
#include <minux/selftest.h>

static void early_puts(const char *s)
{
	while (*s)
		sbi_console_putchar(*s++);
}

void start_kernel(unsigned long hartid, unsigned long dtb_pa)
{
	(void)hartid;
	(void)dtb_pa;

	early_puts("Hello from Minux!\n");

	/* 只在 `make test` 构建时有效，普通构建下是空函数 */
	run_selftests();

	sbi_shutdown();
}
