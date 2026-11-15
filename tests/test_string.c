/*
 * lib/string.c 的启动自测（对照 Linux 的 lib/test_string.c）。
 *
 * 注意：准备测试数据时故意不用 `char buf[] = "..."` 或普通循环，
 * 因为 GCC 可能把它们编译成对 memcpy/memset 的调用——
 * 而那正是被测的函数，还没写好时会把测试数据本身弄坏。
 * 用 volatile 指针逐字节读写，GCC 就不会做这种替换。
 */
#include <minux/string.h>
#include "test.h"

/* 把 src 的 n 个字节写进 dst（不经过 memcpy） */
static void fill(char *dst, const char *src, size_t n)
{
	volatile char *d = dst;

	for (size_t i = 0; i < n; i++)
		d[i] = src[i];
}

/* 比较 a 与 b 的前 n 个字节（不经过 memcmp） */
static bool same(const char *a, const char *b, size_t n)
{
	const volatile char *p = a;

	for (size_t i = 0; i < n; i++)
		if (p[i] != b[i])
			return false;
	return true;
}

static void test_memset(void)
{
	char buf[8];

	fill(buf, "........", 8);
	check(memset(buf + 2, 'x', 4) == buf + 2, "memset: return value");
	check(same(buf, "..xxxx..", 8), "memset: fills exactly n bytes");

	memset(buf, 0x1ab, 1);	/* 只取低 8 位：0xab */
	check((unsigned char)buf[0] == 0xab, "memset: uses (unsigned char)c");
}

static void test_memcpy(void)
{
	char buf[8];

	fill(buf, "........", 8);
	check(memcpy(buf + 1, "hello", 5) == buf + 1, "memcpy: return value");
	check(same(buf, ".hello..", 8), "memcpy: copies exactly n bytes");
}

static void test_memmove(void)
{
	char buf[9];

	/* dest > src：区域重叠，需要从后往前拷 */
	fill(buf, "123456789", 9);
	check(memmove(buf + 2, buf, 5) == buf + 2, "memmove: return value");
	check(same(buf, "121234589", 9), "memmove: overlap, dest > src");

	/* dest < src：区域重叠，需要从前往后拷 */
	fill(buf, "123456789", 9);
	memmove(buf, buf + 2, 5);
	check(same(buf, "345676789", 9), "memmove: overlap, dest < src");

	/* 不重叠时和 memcpy 一样 */
	fill(buf, "abcd-----", 9);
	memmove(buf + 5, buf, 4);
	check(same(buf, "abcd-abcd", 9), "memmove: no overlap");
}

static void test_memcmp(void)
{
	check(memcmp("abc", "abc", 3) == 0, "memcmp: equal");
	check(memcmp("abc", "abd", 3) < 0, "memcmp: less");
	check(memcmp("abd", "abc", 3) > 0, "memcmp: greater");
	check(memcmp("abc", "abd", 2) == 0, "memcmp: only first n bytes");
	check(memcmp("a", "b", 0) == 0, "memcmp: n == 0");
	check(memcmp("\x80", "\x01", 1) > 0, "memcmp: compares as unsigned char");
}

static void test_strlen(void)
{
	check(strlen("") == 0, "strlen: empty string");
	check(strlen("hello") == 5, "strlen: \"hello\"");
}

static void test_strcmp(void)
{
	check(strcmp("abc", "abc") == 0, "strcmp: equal");
	check(strcmp("abc", "abd") < 0, "strcmp: less");
	check(strcmp("abd", "abc") > 0, "strcmp: greater");
	check(strcmp("ab", "abc") < 0, "strcmp: prefix is smaller");
	check(strcmp("abc", "ab") > 0, "strcmp: longer is greater");
	check(strcmp("\x80", "\x01") > 0, "strcmp: compares as unsigned char");

	check(strncmp("abcX", "abcY", 3) == 0, "strncmp: equal in first n");
	check(strncmp("abcX", "abcY", 4) < 0, "strncmp: differs at n-th");
	check(strncmp("ab", "ab", 10) == 0, "strncmp: stops at '\\0'");
	check(strncmp("a", "b", 0) == 0, "strncmp: n == 0");
}

static void test_strcpy(void)
{
	char buf[8];

	fill(buf, "xxxxxxxx", 8);
	check(strcpy(buf, "abc") == buf, "strcpy: return value");
	check(same(buf, "abc\0xxxx", 8), "strcpy: copies the '\\0'");

	/* src 短于 n：剩余部分补 '\0' */
	fill(buf, "xxxxxxxx", 8);
	check(strncpy(buf, "ab", 6) == buf, "strncpy: return value");
	check(same(buf, "ab\0\0\0\0xx", 8), "strncpy: pads with '\\0' up to n");

	/* src 不短于 n：只写 n 个字节，不加 '\0' */
	fill(buf, "xxxxxxxx", 8);
	strncpy(buf, "abcdef", 3);
	check(same(buf, "abcxxxxx", 8), "strncpy: no '\\0' when src is long");
}

void test_string(void)
{
	test_memset();
	test_memcpy();
	test_memmove();
	test_memcmp();
	test_strlen();
	test_strcmp();
	test_strcpy();
}
