#include <minux/timer.h>
#include <minux/buddy.h>
#include <minux/errno.h>
#include <minux/irq.h>
#include <minux/mm.h>
#include <minux/page.h>
#include <minux/string.h>
#include <minux/task.h>
#include "test.h"

#define USER_CODE      0x10000UL
#define USER_STACK     (USER_CODE + PAGE_SIZE)
#define USER_STACK_TOP (USER_STACK + PAGE_SIZE)

extern const char user_probe_start[], user_probe_end[];

/* 与 user_probe.S 在用户栈顶预留的区域对应。 */
struct user_probe_stack {
	unsigned long seed;
	volatile unsigned long request;
	volatile unsigned long reply;
	volatile long pid;
	volatile long unknown_syscall;
	unsigned long padding;
};

struct user_worker {
	struct task_struct *task;
	struct user_probe_stack *stack;
};

struct supervisor_context {
	unsigned long satp;
	unsigned long gp;
	unsigned long tp;
};

struct user_preempt_probe {
	struct user_worker workers[2];
	struct supervisor_context kernel;
};

static struct supervisor_context read_supervisor_context(void)
{
	struct supervisor_context context;

	asm volatile("csrr %0, satp\n\tmv %1, gp\n\tmv %2, tp"
		     : "=r"(context.satp), "=r"(context.gp), "=r"(context.tp));
	return context;
}

static bool create_user_worker(struct user_worker *worker, unsigned long seed)
{
	const size_t code_size = (uintptr_t)user_probe_end - (uintptr_t)user_probe_start;
	struct mm_struct *mm = mm_alloc();
	struct page *code, *stack_page;
	struct user_probe_stack *stack;

	if (!mm)
		return false;
	if (code_size > PAGE_SIZE ||
	    !mm_add_vma(mm, USER_CODE, USER_STACK, VM_READ | VM_EXEC) ||
	    !mm_add_vma(mm, USER_STACK, USER_STACK_TOP, VM_READ | VM_WRITE))
		goto fail;
	code = mm_populate_page(mm, USER_CODE);
	stack_page = mm_populate_page(mm, USER_STACK);
	if (!code || !stack_page)
		goto fail;

	memcpy(page_address(code), user_probe_start, code_size);
	stack = (struct user_probe_stack *)((char *)page_address(stack_page) +
					   PAGE_SIZE) - 1;
	stack->seed = seed;
	stack->request = 1;
	worker->task = task_create_user(mm, USER_CODE, USER_STACK_TOP);
	if (!worker->task)
		goto fail;
	worker->stack = stack;
	return true;

fail:
	mm_destroy(mm);
	return false;
}

static bool wait_for_user_replies(const struct user_preempt_probe *probe, unsigned long round)
{
	const struct user_probe_stack *first = probe->workers[0].stack;
	const struct user_probe_stack *second = probe->workers[1].stack;
	unsigned long start = timer_ticks();

	for (;;) {
		bool replied = first->reply == 3 * first->seed + round &&
			       second->reply == 3 * second->seed + round;

		if (replied || timer_ticks() - start >= 6)
			return replied;
		schedule();
	}
}

static void observe_user_workers(void *arg)
{
	struct user_preempt_probe *probe = arg;
	struct supervisor_context context;
	unsigned long start;
	bool resumed;

	arch_local_irq_save();
	check(wait_for_user_replies(probe, 1),
	      "user: both tasks read and write their own page at the same VA");
	check(probe->workers[0].stack->pid == probe->workers[0].task->pid &&
	      probe->workers[1].stack->pid == probe->workers[1].task->pid &&
	      probe->workers[0].stack->pid > 0 && probe->workers[1].stack->pid > 0 &&
	      probe->workers[0].stack->pid != probe->workers[1].stack->pid,
	      "syscall: getpid returns the current user task's distinct PID");
	check(probe->workers[0].stack->unknown_syscall == -ENOSYS &&
	      probe->workers[1].stack->unknown_syscall == -ENOSYS,
	      "syscall: an unknown call returns -ENOSYS and execution continues");

	/* 两个 U 模式任务只靠定时器让出 CPU，并用寄存器累积第二次结果。 */
	start = timer_ticks();
	probe->workers[0].stack->request = 2;
	probe->workers[1].stack->request = 2;
	resumed = wait_for_user_replies(probe, 2);
	check(resumed && timer_ticks() - start >= 2,
	      "user: both tasks resume after timer preemption with registers intact");

	context = read_supervisor_context();
	check(context.satp == probe->kernel.satp && context.gp == probe->kernel.gp &&
	      context.tp == probe->kernel.tp,
	      "user: switching back restores the kernel page table and registers");
	check(task_destroy(probe->workers[0].task), "user: destroy first preempted task");
	check(task_destroy(probe->workers[1].task), "user: destroy second preempted task");
}

void test_user_preempt(void)
{
	unsigned long flags = arch_local_irq_save();
	u64 free_before = buddy_free_pages();
	struct user_preempt_probe probe = { .kernel = read_supervisor_context() };
	bool created = create_user_worker(&probe.workers[0], 100) &&
		       create_user_worker(&probe.workers[1], 200) &&
		       task_create(observe_user_workers, &probe);

	check(created, "user: create two user tasks and a kernel observer");
	if (created) {
		task_idle_once();
	} else {
		task_destroy(probe.workers[0].task);
		task_destroy(probe.workers[1].task);
	}
	check(buddy_free_pages() == free_before,
	      "user: reclaim both address spaces, kernel stacks and observer");
	arch_local_irq_restore(flags);
}
