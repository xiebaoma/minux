#include <asm/plic.h>
#include <asm/trap.h>
#include <asm/timer.h>
#include <asm/uart.h>
#include <minux/irq.h>
#include <minux/mm.h>
#include <minux/panic.h>
#include <minux/printk.h>
#include <minux/string.h>
#include <minux/switch_to.h>
#include <minux/task.h>

#define SCAUSE_INTERRUPT (1UL << (sizeof(unsigned long) * 8 - 1))
#define SCAUSE_TIMER     5
#define SCAUSE_EXTERNAL  9
#define SCAUSE_USER_ECALL 8
#define SCAUSE_INST_PAGE_FAULT  12
#define SCAUSE_LOAD_PAGE_FAULT  13
#define SCAUSE_STORE_PAGE_FAULT 15
#define SSTATUS_UXL_64   (2UL << 32)

extern void riscv_trap_entry(void);

void trap_init(void)
{
	/* 内核执行时 sscratch 为零。stvec 低两位为零，使用统一入口。 */
	asm volatile("csrw sscratch, zero\n\t"
		     "csrw stvec, %0" : : "r"(riscv_trap_entry) : "memory");
}

struct arch_thread_context *arch_init_user_thread(void *stack_top, void (*start)(void),
						 uintptr_t user_pc, uintptr_t user_sp)
{
	struct pt_regs *regs = (struct pt_regs *)((char *)stack_top - USER_FRAME_SIZE);

	memset(regs, 0, sizeof(*regs));
	regs->epc = user_pc;
	regs->x[2] = user_sp;
	/* SPP 为零，SPIE 置位，首次 sret 进入 U 模式。 */
	regs->status = SSTATUS_UXL_64 | SSTATUS_SPIE;
	return arch_init_thread(regs, start);
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

static void handle_user_exception(struct pt_regs *regs)
{
	struct task_struct *task = task_current();
	unsigned int access;

	switch (regs->cause) {
	case SCAUSE_USER_ECALL:
		riscv_handle_syscall(regs);
		return;
	case SCAUSE_INST_PAGE_FAULT:
		access = VM_EXEC;
		break;
	case SCAUSE_LOAD_PAGE_FAULT:
		access = VM_READ;
		break;
	case SCAUSE_STORE_PAGE_FAULT:
		access = VM_WRITE;
		break;
	default:
		access = 0;
		break;
	}

	if (access && mm_handle_user_fault(task->mm, regs->tval, access))
		return; /* 保持 sepc，重试触发缺页的指令。 */

	pr_warn("user task %d: exception %lu sepc=0x%lx stval=0x%lx\n",
		task->pid, regs->cause, regs->epc, regs->tval);
	task_exit_current();
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

	if (!(regs->cause & SCAUSE_INTERRUPT) && !(regs->status & SSTATUS_SPP)) {
		handle_user_exception(regs);
		return;
	}

	/* 未处理的异常或中断保留完整现场供排查。 */
	panic("unhandled %s: cause=%lu sepc=0x%lx stval=0x%lx sstatus=0x%lx",
	      regs->cause & SCAUSE_INTERRUPT ? "interrupt" : "exception",
	      code, regs->epc, regs->tval, regs->status);
}
