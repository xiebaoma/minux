/*
 * lib/vsprintf.c、printk 级别前缀、WARN_ON 的测试。
 *
 * 每个 T(期望结果, 格式串, 参数...) 检查两件事：
 *   - 输出的字符串和期望完全一致
 *   - 返回值等于期望字符串的长度
 * 这些期望值都和 C 标准库的 printf 行为一致（已在主机上用 libc 核对过），
 * 只有 %p 例外：C 标准规定 %p 的格式由实现决定，我们固定输出 16 位，方便对齐地址。
 */
#include <minux/sprintf.h>
#include <minux/string.h>
#include <minux/limits.h>
#include <minux/printk.h>
#include <minux/bug.h>
#include "test.h"

static void __printf(2, 3) expect_fmt(const char *expect, const char *fmt, ...)
{
	char buf[128];
	va_list args;
	int n;

	va_start(args, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	if (strcmp(buf, expect) == 0 && n == (int)strlen(expect)) {
		check(true, fmt);
		return;
	}
	check(false, fmt);
	test_puts("         want \"");
	test_puts(expect);
	test_puts("\"\n         got  \"");
	test_puts(buf);
	test_puts("\"");
	if (n != (int)strlen(expect))
		test_puts(" (and wrong return value)");
	test_puts("\n");
}

#define T(expect, ...)	expect_fmt(expect, __VA_ARGS__)

static void test_basic(void)
{
	T("hello", "hello");
	T("100%", "100%%");
	T("a%b", "a%%b");
}

static void test_integers(void)
{
	T("0", "%d", 0);
	T("42", "%d", 42);
	T("-42", "%d", -42);
	T("42", "%i", 42);
	T("2147483647", "%d", INT_MAX);
	T("-2147483648", "%d", INT_MIN);
	T("4294967295", "%u", UINT_MAX);
	T("-1", "%ld", -1L);
	T("9223372036854775807", "%ld", LONG_MAX);
	T("-9223372036854775808", "%ld", LONG_MIN);
	T("18446744073709551615", "%lu", ULONG_MAX);
	T("-9223372036854775808", "%lld", LLONG_MIN);
	T("18446744073709551615", "%llu", ULLONG_MAX);
	T("123", "%zu", (size_t)123);
}

static void test_hex(void)
{
	T("0", "%x", 0);
	T("ff", "%x", 255);
	T("FF", "%X", 255);
	T("deadbeef", "%x", 0xdeadbeefU);
	T("ffffffff", "%x", -1);
	T("deadbeefcafe", "%lx", 0xdeadbeefcafeUL);
	T("ffffffffffffffff", "%llx", ULLONG_MAX);
	T("ABCDEF", "%zX", (size_t)0xabcdef);
}

static void test_width(void)
{
	T("   42", "%5d", 42);
	T("42   |", "%-5d|", 42);
	T("00042", "%05d", 42);
	T("-0042", "%05d", -42);
	T("  -42", "%5d", -42);
	T("12345", "%2d", 12345);
	T("00001234", "%08lx", 0x1234UL);
	T("0000000080200000", "%016lx", 0x80200000UL);
	T("    ff", "%6x", 255);
}

static void test_char_string(void)
{
	/* volatile：不让 GCC 在编译期看出它是 NULL 而报警告，这里就是要测 NULL */
	const char *volatile null_str = NULL;

	T("A", "%c", 'A');
	T("[x]", "[%c]", 'x');
	T("abc", "%s", "abc");
	T("", "%s", "");
	T("  abc", "%5s", "abc");
	T("abc  |", "%-5s|", "abc");
	T("abcdef", "%3s", "abcdef");
	T("(null)", "%s", null_str);
}

static void test_pointer(void)
{
	T("0x0000000080200000", "%p", (void *)0x80200000UL);
	T("0x0000000000000000", "%p", NULL);
}

static void test_mixed(void)
{
	T("hart 0, mem 0x80000000-0x88000000 (128 MiB)",
	  "hart %lu, mem 0x%lx-0x%lx (%lu MiB)",
	  0UL, 0x80000000UL, 0x88000000UL, 128UL);
	T("name=init pid=1 state=R",
	  "name=%s pid=%d state=%c", "init", 1, 'R');
}

/* 截断：缓冲区不够时只写 size-1 个字符 + '\0'，返回值仍是完整长度 */
static void test_truncation(void)
{
	char buf[8];
	int n;

	memset(buf, 'Z', sizeof(buf));
	n = snprintf(buf, 6, "hello world");
	check(n == 11, "snprintf: returns full length when truncated");
	check(memcmp(buf, "hello\0ZZ", 8) == 0,
	      "snprintf: writes size-1 chars + '\\0', nothing beyond");

	memset(buf, 'Z', sizeof(buf));
	n = snprintf(buf, 0, "abc");
	check(n == 3, "snprintf: size 0 returns full length");
	check(buf[0] == 'Z', "snprintf: size 0 writes nothing");

	memset(buf, 'Z', sizeof(buf));
	n = snprintf(buf, 1, "abc");
	check(n == 3 && buf[0] == '\0' && buf[1] == 'Z',
	      "snprintf: size 1 writes only '\\0'");

	memset(buf, 'Z', sizeof(buf));
	n = snprintf(buf, 4, "%d", 123456);
	check(n == 6 && memcmp(buf, "123\0", 4) == 0,
	      "snprintf: truncates inside a number");
}

static void test_levels(void)
{
	check(printk_get_level(KERN_ERR "x") == '3', "printk_get_level: KERN_ERR");
	check(printk_get_level(KERN_DEBUG "x") == '7', "printk_get_level: KERN_DEBUG");
	check(printk_get_level("x") == 0, "printk_get_level: no prefix");
	check(printk_get_level("\001") == 0, "printk_get_level: SOH without digit");
	check(printk_get_level("\001x") == 0, "printk_get_level: SOH + non-digit");

	check(strcmp(printk_skip_level(KERN_INFO "msg"), "msg") == 0,
	      "printk_skip_level: strips prefix");
	check(strcmp(printk_skip_level("msg"), "msg") == 0,
	      "printk_skip_level: no prefix, unchanged");
}

static void test_warn_on(void)
{
	int calls = 0;

	check(WARN_ON(0) == 0, "WARN_ON(0) == 0");
	check(WARN_ON(calls++ == 1) == 0 && calls == 1,
	      "WARN_ON: evaluates cond exactly once (false)");

	test_puts("  (a WARNING line below is expected)\n");
	check(WARN_ON(calls++ == 1) == 1 && calls == 2,
	      "WARN_ON: evaluates cond exactly once (true)");
	check(WARN_ON(5) == 1, "WARN_ON(5) == 1");
}

void test_printk(void)
{
	test_basic();
	test_integers();
	test_hex();
	test_width();
	test_char_string();
	test_pointer();
	test_mixed();
	test_truncation();
	test_levels();
	test_warn_on();
}
