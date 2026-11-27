#ifndef _ASM_RISCV_SELFTEST_H
#define _ASM_RISCV_SELFTEST_H

void test_sv39(void);

/* 插入启动自测阶段，位于 slab 和通用 mm 测试之间。 */
#define ARCH_EARLY_TEST_SUITES { "sv39", test_sv39 },

#endif
