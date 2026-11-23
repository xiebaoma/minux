#include <minux/buddy.h>
#include <minux/irq.h>
#include <minux/mm.h>
#include <minux/page.h>
#include <minux/selftest.h>
#include <minux/string.h>
#include <minux/task.h>
#include "test.h"
#include "user_wait_probe.h"

#define WAIT_PROBE_CODE      0x10000UL
#define WAIT_PROBE_STACK     (WAIT_PROBE_CODE + PAGE_SIZE)
#define WAIT_PROBE_STACK_TOP (WAIT_PROBE_STACK + PAGE_SIZE)

extern const char wait_probe_start[], wait_probe_end[];
extern const char wait_probe_parent_first[], wait_probe_child_first[];
extern const char wait_probe_parent_fault[], wait_probe_parent_orphans[];
extern const char wait_probe_child[], wait_probe_fault[];

static struct {
	unsigned int blocked_parents;
	unsigned int runnable_parents;
	unsigned int orphans;
	bool parent_done;
} observed;

static struct task_struct *create_wait_probe(const char *entry)
{
	const size_t code_size = (uintptr_t)wait_probe_end - (uintptr_t)wait_probe_start;
	const uintptr_t pc = WAIT_PROBE_CODE + ((uintptr_t)entry - (uintptr_t)wait_probe_start);
	struct mm_struct *mm = mm_alloc();
	struct task_struct *task;
	struct page *code;

	if (!mm)
		return NULL;
	if (code_size > PAGE_SIZE ||
	    !mm_add_vma(mm, WAIT_PROBE_CODE, WAIT_PROBE_STACK, VM_READ | VM_EXEC) ||
	    !mm_add_vma(mm, WAIT_PROBE_STACK, WAIT_PROBE_STACK_TOP, VM_READ | VM_WRITE))
		goto fail;
	code = mm_populate_page(mm, WAIT_PROBE_CODE);
	if (!code || !mm_populate_page(mm, WAIT_PROBE_STACK))
		goto fail;
	memcpy(page_address(code), wait_probe_start, code_size);
	task = task_create_user(mm, pc, WAIT_PROBE_STACK_TOP);
	if (task)
		return task;
fail:
	mm_destroy(mm);
	return NULL;
}

static long spawn_child(const char *entry, bool run_first)
{
	struct task_struct *child = create_wait_probe(entry);
	int pid;

	if (!child)
		return -1;
	pid = child->pid;
	if (run_first) {
		u64 free_before_exit = buddy_free_pages();

		/* 启动自测尚未开定时器，主动切换保证这个子进程先退出。 */
		schedule();
		check(child->state == TASK_ZOMBIE && !child->stack_page &&
		      !child->mm && !child->context &&
		      buddy_free_pages() >= free_before_exit + (1UL << TASK_STACK_ORDER),
		      "wait4: exited child releases execution resources before it is reaped");
	}
	return pid;
}

long selftest_user_wait(unsigned long action)
{
	struct task_struct *parent = task_current()->parent;

	switch (action) {
	case WAIT_PROBE_SPAWN:
		return spawn_child(wait_probe_child, false);
	case WAIT_PROBE_SPAWN_EXITED:
		return spawn_child(wait_probe_child, true);
	case WAIT_PROBE_SPAWN_FAULT:
		return spawn_child(wait_probe_fault, false);
	case WAIT_PROBE_CHILD_STARTED:
		if (!parent)
			observed.orphans++;
		else if (parent->state == TASK_BLOCKED)
			observed.blocked_parents++;
		else if (parent->state == TASK_RUNNABLE)
			observed.runnable_parents++;
		return 0;
	case WAIT_PROBE_PARENT_DONE:
		observed.parent_done = true;
		return 0;
	default:
		return -1;
	}
}

void test_user_wait(void)
{
	static const struct {
		const char *entry;
		unsigned int blocked_parents;
		unsigned int runnable_parents;
		unsigned int orphans;
		const char *description;
	} cases[] = {
		{ wait_probe_parent_first, 1, 0, 0,
		  "wait4: parent blocks before child exits and wakes with its PID and status" },
		{ wait_probe_child_first, 0, 3, 0,
		  "wait4: reap already exited children and handle a bad status address" },
		{ wait_probe_parent_fault, 0, 0, 0,
		  "wait4: parent observes its child's exceptional exit status" },
		{ wait_probe_parent_orphans, 0, 1, 1,
		  "exit: release zombie children and let live orphans exit independently" },
	};
	unsigned long flags = arch_local_irq_save();
	struct task_struct *boot = task_current();
	u64 free_before = buddy_free_pages();

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		struct task_struct *parent;

		memset(&observed, 0, sizeof(observed));
		parent = create_wait_probe(cases[i].entry);
		check(parent != NULL, "wait4: create the user parent probe");
		if (parent) {
			task_idle_once();
			/* idle 可能在回收子进程时才唤醒父进程。 */
			schedule();
		}
		check(observed.parent_done &&
		      observed.blocked_parents == cases[i].blocked_parents &&
		      observed.runnable_parents == cases[i].runnable_parents &&
		      observed.orphans == cases[i].orphans,
		      cases[i].description);
		check(task_current() == boot && buddy_free_pages() == free_before,
		      "wait4: all process records, address spaces and stacks are reclaimed");
	}
	arch_local_irq_restore(flags);
}
