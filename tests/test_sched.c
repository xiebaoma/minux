#include <minux/buddy.h>
#include <minux/task.h>
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
	test_task_exit();
}
