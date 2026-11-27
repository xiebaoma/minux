#include <minux/buddy.h>
#include <minux/errno.h>
#include <minux/limits.h>
#include <minux/mm.h>
#include <minux/page.h>
#include <minux/paging.h>
#include <minux/slab.h>
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

struct test_held_obj {
	struct test_held_obj *next;
};

static struct test_held_obj *drain_vma_slab(void)
{
	struct test_held_obj *head = NULL, *obj;

	while ((obj = kmalloc(sizeof(struct vm_area_struct))) != NULL) {
		obj->next = head;
		head = obj;
	}
	return head;
}

static void release_vma_slab(struct test_held_obj *head)
{
	while (head) {
		struct test_held_obj *next = head->next;

		kfree(head);
		head = next;
	}
}

static bool vmas_ordered(const struct mm_struct *mm)
{
	uintptr_t prev_end = PAGE_SIZE;
	const struct vm_area_struct *vma;

	list_for_each_entry(vma, &mm->vmas, vm_link) {
		if (vma->vm_mm != mm || vma->vm_start < prev_end ||
		    vma->vm_start >= vma->vm_end ||
		    (vma->vm_start | vma->vm_end) % PAGE_SIZE)
			return false;
		prev_end = vma->vm_end;
	}
	return true;
}

static void test_mmap_placement(void)
{
	const uintptr_t user_limit = arch_user_limit();
	const uintptr_t stack_start = user_limit - 8 * PAGE_SIZE;
	const uintptr_t guard_limit = stack_start - PAGE_SIZE;
	const uintptr_t hint = 16 * PAGE_SIZE;
	const unsigned int rw = VM_READ | VM_WRITE;
	struct mm_struct *mm = mm_alloc(), *dup = NULL;
	const struct vm_area_struct *vma;
	uintptr_t a, b, c, d, e, low;
	int err = 0;

	check(mm && mm->mmap_limit == user_limit,
	      "mmap: a new address space defaults mmap_limit to arch_user_limit");
	if (!mm)
		return;

	check(!mm_mmap_anon(NULL, 0, PAGE_SIZE, VM_READ, &err) && err == -EINVAL &&
	      !mm_mmap_anon(mm, 0, 0, VM_READ, &err) && err == -EINVAL &&
	      !mm_mmap_anon(mm, 0, PAGE_SIZE, 0, &err) && err == -EINVAL &&
	      !mm_mmap_anon(mm, 0, PAGE_SIZE, VM_WRITE, &err) && err == -EINVAL &&
	      !mm_mmap_anon(mm, 0, PAGE_SIZE, VM_READ | (1U << 8), &err) && err == -EINVAL,
	      "mmap: reject null mm, zero length and invalid permissions");
	check(!mm_mmap_anon(mm, 0, SIZE_MAX, VM_READ, &err) && err == -ENOMEM &&
	      !mm_mmap_anon(mm, 0, user_limit, VM_READ, &err) && err == -ENOMEM,
	      "mmap: reject overflowing or oversized lengths with -ENOMEM");

	if (!mm_add_vma(mm, 4 * PAGE_SIZE, 6 * PAGE_SIZE, VM_READ | VM_EXEC) ||
	    !mm_add_vma(mm, stack_start, user_limit, rw | VM_ANON)) {
		check(false, "mmap: set up code and stack VMAs");
		goto out;
	}
	mm->mmap_limit = guard_limit;

	a = mm_mmap_anon(mm, 0, PAGE_SIZE + 1, rw, &err);
	vma = find_vma(mm, a);
	check(a == guard_limit - 2 * PAGE_SIZE && err == 0 && vma &&
	      vma->vm_start == a && vma->vm_end == guard_limit &&
	      vma->vm_flags == (rw | VM_ANON) &&
	      !arch_pgd_user_page(mm->pgd, a, NULL),
	      "mmap: top-down placement rounds length up, stays lazy and leaves stack guard untouched");

	b = mm_mmap_anon(mm, 0, PAGE_SIZE, VM_READ, &err);
	check(b == a - PAGE_SIZE && err == 0,
	      "mmap: subsequent automatic mapping packs directly below prior mapping");

	c = mm_mmap_anon(mm, hint, 2 * PAGE_SIZE, rw, &err);
	check(c == hint && err == 0,
	      "mmap: honor a page-aligned hint when the requested range is free");

	d = mm_mmap_anon(mm, hint + PAGE_SIZE, PAGE_SIZE, rw, &err);
	e = mm_mmap_anon(mm, stack_start - PAGE_SIZE, PAGE_SIZE, rw, &err);
	check(d == b - PAGE_SIZE && e == d - PAGE_SIZE &&
	      mm_mmap_anon(mm, hint + 1, PAGE_SIZE, rw, &err) == e - PAGE_SIZE &&
	      find_vma(mm, stack_start - PAGE_SIZE)->vm_start == stack_start,
	      "mmap: overlapping, guard-zone and unaligned hints fall back to top-down search");

	/* 将上限收紧到代码段末端，迫使分配器落入代码段下方的低地址空洞。 */
	mm->mmap_limit = 6 * PAGE_SIZE;
	low = mm_mmap_anon(mm, 0, 2 * PAGE_SIZE, rw, &err);
	check(low == 2 * PAGE_SIZE && err == 0 &&
	      !mm_mmap_anon(mm, 0, 2 * PAGE_SIZE, rw, &err) && err == -ENOMEM,
	      "mmap: use the lowest gap above the null page and fail when gaps are exhausted");
	check(vmas_ordered(mm), "mmap: VMA list stays strictly ordered and non-overlapping");

	dup = mm_dup(mm);
	check(dup && dup->mmap_limit == mm->mmap_limit && vmas_ordered(dup),
	      "mmap: mm_dup preserves mmap_limit and sorted VMAs");
out:
	mm_destroy(dup);
	mm_destroy(mm);
}

static bool write_byte(struct mm_struct *mm, uintptr_t addr, u8 val)
{
	struct page *page = mm_user_page(mm, addr, VM_WRITE);

	if (!page)
		return false;
	*(u8 *)page_address(page) = val;
	return true;
}

static bool expect_byte(struct mm_struct *mm, uintptr_t addr, u8 val)
{
	struct page *page = mm_user_page(mm, addr, VM_READ);

	return page && *(u8 *)page_address(page) == val;
}

static void test_munmap_and_split(void)
{
	const uintptr_t limit = arch_user_limit();
	const uintptr_t base = 32 * PAGE_SIZE;
	const unsigned int rw = VM_READ | VM_WRITE;
	struct mm_struct *mm = mm_alloc(), *parent = NULL, *child = NULL;
	const struct vm_area_struct *left, *right;
	struct test_held_page *held_pages;
	struct test_held_obj *held_objs;
	struct page *p0;
	unsigned int access;
	uintptr_t addr, cow_base;
	u64 before_pages;
	int err = 0;

	if (!mm) {
		check(false, "munmap: allocate an address space");
		return;
	}
	check(mm_munmap(NULL, PAGE_SIZE, PAGE_SIZE) == -EINVAL &&
	      mm_munmap(mm, PAGE_SIZE, 0) == -EINVAL &&
	      mm_munmap(mm, PAGE_SIZE + 1, PAGE_SIZE) == -EINVAL &&
	      mm_munmap(mm, limit, PAGE_SIZE) == -EINVAL &&
	      mm_munmap(mm, PAGE_SIZE, limit) == -EINVAL &&
	      mm_munmap(mm, PAGE_SIZE, SIZE_MAX) == -EINVAL,
	      "munmap: reject null mm, zero length, unaligned start and out-of-range intervals");
	check(mm_munmap(mm, base, 4 * PAGE_SIZE) == 0,
	      "munmap: unmapping an unmapped range succeeds as a no-op");

	addr = mm_mmap_anon(mm, base, 5 * PAGE_SIZE, rw, &err);
	check(addr == base && err == 0, "munmap: create a 5-page anonymous VMA");
	if (addr != base)
		goto out;
	for (unsigned int i = 0; i < 5; i++)
		check(write_byte(mm, base + i * PAGE_SIZE, 0x10 + i),
		      "munmap: fault in anonymous page before partial unmaps");

	before_pages = buddy_free_pages();
	check(mm_munmap(mm, base, 1) == 0 &&
	      buddy_free_pages() == before_pages + 1 &&
	      !mm_handle_user_fault(mm, base, VM_READ) &&
	      expect_byte(mm, base + PAGE_SIZE, 0x11),
	      "munmap: head trim rounds length up, frees first page and keeps remainder");

	check(mm_munmap(mm, base + 4 * PAGE_SIZE, PAGE_SIZE) == 0 &&
	      buddy_free_pages() == before_pages + 2 &&
	      !mm_handle_user_fault(mm, base + 4 * PAGE_SIZE, VM_READ) &&
	      expect_byte(mm, base + 3 * PAGE_SIZE, 0x13),
	      "munmap: tail trim frees last page and shrinks vm_end");

	/* 耗尽 buddy 与 VMA 所属 slab 档位，验证中间拆分失败时保持原子性。 */
	held_pages = test_hold_all_but(0);
	held_objs = drain_vma_slab();
	check(mm_munmap(mm, base + 2 * PAGE_SIZE, PAGE_SIZE) == -ENOMEM &&
	      !mm_mmap_anon(mm, 0, PAGE_SIZE, rw, &err) && err == -ENOMEM,
	      "munmap: middle split and mmap return -ENOMEM when VMA allocation fails");
	release_vma_slab(held_objs);
	test_release_held(held_pages);
	left = find_vma(mm, base + PAGE_SIZE);
	check(left && left->vm_start == base + PAGE_SIZE &&
	      left->vm_end == base + 4 * PAGE_SIZE &&
	      expect_byte(mm, base + PAGE_SIZE, 0x11) &&
	      expect_byte(mm, base + 2 * PAGE_SIZE, 0x12) &&
	      expect_byte(mm, base + 3 * PAGE_SIZE, 0x13),
	      "munmap: failed split leaves original VMA and all mapped pages intact");

	before_pages = buddy_free_pages();
	check(mm_munmap(mm, base + 2 * PAGE_SIZE, PAGE_SIZE) == 0 &&
	      buddy_free_pages() == before_pages + 1,
	      "munmap: middle split succeeds once memory is available and frees middle page");
	left = find_vma(mm, base + PAGE_SIZE);
	right = find_vma(mm, base + 2 * PAGE_SIZE);
	check(left && right && left != right &&
	      left->vm_start == base + PAGE_SIZE && left->vm_end == base + 2 * PAGE_SIZE &&
	      right->vm_start == base + 3 * PAGE_SIZE && right->vm_end == base + 4 * PAGE_SIZE &&
	      !mm_handle_user_fault(mm, base + 2 * PAGE_SIZE, VM_READ) &&
	      expect_byte(mm, base + PAGE_SIZE, 0x11) &&
	      expect_byte(mm, base + 3 * PAGE_SIZE, 0x13) &&
	      vmas_ordered(mm),
	      "munmap: split produces two ordered VMAs around an unmapped hole");

	check(mm_mmap_anon(mm, base + 6 * PAGE_SIZE, 3 * PAGE_SIZE, rw, &err) ==
	      base + 6 * PAGE_SIZE &&
	      write_byte(mm, base + 6 * PAGE_SIZE, 0x16) &&
	      write_byte(mm, base + 7 * PAGE_SIZE, 0x17) &&
	      write_byte(mm, base + 8 * PAGE_SIZE, 0x18),
	      "munmap: add a third VMA beyond a multi-page hole");
	check(mm_munmap(mm, base + PAGE_SIZE, 7 * PAGE_SIZE) == 0 &&
	      !mm_handle_user_fault(mm, base + PAGE_SIZE, VM_READ) &&
	      !mm_handle_user_fault(mm, base + 3 * PAGE_SIZE, VM_READ) &&
	      !mm_handle_user_fault(mm, base + 7 * PAGE_SIZE, VM_READ) &&
	      expect_byte(mm, base + 8 * PAGE_SIZE, 0x18) &&
	      find_vma(mm, 0) && find_vma(mm, 0)->vm_start == base + 8 * PAGE_SIZE,
	      "munmap: spanning unmap removes full VMAs, skips holes and trims trailing VMA");
	check(mm_munmap(mm, base + 8 * PAGE_SIZE, PAGE_SIZE) == 0 &&
	      list_empty(&mm->vmas) && !find_vma(mm, 0),
	      "munmap: unmapping the last VMA leaves an empty address space");

	/* 验证 COW 共享映射在子进程部分解除映射后，父子两侧引用计数与缺页语义独立。 */
	parent = mm_alloc();
	cow_base = parent ? mm_mmap_anon(parent, base, 3 * PAGE_SIZE, rw, &err) : 0;
	if (!parent || cow_base != base ||
	    !write_byte(parent, cow_base, 0xaa) ||
	    !write_byte(parent, cow_base + 2 * PAGE_SIZE, 0xcc)) {
		check(false, "munmap: prepare parent anonymous mapping for COW");
		goto out;
	}
	p0 = arch_pgd_user_page(parent->pgd, cow_base, NULL);
	child = mm_dup(parent);
	check(child && p0 && page_ref_count(p0) == 2,
	      "munmap: fork shares populated anonymous pages");
	if (!child)
		goto out;
	check(mm_munmap(child, cow_base, 2 * PAGE_SIZE) == 0 &&
	      !mm_handle_user_fault(child, cow_base, VM_READ) &&
	      !mm_handle_user_fault(child, cow_base + PAGE_SIZE, VM_WRITE) &&
	      page_ref_count(p0) == 1,
	      "munmap: child partial unmap drops its COW reference and blocks unmapped pages");
	held_pages = test_hold_all_but(0);
	check(mm_handle_user_fault(parent, cow_base, VM_WRITE) &&
	      arch_pgd_user_page(parent->pgd, cow_base, &access) == p0 &&
	      access == rw,
	      "munmap: parent regains sole-owner write access without allocating a copy");
	test_release_held(held_pages);
	check(write_byte(child, cow_base + 2 * PAGE_SIZE, 0xdd) &&
	      write_byte(parent, cow_base + PAGE_SIZE, 0xbb) &&
	      expect_byte(parent, cow_base, 0xaa) &&
	      expect_byte(parent, cow_base + PAGE_SIZE, 0xbb) &&
	      expect_byte(parent, cow_base + 2 * PAGE_SIZE, 0xcc) &&
	      expect_byte(child, cow_base + 2 * PAGE_SIZE, 0xdd),
	      "munmap: remaining COW and sparse pages operate independently across parent and child");
out:
	mm_destroy(child);
	mm_destroy(parent);
	mm_destroy(mm);
}

void test_mm(void)
{
	u64 before = buddy_free_pages();

	test_vmas();
	test_population_failure();
	test_anonymous_faults();
	test_fork_cow();
	test_mmap_placement();
	test_munmap_and_split();
	check(buddy_free_pages() == before, "mm: destruction returns every private page");
}
