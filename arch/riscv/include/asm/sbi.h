/*
 * SBI（Supervisor Binary Interface）：S 模式内核向 M 模式固件（OpenSBI）请求服务的接口。
 * 调用方式：a7 = 扩展号 EID，a6 = 功能号 FID，a0~a5 = 参数，然后执行 ecall。
 */
#ifndef _ASM_RISCV_SBI_H
#define _ASM_RISCV_SBI_H

#include <minux/types.h>

/* 旧版（v0.1）控制台输出，最简单，够早期调试用 */
#define SBI_EXT_0_1_CONSOLE_PUTCHAR 0x01

/* TIME 扩展：参数是 time 计数器的绝对截止值。 */
#define SBI_EXT_TIME                0x54494D45  /* "TIME" */
#define SBI_TIME_SET_TIMER          0

/* System Reset 扩展 */
#define SBI_EXT_SRST                0x53525354  /* "SRST" */
#define SBI_SRST_RESET              0
#define SBI_SRST_TYPE_SHUTDOWN      0
#define SBI_SRST_REASON_NONE        0

void sbi_console_putchar(int ch);
long sbi_set_timer(u64 deadline);
void sbi_shutdown(void) __attribute__((noreturn));

#endif /* _ASM_RISCV_SBI_H */
