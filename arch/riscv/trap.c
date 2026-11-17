#include <asm/trap.h>
#include <asm/timer.h>
#include <minux/panic.h>

#define SCAUSE_INTERRUPT (1UL << (sizeof(unsigned long) * 8 - 1))
#define SSTATUS_SIE      (1UL << 1)
#define SCAUSE_TIMER     5

extern void riscv_trap_entry(void);

void trap_init(void)
{
	/* stvec 低两位为 0：所有 trap 都从同一个入口开始。 */
	asm volatile("csrw stvec, %0" : : "r"(riscv_trap_entry) : "memory");
}

void trap_enable_interrupts(void)
{
	/* 各来源在 sie 单独开启；此处打开 S 模式的总开关。 */
	asm volatile("csrs sstatus, %0" : : "r"(SSTATUS_SIE) : "memory");
}

void riscv_trap_handler(struct pt_regs *regs)
{
	unsigned long code = regs->cause & ~SCAUSE_INTERRUPT;

	if ((regs->cause & SCAUSE_INTERRUPT) && code == SCAUSE_TIMER) {
		timer_handle_interrupt();
		return;
	}

	/* 未处理的异常或中断保留完整现场供排查。 */
	panic("unhandled %s: cause=%lu sepc=0x%lx stval=0x%lx sstatus=0x%lx",
	      regs->cause & SCAUSE_INTERRUPT ? "interrupt" : "exception",
	      code, regs->epc, regs->tval, regs->status);
}
