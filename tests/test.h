/*
 * 内核自测框架（只在 `make test` 时编译）。
 *
 * 新增一组测试的步骤：
 *   1. 新建 tests/test_xxx.c，实现 void test_xxx(void)，里面用 check() 断言
 *   2. 在下面的“测试套件”列表里声明 test_xxx
 *   3. 在 tests/runner.c 的 suites[] 里加一行
 */
#ifndef _TESTS_TEST_H
#define _TESTS_TEST_H

#include <minux/types.h>

/* 记录一条断言：ok 为假时打印 [FAIL] what */
void check(bool ok, const char *what);

/* 临时输出函数：还没有 printk，先直接走 SBI。有了 printk 后换掉 */
void test_puts(const char *s);

/* ---- 测试套件 ---- */
void test_string(void);
void test_list(void);
void test_printk(void);
void test_fdt(void);
void test_memblock(void);
void test_buddy(void);
void test_slab(void);
void test_sv39(void);
void test_mm(void);

#endif /* _TESTS_TEST_H */
