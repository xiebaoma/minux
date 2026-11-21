#include <minux/buddy.h>
#include <minux/page.h>
#include <minux/task.h>
#include "test.h"

struct switch_probe {
	struct task_struct *task;
	struct task_struct *next;
	struct task_struct *boot;
	unsigned int *progress;
	unsigned int turn;
};

static void probe_entry(void *arg)
{
	struct switch_probe *probe = arg;
	unsigned long expected = 0x5a5a + probe->turn;
	volatile unsigned long on_stack = expected;
	uintptr_t base = (uintptr_t)page_address(probe->task->stack_page);
	register unsigned long saved_low asm("s1") = expected;
	register unsigned long saved_high asm("s11") = ~expected;

	check(task_current() == probe->task && probe->task->state == TASK_RUNNING &&
	      probe->boot->state == TASK_RUNNABLE,
	      "task: the first entry sees the new task running");
	check((uintptr_t)&on_stack - base < (PAGE_SIZE << TASK_STACK_ORDER),
	      "task: the entry runs on its own kernel stack");
	check(!task_destroy(probe->task) && !task_destroy(probe->boot),
	      "task: cannot destroy the running task or the suspended boot task");
	check(*probe->progress == probe->turn,
	      "task: first entry receives its argument and runs in switch order");
	(*probe->progress)++;

	/* 强制 GCC 在切换两侧使用指定的 RISC-V 被调用者保存寄存器。 */
	asm volatile("" : "+r"(saved_low), "+r"(saved_high));
	task_switch(probe->next);
	asm volatile("" : "+r"(saved_low), "+r"(saved_high));

	check(task_current() == probe->task && probe->task->state == TASK_RUNNING &&
	      *probe->progress == probe->turn + 2,
	      "task: resumes after its original switch call");
	check(on_stack == expected && saved_low == expected && saved_high == ~expected,
	      "task: restores stack contents and callee-saved registers");
	(*probe->progress)++;
	task_switch(probe->next);
}

void test_task(void)
{
	struct task_struct *boot = task_current();
	unsigned int progress = 0;
	u64 free_before = buddy_free_pages();
	struct switch_probe first = { .boot = boot, .progress = &progress, .turn = 0 };
	struct switch_probe second = { .boot = boot, .progress = &progress, .turn = 1 };

	check(boot && boot->state == TASK_RUNNING && !boot->stack_page,
	      "task: boot uses the existing stack");
	if (!boot)
		return;
	task_switch(boot);
	check(task_current() == boot && boot->state == TASK_RUNNING,
	      "task: switching to self leaves the boot context running");
	check(!task_create(NULL, NULL) && buddy_free_pages() == free_before,
	      "task: rejects a missing entry before allocating");

	first.task = task_create(probe_entry, &first);
	second.task = task_create(probe_entry, &second);
	check(first.task && second.task, "task: allocate two kernel tasks");
	if (first.task && second.task) {
		first.next = second.task;
		second.next = boot;
		check(first.task->state == TASK_RUNNABLE && second.task->state == TASK_RUNNABLE &&
		      first.task->stack_page != second.task->stack_page && progress == 0,
		      "task: new tasks start suspended with distinct stacks");
		task_switch(first.task);
		check(task_current() == boot && boot->state == TASK_RUNNING && progress == 2,
		      "task: both new tasks run before returning to boot");
		task_switch(first.task);
		check(task_current() == boot && boot->state == TASK_RUNNING && progress == 4 &&
		      first.task->state == TASK_RUNNABLE && second.task->state == TASK_RUNNABLE,
		      "task: both tasks resume and switch back to boot again");
	}
	if (first.task)
		check(task_destroy(first.task), "task: release the first suspended context");
	if (second.task)
		check(task_destroy(second.task), "task: release the second suspended context");
	check(buddy_free_pages() == free_before, "task: release all allocated task pages");
}
