/*
 * 停机与关机。对照 Linux 的 arch/riscv/kernel/reset.c。
 */
#include <minux/reboot.h>
#include <asm/sbi.h>

void machine_halt(void)
{
	/* wfi：等待中断。现在还没打开中断，所以会一直停在这里 */
	for (;;)
		asm volatile("wfi");
}

void machine_power_off(void)
{
	sbi_shutdown();
}
