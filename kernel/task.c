#include <minux/buddy.h>
#include <minux/bug.h>
#include <minux/page.h>
#include <minux/slab.h>
#include <minux/switch_to.h>
#include <minux/task.h>

static struct task_struct boot_task;
static struct task_struct *current_task;

void task_init(void)
{
	boot_task.state = TASK_RUNNING;
	current_task = &boot_task;
}

struct task_struct *task_current(void)
{
	return current_task;
}

static void task_start(void) __noreturn;

static void task_start(void)
{
	struct task_struct *task = task_current();

	task->entry(task->arg);
	panic("kernel thread returned from its entry function");
}

struct task_struct *task_create(void (*entry)(void *), void *arg)
{
	struct task_struct *task;
	char *stack;

	if (!entry)
		return NULL;
	task = kmalloc(sizeof(*task));
	if (!task)
		return NULL;
	task->stack_page = alloc_pages(TASK_STACK_ORDER);
	if (!task->stack_page) {
		kfree(task);
		return NULL;
	}

	stack = page_address(task->stack_page);
	task->context = arch_init_thread(stack + (PAGE_SIZE << TASK_STACK_ORDER),
					 task_start);
	task->state = TASK_RUNNABLE;
	task->entry = entry;
	task->arg = arg;
	return task;
}

bool task_destroy(struct task_struct *task)
{
	if (!task || task == current_task || task == &boot_task)
		return false;
	BUG_ON(!free_pages(task->stack_page, TASK_STACK_ORDER));
	kfree(task);
	return true;
}

void task_switch(struct task_struct *next)
{
	struct task_struct *prev = current_task;

	BUG_ON(!prev || !next);
	if (prev == next)
		return;
	BUG_ON(next->state != TASK_RUNNABLE || !next->context);

	prev->state = TASK_RUNNABLE;
	next->state = TASK_RUNNING;
	current_task = next;
	arch_switch_to(&prev->context, next->context);
}
