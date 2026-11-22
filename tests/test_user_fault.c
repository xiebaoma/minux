#include <minux/buddy.h>
#include <minux/errno.h>
#include <minux/irq.h>
#include <minux/mm.h>
#include <minux/page.h>
#include <minux/selftest.h>
#include <minux/string.h>
#include <minux/task.h>
#include <minux/uaccess.h>
#include "test.h"
#include "user_fault_probe.h"

extern const char fault_probe_start[], fault_probe_end[];
extern const char fault_probe_gap[], fault_probe_readonly[], fault_probe_noexec[];
extern const char fault_probe_valid[];

_Static_assert(PROBE_PAGE_BYTES == PAGE_SIZE, "user fault probe page size mismatch");

static unsigned int successful_copies;

long selftest_user_copy(uintptr_t to, uintptr_t from, size_t size)
{
	char data[16];

	if (size > sizeof(data) || copy_from_user(data, from, size) ||
	    copy_to_user(to, data, size))
		return -EFAULT;
	successful_copies++;
	return 0;
}

static bool run_fault_probe(const char *entry)
{
	const size_t code_size = (uintptr_t)fault_probe_end - (uintptr_t)fault_probe_start;
	const uintptr_t user_pc = PROBE_CODE + ((uintptr_t)entry - (uintptr_t)fault_probe_start);
	struct mm_struct *mm = mm_alloc();
	struct page *code;

	if (!mm)
		return false;
	if (code_size > PAGE_SIZE ||
	    !mm_add_vma(mm, PROBE_CODE, PROBE_STACK, VM_READ | VM_EXEC) ||
	    !mm_add_vma(mm, PROBE_STACK, PROBE_STACK_TOP, VM_READ | VM_WRITE) ||
	    !mm_add_vma(mm, PROBE_ANON, PROBE_ANON_END, VM_READ | VM_WRITE | VM_ANON) ||
	    !mm_add_vma(mm, PROBE_RO, PROBE_RO + PAGE_SIZE, VM_READ | VM_ANON))
		goto fail;
	code = mm_populate_page(mm, PROBE_CODE);
	if (!code || !mm_populate_page(mm, PROBE_STACK))
		goto fail;

	memcpy(page_address(code), fault_probe_start, code_size);
	if (!task_create_user(mm, user_pc, PROBE_STACK_TOP))
		goto fail;

	task_idle_once();
	return true;
fail:
	mm_destroy(mm);
	return false;
}

void test_user_fault(void)
{
	static const struct {
		const char *entry;
		unsigned int expected_copies;
		const char *description;
	} cases[] = {
		{ fault_probe_gap, 0, "user fault: a load in the VMA gap terminates the task" },
		{ fault_probe_readonly, 0, "user fault: writing a mapped read-only page terminates the task" },
		{ fault_probe_noexec, 0, "user fault: executing a non-executable page terminates the task" },
		{ fault_probe_valid, 3, "user fault: later task retries demand faults and gets -EFAULT for bad copies" },
	};
	unsigned long flags = arch_local_irq_save();
	struct task_struct *boot = task_current();
	u64 free_before = buddy_free_pages();

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		successful_copies = 0;
		check(run_fault_probe(cases[i].entry), "user fault: create and dispatch a user probe");
		check(successful_copies == cases[i].expected_copies, cases[i].description);
		check(task_current() == boot && buddy_free_pages() == free_before,
		      "user fault: control returns to the kernel and all task pages are reclaimed");
	}
	arch_local_irq_restore(flags);
}
