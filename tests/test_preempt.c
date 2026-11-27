#include <minux/timer.h>
#include <minux/buddy.h>
#include <minux/irq.h>
#include <minux/task.h>
#include "test.h"

struct preempt_worker {
	struct preempt_worker *peer;
	volatile bool started;
	volatile bool saw_peer;
	bool saw_peer_resume;
	bool irqs_preserved;
};

static void preempt_worker_entry(void *arg)
{
	struct preempt_worker *worker = arg;
	unsigned long flags = arch_local_irq_save();
	unsigned long disabled = arch_local_irq_save();
	unsigned long start = timer_ticks();
	unsigned long resumed_flags;

	arch_local_irq_restore(flags);
	if (flags == disabled)
		return;
	worker->started = true;
	/* 两个线程都不主动让出 CPU，且各自等待对方观察到自己的进展。 */
	while (!worker->peer->started && timer_ticks() - start < 3)
		;
	worker->saw_peer = worker->peer->started;
	while (worker->saw_peer && !worker->peer->saw_peer && timer_ticks() - start < 3)
		;
	worker->saw_peer_resume = worker->peer->saw_peer;
	resumed_flags = arch_local_irq_save();
	worker->irqs_preserved = resumed_flags == flags;
	arch_local_irq_restore(resumed_flags);
}

void test_preempt(void)
{
	struct preempt_worker first = { 0 };
	struct preempt_worker second = { .peer = &first };
	unsigned long flags = arch_local_irq_save();
	unsigned long start = timer_ticks();
	u64 free_before = buddy_free_pages();
	struct task_struct *a, *b;

	first.peer = &second;
	a = task_create(preempt_worker_entry, &first);
	b = task_create(preempt_worker_entry, &second);
	check(a && b, "preempt: create two CPU-bound threads");
	if (a && b) {
		task_idle_once();
		check(first.saw_peer_resume && second.saw_peer_resume && timer_ticks() - start >= 2,
		      "preempt: both threads make progress through timer preemption alone");
		check(first.irqs_preserved && second.irqs_preserved,
		      "preempt: interrupt state survives trap return");
	} else {
		task_destroy(a);
		task_destroy(b);
	}
	check(buddy_free_pages() == free_before, "preempt: both stacks are reclaimed");
	arch_local_irq_restore(flags);
}
