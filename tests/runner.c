/*
 * 自测运行器：依次运行 suites[] 里的每组测试，最后汇总。
 */
#include <minux/selftest.h>
#include <asm/sbi.h>
#include "test.h"

struct test_suite {
	const char *name;
	void (*run)(void);
};

static const struct test_suite suites[] = {
	{ "string", test_string },
	{ "list",   test_list },
	{ "printk", test_printk },
	{ "fdt",    test_fdt },
	{ "memblock", test_memblock },
};

static int failures;
static int total;

void test_puts(const char *s)
{
	while (*s)
		sbi_console_putchar(*s++);
}

static void test_putu(unsigned int n)
{
	char buf[10];
	int i = 0;

	do {
		buf[i++] = '0' + n % 10;
		n /= 10;
	} while (n);
	while (i)
		sbi_console_putchar(buf[--i]);
}

void check(bool ok, const char *what)
{
	total++;
	if (!ok) {
		failures++;
		test_puts("  [FAIL] ");
		test_puts(what);
		test_puts("\n");
	}
}

void run_selftests(void)
{
	int all_total = 0, all_failures = 0;

	test_puts("==== selftest ====\n");

	for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
		failures = 0;
		total = 0;

		test_puts(suites[i].name);
		test_puts(":\n");
		suites[i].run();

		test_puts(failures ? "  " : "  ok, ");
		test_putu(total - failures);
		test_puts("/");
		test_putu(total);
		test_puts(" passed\n");

		all_total += total;
		all_failures += failures;
	}

	test_puts("==== ");
	test_putu(all_total - all_failures);
	test_puts("/");
	test_putu(all_total);
	test_puts(all_failures ? " passed, FAILED ====\n" : " passed, all OK ====\n");
}
