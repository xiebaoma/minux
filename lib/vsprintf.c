/*
 * 格式化输出的核心：vsnprintf。对照 Linux 的 lib/vsprintf.c。
 *
 * 支持的格式（printf 的一个子集，够内核用）：
 *
 *   %[标志][宽度][长度]转换
 *
 *   标志    '-'  左对齐（默认右对齐）
 *           '0'  用 '0' 而不是空格补齐宽度；负数时 '0' 补在负号之后："-0042"
 *   宽度    十进制数字，最少输出这么多字符；内容更长时不截断
 *   长度    'l' long，'ll' long long，'z' size_t；不写就是 int
 *   转换    d i   有符号十进制
 *           u     无符号十进制
 *           x X   无符号十六进制（小写 / 大写）
 *           c     字符
 *           s     字符串；参数为 NULL 时输出 "(null)"
 *           p     指针，固定输出 "0x" + 16 位十六进制，如 0x0000000080200000
 *           %     输出 '%' 本身
 *
 * 几点约定（标准里属于未定义行为的地方，这里选了确定的做法）：
 *   - '-' 和 '0' 同时出现时 '-' 优先，'0' 被忽略（与 C 标准一致）。
 *   - '0' 只对数字（d i u x X p）有效；%c、%s 总是用空格补齐。
 *   - 不认识的转换符：把整个 "%..." 原样输出（如 "%5q"），方便发现格式串写错。
 *   - 格式串在 '%' 之后意外结束（如 "abc%"、"%-5l"）：同样原样输出然后结束，
 *     绝不越过结尾的 '\0' 继续读。
 */
#include <minux/sprintf.h>

/* 标志位 */
#define FLAG_LEFT	0x1	/* '-' */
#define FLAG_ZERO	0x2	/* '0' */

/* 长度修饰 */
enum length {
	LEN_INT,	/* 无 */
	LEN_LONG,	/* l */
	LEN_LLONG,	/* ll */
	LEN_SIZE,	/* z */
};

/*
 * 宽度上限。没有它，"%99999999999d" 这种格式会让 int 溢出（未定义行为）。
 * 内核里不会有人真的需要这么宽，取一个足够大又远离 INT_MAX 的值。
 */
#define WIDTH_MAX	4096

/*
 * unsigned long long 最大是 2^64 - 1 = 18446744073709551615，20 位十进制；
 * 十六进制是 16 位。按最坏情况（十进制）开数组。
 */
#define NUM_BUF_SIZE	20

/* ------------------------------------------------------------------ */
/* 输出游标                                                           */
/* ------------------------------------------------------------------ */

struct out {
	char	*buf;
	size_t	size;	/* buf 的总容量，含 '\0' */
	size_t	len;	/* 本应输出的字符数（不管是否被截断） */
};

/*
 * 所有输出都经过这里。只在还有空间（并且给 '\0' 留了一个位置）时才真正写，
 * 但 len 每次都加 1，所以截断对其他代码完全透明。
 */
static void out_char(struct out *o, char c)
{
	if (o->len + 1 < o->size)
		o->buf[o->len] = c;
	o->len++;
}

static void out_repeat(struct out *o, char c, int n)
{
	while (n-- > 0)
		out_char(o, c);
}

static void out_mem(struct out *o, const char *s, int n)
{
	while (n-- > 0)
		out_char(o, *s++);
}

/* ------------------------------------------------------------------ */
/* 数字                                                               */
/* ------------------------------------------------------------------ */

/*
 * 把 num 按 base 进制转成字符，结果放进 tmp，返回位数。
 * 注意 tmp 里是倒序的：tmp[0] 是最低位。
 * num == 0 时也要输出一个 '0'，所以用 do-while。
 */
static int num_to_digits(char *tmp, unsigned long long num, unsigned int base,
			 bool upper)
{
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	int n = 0;

	do {
		tmp[n++] = digits[num % base];
		num /= base;
	} while (num != 0);

	return n;
}

/*
 * 输出一个数字，处理宽度和补齐。
 *
 *   prefix      数字前面的东西："-"（负数）、"0x"（指针）或 ""
 *   min_digits  最少输出多少位数字，不足在前面补 '0'（%p 用 16，其他用 1）
 *
 * 三种布局（W 表示补齐用的字符）：
 *   左对齐       prefix 000digits WWWW     W = ' '
 *   右对齐+'0'   prefix 0000000000digits   补齐的 '0' 在前缀之后
 *   右对齐       WWWW prefix 000digits     W = ' '
 */
static void out_number(struct out *o, unsigned long long num, unsigned int base,
		       bool upper, const char *prefix, int min_digits,
		       int width, unsigned int flags)
{
	char tmp[NUM_BUF_SIZE];
	int ndigits, nzeros, plen, content, pad;

	ndigits = num_to_digits(tmp, num, base, upper);
	nzeros = min_digits > ndigits ? min_digits - ndigits : 0;

	for (plen = 0; prefix[plen] != '\0'; plen++)
		;

	content = plen + nzeros + ndigits;
	pad = width > content ? width - content : 0;

	if (!(flags & FLAG_LEFT) && !(flags & FLAG_ZERO))
		out_repeat(o, ' ', pad);

	out_mem(o, prefix, plen);

	if (!(flags & FLAG_LEFT) && (flags & FLAG_ZERO))
		out_repeat(o, '0', pad);
	out_repeat(o, '0', nzeros);

	/* tmp 是倒序的，从高位往低位输出 */
	while (ndigits > 0)
		out_char(o, tmp[--ndigits]);

	if (flags & FLAG_LEFT)
		out_repeat(o, ' ', pad);
}

/*
 * 有符号数：拆成“符号 + 绝对值”再交给 out_number。
 *
 * 不能写 -val：val == LLONG_MIN 时它的相反数超出 long long 范围，是未定义行为。
 * 先转成 unsigned long long（有符号转无符号是按模 2^64 定义好的），
 * 再在无符号域里取负（无符号运算也是按模定义的）：
 *   (unsigned long long)LLONG_MIN == 2^63
 *   0 - 2^63 (mod 2^64)          == 2^63      正好就是 |LLONG_MIN|
 */
static void out_signed(struct out *o, long long val, int width,
		       unsigned int flags)
{
	unsigned long long abs = (unsigned long long)val;

	if (val < 0)
		abs = 0 - abs;

	out_number(o, abs, 10, false, val < 0 ? "-" : "", 1, width, flags);
}

/* ------------------------------------------------------------------ */
/* 字符串                                                             */
/* ------------------------------------------------------------------ */

/* 字符串和字符：宽度只用空格补齐 */
static void out_string(struct out *o, const char *s, int slen, int width,
		       unsigned int flags)
{
	int pad = width > slen ? width - slen : 0;

	if (!(flags & FLAG_LEFT))
		out_repeat(o, ' ', pad);
	out_mem(o, s, slen);
	if (flags & FLAG_LEFT)
		out_repeat(o, ' ', pad);
}

/* ------------------------------------------------------------------ */
/* 主循环                                                             */
/* ------------------------------------------------------------------ */

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	struct out o = { .buf = buf, .size = size, .len = 0 };

	while (*fmt != '\0') {
		const char *spec;
		unsigned int flags = 0;
		int width = 0;
		enum length len = LEN_INT;

		if (*fmt != '%') {
			out_char(&o, *fmt++);
			continue;
		}

		spec = fmt;	/* 指向 '%'，遇到不认识的转换时用 */
		fmt++;

		/* 1. 标志：可以重复、任意顺序，如 "%-0-5d" */
		for (;;) {
			if (*fmt == '-')
				flags |= FLAG_LEFT;
			else if (*fmt == '0')
				flags |= FLAG_ZERO;
			else
				break;
			fmt++;
		}
		/* '-' 优先：左对齐时右边只能补空格 */
		if (flags & FLAG_LEFT)
			flags &= ~FLAG_ZERO;

		/* 2. 宽度 */
		while (*fmt >= '0' && *fmt <= '9') {
			if (width < WIDTH_MAX)
				width = width * 10 + (*fmt - '0');
			fmt++;
		}
		if (width > WIDTH_MAX)
			width = WIDTH_MAX;

		/* 3. 长度 */
		if (*fmt == 'l') {
			fmt++;
			len = LEN_LONG;
			if (*fmt == 'l') {
				fmt++;
				len = LEN_LLONG;
			}
		} else if (*fmt == 'z') {
			fmt++;
			len = LEN_SIZE;
		}

		/*
		 * 4. 转换。va_arg 的类型必须和调用方实际传入的一致，
		 *    所以按长度修饰分别取，再统一扩展成 64 位处理。
		 */
		switch (*fmt) {
		case 'd':
		case 'i': {
			long long v;

			switch (len) {
			case LEN_LONG:	v = va_arg(args, long);		break;
			case LEN_LLONG:	v = va_arg(args, long long);	break;
			case LEN_SIZE:	v = va_arg(args, ssize_t);	break;
			default:	v = va_arg(args, int);		break;
			}
			out_signed(&o, v, width, flags);
			break;
		}

		case 'u':
		case 'x':
		case 'X': {
			unsigned long long v;

			switch (len) {
			case LEN_LONG:	v = va_arg(args, unsigned long);	break;
			case LEN_LLONG:	v = va_arg(args, unsigned long long);	break;
			case LEN_SIZE:	v = va_arg(args, size_t);		break;
			default:	v = va_arg(args, unsigned int);		break;
			}
			out_number(&o, v, *fmt == 'u' ? 10 : 16, *fmt == 'X',
				   "", 1, width, flags);
			break;
		}

		case 'p': {
			uintptr_t v = (uintptr_t)va_arg(args, void *);

			out_number(&o, v, 16, false, "0x",
				   (int)(sizeof(void *) * 2), width, flags);
			break;
		}

		case 'c': {
			/* char 经过可变参数会被提升成 int，必须按 int 取 */
			char c = (char)va_arg(args, int);

			out_string(&o, &c, 1, width, flags);
			break;
		}

		case 's': {
			const char *s = va_arg(args, const char *);
			int slen = 0;

			if (s == NULL)
				s = "(null)";
			while (s[slen] != '\0')
				slen++;
			out_string(&o, s, slen, width, flags);
			break;
		}

		case '%':
			out_char(&o, '%');
			break;

		case '\0':
			/* 格式串在说明符中途结束：原样输出后停下，不能 fmt++ 越过结尾 */
			out_mem(&o, spec, (int)(fmt - spec));
			continue;

		default:
			/* 不认识的转换：把 "%..." 原样输出，便于发现写错的格式串 */
			out_mem(&o, spec, (int)(fmt - spec + 1));
			break;
		}
		fmt++;
	}

	/* size > 0 时一定以 '\0' 结尾：没截断写在 len 处，截断了写在最后一格 */
	if (size > 0)
		buf[o.len < size ? o.len : size - 1] = '\0';

	return (int)o.len;
}

/*
 * snprintf 只是把 "..." 打包成 va_list 交给 vsnprintf。
 * 这是使用可变参数的标准写法，写 printk 和 panic 时可以照着来。
 */
int snprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list args;
	int n;

	va_start(args, fmt);
	n = vsnprintf(buf, size, fmt, args);
	va_end(args);
	return n;
}