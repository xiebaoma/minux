#include <asm/sbi.h>

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

	sbi_shutdown();
}
