/*
 * 内存与字符串操作。
 *
 * 原则：逐字节实现即可，先正确，后优化。
 * 写完后用 `make dump` 检查 memset/memcpy 的反汇编里没有调用它们自己。
 *
 * GCC 可能把这类循环识别成 memset/memcpy 调用，-fno-builtin 会关闭这个优化，make dump 可以验证。
 */
#include <minux/string.h>

void *memset(void *s, int c, size_t n)
{
	unsigned char *p = s;

	while (n--)
		*p++ = (unsigned char)c;
	return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
	unsigned char *d = dest;
	const unsigned char *s = src;

	while (n--)
		*d++ = *s++;
	return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
	unsigned char *d = dest;
	const unsigned char *s = src;

	if (d == s || n == 0)
		return dest;

	/* 转成整数再比较：对不属于同一对象的指针直接用 < 是未定义行为 */
	if ((uintptr_t)d < (uintptr_t)s) {
		/*
		 * dest 在 src 前面：从前往后拷。
		 * 写 d[i] 时，src 里 i 及之前的字节已经读完，覆盖不到还没读的部分。
		 */
		while (n--)
			*d++ = *s++;
	} else {
		/*
		 * dest 在 src 后面：从后往前拷。
		 * 如果从前往后，写 d[0] 可能会覆盖 s[k]（k > 0）这个还没读的字节。
		 */
		d += n;
		s += n;
		while (n--)
			*--d = *--s;
	}
	return dest;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
	const unsigned char *a = s1;
	const unsigned char *b = s2;

	for (; n; n--, a++, b++) {
		if (*a != *b)
			return (int)*a - (int)*b;
	}
	return 0;
}

size_t strlen(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	return (size_t)(p - s);
}

int strcmp(const char *s1, const char *s2)
{
	const unsigned char *a = (const unsigned char *)s1;
	const unsigned char *b = (const unsigned char *)s2;

	/* 循环继续时，*a 非零且 *a == *b，所以 *b 也必然非零，因此循环条件里不需要再检查 *b */
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (int)*a - (int)*b;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
	const unsigned char *a = (const unsigned char *)s1;
	const unsigned char *b = (const unsigned char *)s2;

	for (; n; n--, a++, b++) {
		if (*a != *b)
			return (int)*a - (int)*b;
		if (*a == '\0')
			return 0;	/* 两边同时结束 */
	}
	return 0;
}

char *strcpy(char *dest, const char *src)
{
	char *d = dest;

	while ((*d++ = *src++) != '\0')
		;
	return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
	size_t i = 0;

	/* 先拷 src 的内容，最多 n 个；src 不短于 n 时这里就写满了，不补 '\0' */
	for (; i < n && src[i] != '\0'; i++)
		dest[i] = src[i];
	/* src 比 n 短：剩下的位置全部填 '\0' */
	for (; i < n; i++)
		dest[i] = '\0';
	return dest;
}