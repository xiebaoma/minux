#include <minux/block.h>
#include <minux/buddy.h>
#include <minux/console.h>
#include <minux/irq.h>
#include <minux/mm.h>
#include <minux/page.h>
#include <minux/ramfs.h>
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
extern const char wait_probe_child[], wait_probe_fault[], wait_probe_fork[], wait_probe_fork_oom[];

extern const char wait_probe_exec[], wait_probe_exec_errors[], wait_probe_files[], wait_probe_pipes[];
extern const char wait_probe_commands[];
extern const char wait_probe_tty[];
extern const char wait_probe_mmap[];

_Static_assert(WAIT_PROBE_PAGE_BYTES == PAGE_SIZE, "wait probe page size mismatch");

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

static void test_fork_oom(void)
{
	u64 baseline = buddy_free_pages();
	struct task_struct *parent = create_wait_probe(wait_probe_fork_oom);
	struct page *stack = alloc_pages(TASK_STACK_ORDER);
	struct test_held_page *held;

	check(parent && stack, "fork: prepare a parent and reserve one child kernel stack");
	if (parent && stack) {
		held = test_hold_all_but(0);
		free_pages(stack, TASK_STACK_ORDER);
		observed.parent_done = false;
		task_idle_once();
		check(observed.parent_done, "fork/execve: failed allocations preserve the caller, its stack and its children");
		test_release_held(held);
	} else {
		if (parent)
			task_destroy(parent);
		if (stack)
			free_pages(stack, TASK_STACK_ORDER);
	}
	check(buddy_free_pages() == baseline, "fork: failed syscall releases the provisional task and stack");
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
		{ wait_probe_exec, 0, 0, 0,
		  "execve: replace a forked process, preserve its PID and reap the whole family" },
		{ wait_probe_pipes, 0, 0, 0,
		  "pipe: dup3 replacement, rollback, blocking I/O, wrap-around, peer close and EOF" },
		{ wait_probe_commands, 0, 0, 0,
		  "commands: echo arguments, cat binary data, exit status and a shell pipeline larger than PIPE_BUF" },
		{ wait_probe_tty, 0, 0, 0,
		  "tty: retain data on EFAULT, split reads and consume EOF once" },
		{ wait_probe_files, 0, 0, 0,
		  "fd: access modes, user faults, short I/O, EMFILE, slot reuse and exit cleanup" },
		{ wait_probe_exec_errors, 0, 0, 0,
		  "execve: invalid ELF, pathname and vectors return to the same user program" },
		{ wait_probe_fork, 0, 0, 0,
		  "fork: repeated user clone, store, kernel copy and wait preserve private stacks and PIDs" },
		{ wait_probe_mmap, 0, 0, 0,
		  "mmap: validate flags, COW across fork, split via munmap and fault on the unmapped page" },
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
		struct inode *saved_root = root_inode;

		/* 写文件的探针使用独立挂载，卸载后仍可精确核对页数。 */
		if (cases[i].entry == wait_probe_exec || cases[i].entry == wait_probe_files ||
		    cases[i].entry == wait_probe_commands) {
			int err = rootfs_init(boot_disk);
			check(!err, "mount private ramfs for the user file probe");
			if (err)
				continue;
		}
		memset(&observed, 0, sizeof(observed));
		parent = create_wait_probe(cases[i].entry);
		check(parent != NULL, "wait4: create the user parent probe");
		if (parent) {
			if (cases[i].entry == wait_probe_tty) {
				const char input[] = "abc\004\b\n\004Z\n";
				for (size_t j = 0; j < sizeof(input) - 1; j++)
					tty_receive_char(input[j]);
			}
			task_idle_once();
			/* 每一轮 wait4 都可能由 idle 回收子进程后才唤醒父进程。 */
			for (unsigned int n = 0; n < FORK_PROBE_ROUNDS; n++) {
				schedule();
				if (observed.parent_done)
					break;
			}
		}
		check(observed.parent_done &&
		      observed.blocked_parents == cases[i].blocked_parents &&
		      observed.runnable_parents == cases[i].runnable_parents &&
		      observed.orphans == cases[i].orphans,
		      cases[i].description);
		if (root_inode != saved_root) {
			ramfs_destroy(root_inode);
			root_inode = saved_root;
		}
		check(task_current() == boot && buddy_free_pages() == free_before,
		      "wait4: all process records, address spaces and stacks are reclaimed");
	}
	test_fork_oom();
	arch_local_irq_restore(flags);
}
