#include <minux/buddy.h>
#include <minux/bug.h>
#include <minux/errno.h>
#include <minux/irq.h>
#include <minux/limits.h>
#include <minux/mm.h>
#include <minux/page.h>
#include <minux/paging.h>
#include <minux/slab.h>
#include <minux/switch_to.h>
#include <minux/task.h>
#include <minux/wait.h>

static struct task_struct boot_task;
static struct task_struct *current_task;
/* 启动任务进入 idle 循环后，仅在没有其他任务可运行时接手。 */
static struct task_struct *idle_task;
static unsigned int next_user_pid = 1;
/* 本地中断关闭时访问，按 FIFO 排列等待运行的普通任务。 */
static LIST_HEAD(run_queue);
/* 单 hart 上，接手的任务会在再次调度前回收这个已退出的任务。 */
static struct task_struct *dead_task;

static void init_task_lists(struct task_struct *task)
{
	INIT_LIST_HEAD(&task->run_list);
	INIT_LIST_HEAD(&task->children);
	INIT_LIST_HEAD(&task->sibling);
	init_wait_queue(&task->child_wait);
}

void task_init(void)
{
	boot_task.state = TASK_RUNNING;
	init_task_lists(&boot_task);
	current_task = &boot_task;
}

struct task_struct *task_current(void)
{
	return current_task;
}

static void *task_stack_top(const struct task_struct *task)
{
	char *base = page_address(task->stack_page);

	return base + (PAGE_SIZE << TASK_STACK_ORDER);
}

static void task_release_resources(struct task_struct *task)
{
	mm_destroy(task->mm);
	BUG_ON(!free_pages(task->stack_page, TASK_STACK_ORDER));
	task->mm = NULL;
	task->stack_page = NULL;
	task->context = NULL;
}

static void detach_child(struct task_struct *child)
{
	list_del_init(&child->sibling);
	child->parent = NULL;
}

static void detach_children(struct task_struct *parent)
{
	struct task_struct *child, *next;

	list_for_each_entry_safe(child, next, &parent->children, sibling) {
		detach_child(child);
		if (child->state == TASK_ZOMBIE)
			kfree(child);
	}
}

static void finish_task_switch(void)
{
	struct task_struct *task = dead_task;

	if (!task)
		return;
	dead_task = NULL;
	detach_children(task);
	task_release_resources(task);
	if (task->parent) {
		task->state = TASK_ZOMBIE;
		wake_up_all(&task->parent->child_wait);
	} else {
		kfree(task);
	}
}

void task_exit_current(int status)
{
	arch_local_irq_save();
	BUG_ON(current_task == &boot_task || current_task->state != TASK_RUNNING);
	current_task->exit_status = status;
	current_task->state = TASK_DEAD;
	schedule();
	BUG();
}

static void task_start(void) __noreturn;

static void task_start(void)
{
	struct task_struct *task = task_current();

	/* 新线程直接进入这里，没有挂起的 task_switch() 可以返回。 */
	finish_task_switch();
	if (task->mm)
		arch_enter_user(task_stack_top(task));

	arch_local_irq_enable();
	task->entry(task->arg);
	task_exit_current(0);
}

static struct task_struct *task_alloc(void)
{
	struct task_struct *task = kzalloc(sizeof(*task));

	if (!task)
		return NULL;
	task->stack_page = alloc_pages(TASK_STACK_ORDER);
	if (!task->stack_page) {
		kfree(task);
		return NULL;
	}

	init_task_lists(task);
	return task;
}

static void task_publish(struct task_struct *task)
{
	unsigned long flags = arch_local_irq_save();

	if (task->parent)
		list_add_tail(&task->sibling, &task->parent->children);
	task->state = TASK_RUNNABLE;
	list_add_tail(&task->run_list, &run_queue);
	arch_local_irq_restore(flags);
}

struct task_struct *task_create(void (*entry)(void *), void *arg)
{
	struct task_struct *task;

	if (!entry)
		return NULL;
	task = task_alloc();
	if (!task)
		return NULL;
	task->entry = entry;
	task->arg = arg;
	task->context = arch_init_thread(task_stack_top(task), task_start);
	task_publish(task);
	return task;
}

struct task_struct *task_create_user(struct mm_struct *mm, uintptr_t user_pc,
				     uintptr_t user_sp)
{
	struct task_struct *task;
	unsigned long flags;

	if (!mm)
		return NULL;
	task = task_alloc();
	if (!task)
		return NULL;

	flags = arch_local_irq_save();
	if (next_user_pid <= INT_MAX)
		task->pid = next_user_pid++;
	arch_local_irq_restore(flags);
	if (!task->pid) {
		task_release_resources(task);
		kfree(task);
		return NULL;
	}

	task->mm = mm;
	if (current_task->mm)
		task->parent = current_task;
	task->context = arch_init_user_thread(task_stack_top(task), task_start,
					     user_pc, user_sp);
	task_publish(task);
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
	detach_children(task);
	if (task->parent) {
		struct task_struct *parent = task->parent;

		detach_child(task);
		wake_up_all(&parent->child_wait);
	}
	arch_local_irq_restore(flags);
	task_release_resources(task);
	kfree(task);
	return true;
}

struct child_wait {
	struct task_struct *parent;		//要检查哪个父任务的子任务链表。
	struct task_struct *zombie;		//如果找到了已退出的子任务，记录它，供 task_wait_child() 回收。
	int pid;						//要等哪个子任务；`-1` 表示任意子任务。
	bool nohang;					//是否不阻塞，立即返回。
	bool matched;					//是否找到了符合 pid 的子任务。
};

static bool child_wait_ready(void *arg)
{
	struct child_wait *wait = arg;
	struct task_struct *child;

	wait->matched = false;
	list_for_each_entry(child, &wait->parent->children, sibling) {
		if (wait->pid != -1 && child->pid != wait->pid)
			continue;
		wait->matched = true;
		if (child->state == TASK_ZOMBIE) {
			wait->zombie = child;
			return true;
		}
	}
	return !wait->matched || wait->nohang;
}

//让当前任务等待并回收自己的子任务
int task_wait_child(int pid, bool nohang, int *status)
{
	unsigned long flags = arch_local_irq_save();
	struct child_wait wait = { .parent = current_task, .pid = pid, .nohang = nohang };
	int result;

	wait_event(&wait.parent->child_wait, child_wait_ready, &wait);
	if (wait.zombie) {
		result = wait.zombie->pid;
		*status = wait.zombie->exit_status;
		detach_child(wait.zombie);
		kfree(wait.zombie);
	} else {
		result = wait.matched ? 0 : -ECHILD;
	}
	arch_local_irq_restore(flags);
	return result;
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
	//切到用户任务就启用该任务的页表，切到内核线程就恢复内核页表
	if (prev->mm != next->mm)
		arch_pgd_activate(next->mm ? next->mm->pgd : NULL);
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
