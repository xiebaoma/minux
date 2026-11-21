#include <asm/plic.h>
#include <asm/trap.h>
#include <asm/timer.h>
#include <asm/uart.h>
#include <minux/irq.h>
#include <minux/panic.h>
#include <minux/task.h>

#define SCAUSE_INTERRUPT (1UL << (sizeof(unsigned long) * 8 - 1))
#define SSTATUS_SIE      (1UL << 1)
#define SCAUSE_TIMER     5
#define SCAUSE_EXTERNAL  9

extern void riscv_trap_entry(void);

void trap_init(void)
{
	/* stvec 低两位为 0：所有 trap 都从同一个入口开始。 */
	asm volatile("csrw stvec, %0" : : "r"(riscv_trap_entry) : "memory");
}

void arch_local_irq_enable(void)
{
	/* 各来源在 sie 单独开启；此处打开 S 模式的总开关。 */
	asm volatile("csrs sstatus, %0" : : "r"(SSTATUS_SIE) : "memory");
}

unsigned long arch_local_irq_save(void)
{
	unsigned long status;

	asm volatile("csrrc %0, sstatus, %1"
		     : "=r"(status) : "r"(SSTATUS_SIE) : "memory");
	return status & SSTATUS_SIE;
}

void arch_local_irq_restore(unsigned long flags)
{
	if (flags & SSTATUS_SIE)
		arch_local_irq_enable();
	else
		asm volatile("csrc sstatus, %0" : : "r"(SSTATUS_SIE) : "memory");
}

void arch_wait_for_interrupt(void)
{
	/* WFI 可由已启用且待处理的中断唤醒，此时 sstatus.SIE 可为零。 */
	asm volatile("wfi" : : : "memory");
}

void riscv_trap_handler(struct pt_regs *regs)
{
	unsigned long code = regs->cause & ~SCAUSE_INTERRUPT;

	if ((regs->cause & SCAUSE_INTERRUPT) && code == SCAUSE_TIMER) {
		timer_handle_interrupt();
		/* 定时器已重装，当前任务的完整 trap 现场仍保存在其栈上。 */
		schedule();
		return;
	}
	if ((regs->cause & SCAUSE_INTERRUPT) && code == SCAUSE_EXTERNAL) {
		u32 irq;

		/* PLIC 可能同时排队多个来源，逐个认领并完成。 */
		while ((irq = plic_claim()) != 0) {
			if (irq != uart_irq_number())
				panic("unhandled PLIC IRQ: %u", irq);
			uart_handle_interrupt();
			plic_complete(irq);
		}
		return;
	}

	/* 未处理的异常或中断保留完整现场供排查。 */
	panic("unhandled %s: cause=%lu sepc=0x%lx stval=0x%lx sstatus=0x%lx",
	      regs->cause & SCAUSE_INTERRUPT ? "interrupt" : "exception",
	      code, regs->epc, regs->tval, regs->status);
}
