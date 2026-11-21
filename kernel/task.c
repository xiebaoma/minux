#include <minux/buddy.h>
#include <minux/bug.h>
#include <minux/page.h>
#include <minux/slab.h>
#include <minux/switch_to.h>
#include <minux/task.h>

static struct task_struct boot_task;
static struct task_struct *current_task;
/* 按 FIFO 排列可运行的任务，当前任务在切出时才加入队尾。 */
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
	task->entry(task->arg);
	task->state = TASK_DEAD;
	/* 启动任务一直可运行，退出时总能切到另一个任务。 */
	schedule();
	//末尾的 BUG() 用来检查一个约定：已退出的任务一旦切走，就永远不会恢复执行。如果这里的 schedule() 返回了，就说明退出路径出了错。
	BUG();
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
	list_add_tail(&task->run_list, &run_queue);
	return task;
}

bool task_destroy(struct task_struct *task)
{
	if (!task || task == &boot_task || task->state != TASK_RUNNABLE)
		return false;
	list_del(&task->run_list);
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

	list_del_init(&next->run_list);
	if (prev->state == TASK_DEAD) {
		dead_task = prev;
	} else {
		prev->state = TASK_RUNNABLE;
		list_add_tail(&prev->run_list, &run_queue);
	}
	next->state = TASK_RUNNING;
	current_task = next;
	arch_switch_to(&prev->context, next->context);
	finish_task_switch();
}

void schedule(void)
{
	if (list_empty(&run_queue))
		return;
	task_switch(list_first_entry(&run_queue, struct task_struct, run_list));
}
