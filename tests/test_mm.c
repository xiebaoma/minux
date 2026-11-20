#include <minux/buddy.h>
#include <minux/mm.h>
#include <minux/page.h>
#include <minux/paging.h>
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

struct held_page {
	struct held_page *next;
	struct page *page;
};

static void test_population_failure(void)
{
	const uintptr_t addr = PAGE_SIZE;
	struct mm_struct *mm = mm_alloc();
	struct held_page *held = NULL;
	struct page *page;
	u64 before;

	if (!mm || !mm_add_vma(mm, addr, addr + PAGE_SIZE, VM_READ)) {
		check(false, "mm: prepare a VMA for the population failure test");
		mm_destroy(mm);
		return;
	}
	before = buddy_free_pages();
	/* 留两页给数据页和第一张子表，让第二张子表分配失败。 */
	for (u64 remaining = before; remaining > 2; remaining--) {
		struct held_page *node;

		page = alloc_pages(0);
		if (!page)
			break;
		node = page_address(page);
		*node = (struct held_page){ held, page };
		held = node;
	}
	check(buddy_free_pages() == 2 && !mm_populate_page(mm, addr) &&
	      buddy_free_pages() == 2, "mm: failed population returns data and child table pages");
	while (held) {
		struct held_page *node = held;

		held = node->next;
		free_pages(node->page, 0);
	}
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

void test_mm(void)
{
	u64 before = buddy_free_pages();

	test_vmas();
	test_population_failure();
	check(buddy_free_pages() == before, "mm: destruction returns every private page");
}
