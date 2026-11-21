/*
 * 先运行启动自测，打开设备中断后运行抢占自测并汇总。
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
	{ "spinlock", test_spinlock },
	{ "printk", test_printk },
	{ "fdt",    test_fdt },
	{ "memblock", test_memblock },
	{ "buddy",  test_buddy },
	{ "slab",   test_slab },
	{ "sv39",   test_sv39 },
	{ "mm",     test_mm },
	{ "task",   test_task },
	{ "sched",  test_sched },
};

static int failures;
static int total;
static int all_failures;
static int all_total;

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

static void run_suite(const char *name, void (*run)(void))
{
	failures = 0;
	total = 0;

	test_puts(name);
	test_puts(":\n");
	run();

	test_puts(failures ? "  " : "  ok, ");
	test_putu(total - failures);
	test_puts("/");
	test_putu(total);
	test_puts(" passed\n");

	all_total += total;
	all_failures += failures;
}

void run_selftests(void)
{
	test_puts("==== selftest ====\n");
	for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i++)
		run_suite(suites[i].name, suites[i].run);
}

void run_runtime_selftests(void)
{
	run_suite("preempt", test_preempt);
	test_puts("==== ");
	test_putu(all_total - all_failures);
	test_puts("/");
	test_putu(all_total);
	test_puts(all_failures ? " passed, FAILED ====\n" : " passed, all OK ====\n");
}
