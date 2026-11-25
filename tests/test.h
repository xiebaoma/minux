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

/* 用物理页本身记录分配，以便在真实内存耗尽条件下测试回滚。 */
struct test_held_page;
struct test_held_page *test_hold_all_but(u64 keep);
void test_release_held(struct test_held_page *held);

/* ---- 测试套件 ---- */
void test_string(void);
void test_list(void);
void test_spinlock(void);
void test_printk(void);
void test_fdt(void);
void test_memblock(void);
void test_buddy(void);
void test_slab(void);
void test_sv39(void);
void test_mm(void);
void test_elf(void);
void test_memory_block(void);
void test_virtio_block(void);
void test_rootfs(void);
void test_initramfs(void);
void test_task(void);
void test_sched(void);
void test_preempt(void);
void test_user_preempt(void);
void test_user_fault(void);
void test_user_wait(void);

#endif /* _TESTS_TEST_H */
