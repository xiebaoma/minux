#include <minux/buddy.h>
#include <minux/mm.h>
#include <minux/page.h>
#include <minux/paging.h>
#include <minux/string.h>
#include "test.h"

static void test_vmas(void)
{
	const uintptr_t start = 4 * PAGE_SIZE;
	const uintptr_t end = 8 * PAGE_SIZE;
	const uintptr_t limit = arch_user_limit();
	const struct vm_area_struct *first, *second, *last;
	struct mm_struct *mm = mm_alloc();

	check(mm != NULL, "mm: allocate an address space");
	if (!mm)
		return;
	check(!find_vma(mm, start), "mm: a new address space has no VMAs");
	last = mm_add_vma(mm, limit - PAGE_SIZE, limit, VM_EXEC);
	second = mm_add_vma(mm, end, end + PAGE_SIZE, VM_READ);
	first = mm_add_vma(mm, start, end, VM_READ | VM_WRITE);
	check(first && first->vm_start == start && first->vm_end == end &&
	      first->vm_mm == mm && first->vm_flags == (VM_READ | VM_WRITE) &&
	      second && last, "mm: insert out of order and allow adjacent VMAs");
	check(!mm_add_vma(mm, start, end, VM_READ) &&
	      !mm_add_vma(mm, start - PAGE_SIZE, start + PAGE_SIZE, VM_READ) &&
	      !mm_add_vma(mm, end - PAGE_SIZE, end + PAGE_SIZE, VM_READ),
	      "mm: reject duplicate, partial and spanning overlaps");
	check(!mm_add_vma(mm, 0, PAGE_SIZE, VM_READ) &&
	      !mm_add_vma(mm, PAGE_SIZE, PAGE_SIZE, VM_READ) &&
	      !mm_add_vma(mm, PAGE_SIZE + 1, start, VM_READ) &&
	      !mm_add_vma(mm, PAGE_SIZE, start - 1, VM_READ) &&
	      !mm_add_vma(mm, limit - PAGE_SIZE, limit + PAGE_SIZE, VM_READ),
	      "mm: reject null page, empty, unaligned and out of range VMAs");
	check(!mm_add_vma(mm, PAGE_SIZE, start, 0) &&
	      !mm_add_vma(mm, PAGE_SIZE, start, VM_WRITE) &&
	      !mm_add_vma(mm, PAGE_SIZE, start, VM_READ | (1U << 8)),
	      "mm: reject unsupported VMA permissions");
	check(first && second && last && find_vma(mm, start - 1) == first &&
	      find_vma(mm, start) == first && find_vma(mm, end - 1) == first &&
	      find_vma(mm, end) == second && find_vma(mm, end + PAGE_SIZE) == last &&
	      find_vma(mm, limit - 1) == last && !find_vma(mm, limit),
	      "mm: find a containing or next VMA at boundaries and in gaps");
	check(!mm_populate_page(mm, start - PAGE_SIZE) &&
	      !mm_populate_page(mm, start + 1) && !mm_populate_page(mm, limit),
	      "mm: reject population outside a VMA or off a page boundary");
	mm_destroy(mm);
}

struct test_held_page {
	struct test_held_page *next;
	struct page *page;
};

static void test_anonymous_faults(void)
{
	const uintptr_t anon = 2 * PAGE_SIZE;
	const uintptr_t eager = 4 * PAGE_SIZE;
	struct mm_struct *mm = mm_alloc();
	struct page *page;
	u64 before;

	if (!mm || !mm_add_vma(mm, anon, anon + PAGE_SIZE, VM_READ | VM_ANON) ||
	    !mm_add_vma(mm, eager, eager + PAGE_SIZE, VM_READ)) {
		check(false, "mm: prepare anonymous and eager VMAs");
		mm_destroy(mm);
		return;
	}
	before = buddy_free_pages();
	check(!mm_handle_user_fault(mm, anon - 1, VM_READ) &&
	      !mm_handle_user_fault(mm, anon, VM_WRITE) &&
	      !mm_handle_user_fault(mm, anon, VM_EXEC) &&
	      !mm_handle_user_fault(mm, eager, VM_READ) &&
	      buddy_free_pages() == before,
	      "mm: denied and non-anonymous faults leave the page table empty");
	check(mm_handle_user_fault(mm, anon + PAGE_SIZE - 1, VM_READ),
	      "mm: a permitted byte address faults in its anonymous page");
	page = mm_user_page(mm, anon, VM_READ);
	check(page && *(u8 *)page_address(page) == 0 && !mm_user_page(mm, anon, VM_WRITE),
	      "mm: the new anonymous page is zeroed and read-only");
	check(!mm_handle_user_fault(mm, anon, VM_READ) &&
	      mm_user_page(mm, anon, VM_READ) == page,
	      "mm: an exception on a present page cannot be retried by replacing it");
	mm_destroy(mm);
}

struct test_held_page *test_hold_all_but(u64 keep)
{
	struct test_held_page *held = NULL;

	for (u64 remaining = buddy_free_pages(); remaining > keep; remaining--) {
		struct test_held_page *node;
		struct page *page = alloc_pages(0);

		if (!page)
			break;
		node = page_address(page);
		*node = (struct test_held_page){ held, page };
		held = node;
	}
	return held;
}

void test_release_held(struct test_held_page *held)
{
	while (held) {
		struct test_held_page *node = held;

		held = node->next;
		free_pages(node->page, 0);
	}
}

static void test_population_failure(void)
{
	const uintptr_t addr = PAGE_SIZE;
	struct mm_struct *mm = mm_alloc();
	struct test_held_page *held = NULL;
	struct page *page;
	u64 before;

	if (!mm || !mm_add_vma(mm, addr, addr + PAGE_SIZE, VM_READ)) {
		check(false, "mm: prepare a VMA for the population failure test");
		mm_destroy(mm);
		return;
	}
	before = buddy_free_pages();
	/* 留两页给数据页和第一张子表，让第二张子表分配失败。 */
	held = test_hold_all_but(2);
	check(buddy_free_pages() == 2 && !mm_populate_page(mm, addr) &&
	      buddy_free_pages() == 2, "mm: failed population returns data and child table pages");
	test_release_held(held);
	check(buddy_free_pages() == before, "mm: release all pages held by the failure test");
	page = mm_populate_page(mm, addr);
	check(page && buddy_free_pages() == before - 3,
	      "mm: retry installs one data page and two child tables");
	if (page) {
		u8 *data = page_address(page);
		bool zero = true;

		for (size_t i = 0; i < PAGE_SIZE; i++)
			zero &= data[i] == 0;
		check(zero, "mm: a newly populated page is zeroed");
		data[0] = 0x5a;
		check(!mm_populate_page(mm, addr) && data[0] == 0x5a &&
		      buddy_free_pages() == before - 3,
		      "mm: reject a duplicate mapping without replacing it or leaking a page");
	}
	mm_destroy(mm);
}

static void test_fork_cow(void)
{
	const uintptr_t data = PAGE_SIZE, hole = 2 * PAGE_SIZE, code = arch_user_limit() / 2;
	const unsigned int rw = VM_READ | VM_WRITE;
	struct mm_struct *parent = mm_alloc(), *child = NULL, *grandchild = NULL;
	struct page *original, *text, *copy;
	struct test_held_page *held;
	unsigned int access;

	if (!parent || !mm_add_vma(parent, data, hole + PAGE_SIZE, rw | VM_ANON) ||
	    !mm_add_vma(parent, code, code + PAGE_SIZE, VM_READ | VM_EXEC))
		goto setup_failed;
	original = mm_populate_page(parent, data);
	text = mm_populate_page(parent, code);
	if (!original || !text)
		goto setup_failed;
	memset(page_address(original), 0x5a, PAGE_SIZE);

	/* 根表和低地址子表能建成，到远端映射前耗尽，检查部分构造的回滚。 */
	held = test_hold_all_but(3);
	child = mm_dup(parent);
	check(!child && buddy_free_pages() == 3 && page_ref_count(original) == 1 &&
	      arch_pgd_user_page(parent->pgd, data, &access) == original && access == rw,
	      "fork: partial clone failure restores references and parent write access");
	test_release_held(held);
	if (child)
		goto out;

	child = mm_dup(parent);
	if (child)
		grandchild = mm_dup(child);
	if (!grandchild)
		goto setup_failed;
	check(page_ref_count(original) == 3 &&
	      arch_pgd_user_page(parent->pgd, data, &access) == original && access == (VM_READ | PGD_COW) &&
	      arch_pgd_user_page(child->pgd, data, &access) == original && access == (VM_READ | PGD_COW) &&
	      arch_pgd_user_page(grandchild->pgd, data, &access) == original && access == (VM_READ | PGD_COW),
	      "fork: three generations share a write-protected data page");
	check(arch_pgd_user_page(child->pgd, code, &access) == text && access == (VM_READ | VM_EXEC) &&
	      page_ref_count(text) == 3 && !mm_handle_user_fault(child, code, VM_WRITE) &&
	      !mm_user_page(child, code, VM_WRITE), "fork: shared text remains read-only");
	check(!arch_pgd_user_page(child->pgd, hole, NULL) && mm_user_page(child, hole, VM_WRITE) &&
	      !arch_pgd_user_page(parent->pgd, hole, NULL),
	      "fork: a sparse anonymous VMA faults in independently");

	held = test_hold_all_but(0);
	check(!mm_handle_user_fault(child, data, VM_WRITE) && !mm_user_page(child, data, VM_WRITE) &&
	      arch_pgd_user_page(child->pgd, data, &access) == original && access == (VM_READ | PGD_COW) &&
	      page_ref_count(original) == 3, "cow: a failed write leaves the shared page intact");
	test_release_held(held);
	copy = mm_user_page(child, data, VM_WRITE);
	check(copy && copy != original && page_ref_count(original) == 2 &&
	      memcmp(page_address(copy), page_address(original), PAGE_SIZE) == 0,
	      "cow: retry copies all bytes into one private writable page");
	if (copy)
		*(u8 *)page_address(copy) = 0xa5;
	check(*(u8 *)page_address(original) == 0x5a, "cow: private write preserves the original data");
	mm_destroy(grandchild);
	grandchild = NULL;
	held = test_hold_all_but(0);
	check(page_ref_count(original) == 1 && mm_handle_user_fault(parent, data, VM_WRITE) &&
	      arch_pgd_user_page(parent->pgd, data, &access) == original && access == rw,
	      "cow: the sole owner regains write access even with no free pages");
	test_release_held(held);
	goto out;
setup_failed:
	check(false, "fork: prepare address spaces for COW");
out:
	mm_destroy(grandchild);
	mm_destroy(child);
	mm_destroy(parent);
}

void test_mm(void)
{
	u64 before = buddy_free_pages();

	test_vmas();
	test_population_failure();
	test_anonymous_faults();
	test_fork_cow();
	check(buddy_free_pages() == before, "mm: destruction returns every private page");
}
