/*
 * 停机与关机。对照 Linux 的 include/linux/reboot.h，实现在 arch/riscv/reset.c。
 */
#ifndef _MINUX_REBOOT_H
#define _MINUX_REBOOT_H

#include <minux/compiler.h>

void machine_halt(void) __noreturn;		/* 停在原地，不关电（QEMU 不会退出） */
void machine_power_off(void) __noreturn;	/* 关机（QEMU 会退出） */

#endif /* _MINUX_REBOOT_H */
