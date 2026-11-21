#include <minux/buddy.h>
#include <minux/bug.h>
#include <minux/irq.h>
#include <minux/page.h>
#include <minux/slab.h>
#include <minux/switch_to.h>
#include <minux/task.h>
#include <minux/wait.h>

static struct task_struct boot_task;
static struct task_struct *current_task;
/* 启动任务进入 idle 循环后，仅在没有其他任务可运行时接手。 */
static struct task_struct *idle_task;
/* 本地中断关闭时访问，按 FIFO 排列等待运行的普通任务。 */
static LIST_HEAD(run_queue);
/* 单 hart 上，接手的任务会在再次调度前回收这个已退出的任务。 */
static struct task_struct *dead_task;

void task_init(void)
{
	boot_task.state = TASK_RUNNING;
	INIT_LIST_HEAD(&boot_task.run_list);
	current_task = &boot_task;
}

struct task_struct *task_current(void)
{
	return current_task;
}

static void task_free(struct task_struct *task)
{
	BUG_ON(!free_pages(task->stack_page, TASK_STACK_ORDER));
	kfree(task);
}

static void finish_task_switch(void)
{
	struct task_struct *task = dead_task;

	if (!task)
		return;
	dead_task = NULL;
	task_free(task);
}

static void task_start(void) __noreturn;

static void task_start(void)
{
	struct task_struct *task = task_current();

	/* 新线程直接进入这里，没有挂起的 task_switch() 可以返回。 */
	finish_task_switch();
	arch_local_irq_enable();
	task->entry(task->arg);
	arch_local_irq_save();
	task->state = TASK_DEAD;
	schedule();
	/* 已退出的任务一旦切走便不会恢复执行。 */
	BUG();
}

struct task_struct *task_create(void (*entry)(void *), void *arg)
{
	struct task_struct *task;
	unsigned long flags;
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
	flags = arch_local_irq_save();
	list_add_tail(&task->run_list, &run_queue);
	arch_local_irq_restore(flags);
	return task;
}

bool task_destroy(struct task_struct *task)
{
	unsigned long flags;

	if (!task || task == &boot_task)
		return false;
	flags = arch_local_irq_save();
	if (task->state != TASK_RUNNABLE) {
		arch_local_irq_restore(flags);
		return false;
	}
	list_del(&task->run_list);
	task->state = TASK_DEAD;
	arch_local_irq_restore(flags);
	task_free(task);
	return true;
}

void task_switch(struct task_struct *next)
{
	struct task_struct *prev = current_task;

	BUG_ON(!prev || !next);
	if (prev == next)
		return;
	BUG_ON(next->state != TASK_RUNNABLE || !next->context);

	if (next != idle_task)
		list_del_init(&next->run_list);
	if (prev->state == TASK_DEAD) {
		dead_task = prev;
	} else if (prev->state == TASK_RUNNING) {
		prev->state = TASK_RUNNABLE;
		if (prev != idle_task)
			list_add_tail(&prev->run_list, &run_queue);
	}
	next->state = TASK_RUNNING;
	current_task = next;
	arch_switch_to(&prev->context, next->context);
	finish_task_switch();
}

static void schedule_irq_disabled(void)
{
	if (!list_empty(&run_queue)) {
		task_switch(list_first_entry(&run_queue, struct task_struct, run_list));
	} else if (current_task->state != TASK_RUNNING) {
		BUG_ON(!idle_task);
		task_switch(idle_task);
	}
}

void schedule(void)
{
	unsigned long flags = arch_local_irq_save();

	schedule_irq_disabled();
	arch_local_irq_restore(flags);
}

void task_idle_once(void)
{
	unsigned long flags = arch_local_irq_save();

	BUG_ON(current_task != &boot_task);
	idle_task = &boot_task;
	/* 关中断检查队列，待处理中断可以唤醒 CPU，恢复总开关后再处理。 */
	if (list_empty(&run_queue))
		arch_wait_for_interrupt();
	else
		schedule_irq_disabled();
	arch_local_irq_restore(flags);
}

void wait_event(struct wait_queue *queue, bool (*ready)(void *), void *arg)
{
	unsigned long flags = arch_local_irq_save();
	struct task_struct *task = current_task;

	while (!ready(arg)) {
		BUG_ON(!idle_task || task == idle_task);
		task->state = TASK_BLOCKED;
		list_add_tail(&task->run_list, &queue->tasks);
		schedule_irq_disabled();
	}
	arch_local_irq_restore(flags);
}

void wake_up_all(struct wait_queue *queue)
{
	unsigned long flags = arch_local_irq_save();
	struct task_struct *task, *next;

	list_for_each_entry_safe(task, next, &queue->tasks, run_list) {
		list_del(&task->run_list);
		task->state = TASK_RUNNABLE;
		list_add_tail(&task->run_list, &run_queue);
	}
	arch_local_irq_restore(flags);
}
