/*
 * 编译器属性的简写。对照 Linux 的 include/linux/compiler_attributes.h、compiler.h。
 */
#ifndef _MINUX_COMPILER_H
#define _MINUX_COMPILER_H

/*
 * 让 GCC 像检查 printf 一样检查格式串：第 a 个参数是格式串，
 * 可变参数从第 b 个开始。写错 %d/%s 之类会在编译期报警告。
 */
#define __printf(a, b)		__attribute__((format(printf, a, b)))

/*
 * 函数不会返回（panic、关机等）。编译器据此：不在调用之后生成多余代码；
 * 调用方不会再误报“函数末尾没有 return”；如果这个函数本身可能返回，会报警告。
 */
#define __noreturn		__attribute__((noreturn))

/* 分支预测提示：告诉编译器这个条件大概率为真/假 */
#define likely(x)		__builtin_expect(!!(x), 1)
#define unlikely(x)		__builtin_expect(!!(x), 0)

#endif /* _MINUX_COMPILER_H */
