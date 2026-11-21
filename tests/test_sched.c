#include <minux/buddy.h>
#include <minux/irq.h>
#include <minux/task.h>
#include <minux/wait.h>
#include "test.h"

struct schedule_probe {
	unsigned int *progress;
	unsigned int turn;
};

static void schedule_probe_entry(void *arg)
{
	const struct schedule_probe *probe = arg;

	for (unsigned int round = 0; round < 2; round++) {
		check(*probe->progress == 2 * round + probe->turn,
		      "sched: peer tasks run in FIFO order");
		(*probe->progress)++;
		schedule();
	}
}

struct irq_schedule_probe {
	unsigned long disabled_flags;
	unsigned long enabled_flags;
	bool completed;
};

static void irq_schedule_probe_entry(void *arg)
{
	struct irq_schedule_probe *probe = arg;
	unsigned long flags = arch_local_irq_save();

	check(flags == probe->enabled_flags, "sched: a new task starts with IRQs enabled");
	schedule();
	flags = arch_local_irq_save();
	check(flags == probe->disabled_flags, "sched: resuming a task preserves its disabled IRQ state");

	arch_local_irq_restore(probe->enabled_flags);
	schedule();
	flags = arch_local_irq_save();
	check(flags == probe->enabled_flags, "sched: resuming a task restores its enabled IRQ state");
	probe->completed = true;
	arch_local_irq_restore(flags);
}

static void test_schedule_irq_restore(void)
{
	struct irq_schedule_probe probe = { 0 };
	struct task_struct *worker = task_create(irq_schedule_probe_entry, &probe);
	unsigned long flags;

	check(worker != NULL, "sched: create a task to check IRQ state across switches");
	if (!worker)
		return;

	probe.disabled_flags = arch_local_irq_save();
	/* 启动自测在设备中断初始化前运行。 */
	arch_local_irq_enable();
	probe.enabled_flags = arch_local_irq_save();
	arch_local_irq_restore(probe.enabled_flags);
	schedule();
	flags = arch_local_irq_save();
	check(flags == probe.enabled_flags, "sched: boot resumes with IRQs enabled after yielding");

	schedule();
	flags = arch_local_irq_save();
	check(flags == probe.disabled_flags, "sched: boot keeps IRQs disabled when an IRQ-enabled task yields");
	schedule();
	check(probe.completed, "sched: the IRQ probe runs to completion");
	if (!probe.completed)
		task_destroy(worker);
	arch_local_irq_restore(probe.disabled_flags);
}

struct exit_probe {
	u64 free_with_tasks;
	unsigned int entries;
};

static void exit_probe_entry(void *arg)
{
	struct exit_probe *probe = arg;

	if (probe->entries == 1)
		check(buddy_free_pages() >= probe->free_with_tasks + (1UL << TASK_STACK_ORDER),
		      "sched: a new task reaps its predecessor's stack before entering");
	probe->entries++;
}

static void test_task_exit(void)
{
	struct task_struct *boot = task_current();
	u64 free_before = buddy_free_pages();

	for (unsigned int round = 0; round < 3; round++) {
		struct exit_probe probe = { 0 };
		struct task_struct *first = task_create(exit_probe_entry, &probe);
		struct task_struct *second = task_create(exit_probe_entry, &probe);

		check(first && second, "sched: create tasks that return from their entries");
		if (!first || !second) {
			task_destroy(first);
			task_destroy(second);
			return;
		}
		probe.free_with_tasks = buddy_free_pages();
		schedule();
		check(task_current() == boot && probe.entries == 2,
		      "sched: both entries return and boot resumes");
		check(buddy_free_pages() == free_before,
		      "sched: the resumed boot task reaps the last task, returning all pages");
	}
}

static void idle_probe_entry(void *arg)
{
	unsigned int *runs = arg;

	(*runs)++;
	schedule();
	(*runs)++;
}

static void test_idle_dispatch(void)
{
	struct task_struct *boot = task_current();
	u64 free_before = buddy_free_pages();
	unsigned int runs[2] = { 0 };
	struct task_struct *first = task_create(idle_probe_entry, &runs[0]);
	struct task_struct *second = task_create(idle_probe_entry, &runs[1]);

	check(first && second, "sched: create two tasks for the idle path");
	if (!first || !second) {
		task_destroy(first);
		task_destroy(second);
		return;
	}

	task_idle_once();
	check(task_current() == boot && runs[0] == 2 && runs[1] == 2,
	      "sched: idle resumes only after both tasks have yielded and exited");
	if (runs[0] < 2)
		task_destroy(first);
	if (runs[1] < 2)
		task_destroy(second);
	check(buddy_free_pages() == free_before,
	      "sched: idle fallback reaps the last task and returns all pages");
}

struct wait_probe {
	struct wait_queue queue;
	bool ready;
	unsigned int woken;
};

struct wait_worker {
	struct wait_probe *probe;
	unsigned int turn;
};

static bool wait_probe_ready(void *arg)
{
	const struct wait_probe *probe = arg;

	return probe->ready;
}

static void wait_probe_entry(void *arg)
{
	const struct wait_worker *worker = arg;
	struct wait_probe *probe = worker->probe;
	unsigned long enabled = arch_local_irq_save();
	unsigned long expected = arch_local_irq_save();
	unsigned long flags;

	if (worker->turn == 0)
		expected = enabled;
	arch_local_irq_restore(expected);

	wait_event(&probe->queue, wait_probe_ready, probe);
	flags = arch_local_irq_save();
	check(flags == expected, "wait: sleeping preserves the caller's IRQ state");
	check(probe->ready && probe->woken == worker->turn,
	      "wait: waiters resume in FIFO order after the condition is true");
	probe->woken++;
	arch_local_irq_restore(enabled);
}

static void test_wait_queue(void)
{
	struct wait_probe probe = { 0 };
	struct wait_worker workers[] = { { &probe, 0 }, { &probe, 1 } };
	struct task_struct *first, *second;
	u64 free_before = buddy_free_pages();

	init_wait_queue(&probe.queue);
	first = task_create(wait_probe_entry, &workers[0]);
	second = task_create(wait_probe_entry, &workers[1]);
	check(first && second, "wait: create two waiters");
	if (!first || !second) {
		task_destroy(first);
		task_destroy(second);
		return;
	}

	task_idle_once();
	check(probe.woken == 0 && first->state == TASK_BLOCKED && second->state == TASK_BLOCKED,
	      "wait: both waiters block and return control to idle");
	wake_up_all(&probe.queue);
	task_idle_once();
	check(probe.woken == 0, "wait: waking before the condition is true puts waiters back to sleep");

	probe.ready = true;
	wake_up_all(&probe.queue);
	task_idle_once();
	check(probe.woken == 2 && list_empty(&probe.queue.tasks),
	      "wait: wake-up runs both waiters and drains the queue");
	check(buddy_free_pages() == free_before, "wait: exiting waiters return their stack pages");
}

void test_sched(void)
{
	struct task_struct *boot = task_current();
	struct task_struct *first, *second;
	unsigned int progress = 0;
	struct schedule_probe probes[] = {
		{ .progress = &progress, .turn = 0 },
		{ .progress = &progress, .turn = 1 },
	};
	u64 free_before = buddy_free_pages();

	schedule();
	check(task_current() == boot && boot->state == TASK_RUNNING,
	      "sched: an empty run queue keeps the current task running");

	first = task_create(schedule_probe_entry, &probes[0]);
	second = task_create(schedule_probe_entry, &probes[1]);
	check(first && second, "sched: create two runnable tasks");
	if (first && second) {
		schedule();
		check(task_current() == boot && progress == 2,
		      "sched: both peers run before the boot task resumes");
		schedule();
		check(task_current() == boot && progress == 4,
		      "sched: yielding puts each task at the tail for the next round");
	}

	if (first)
		check(task_destroy(first), "sched: remove the first queued task");
	if (second)
		check(task_destroy(second), "sched: remove the second queued task");
	schedule();
	check(task_current() == boot && buddy_free_pages() == free_before,
	      "sched: cleanup leaves an empty run queue and returns the task pages");
	test_schedule_irq_restore();
	test_task_exit();
	/* 启动任务从这里开始承担 idle 角色。 */
	test_idle_dispatch();
	test_wait_queue();
}
