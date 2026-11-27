#include <asm/selftest.h>
#include <asm/sv39.h>
#include <minux/buddy.h>
#include <minux/memblock.h>
#include <minux/mm.h>
#include <minux/paging.h>
#include <minux/string.h>
#include "test.h"

static _Alignas(PAGE_SIZE) u8 table_pages[4 * PAGE_SIZE];

static struct sv39_table *test_root(struct memblock *map, size_t nr_pages)
{
	u64 phys;

	/* 让分配器使用测试专有页，并检查创建页表时会清除旧内容。 */
	memset(table_pages, 0xff, sizeof(table_pages));
	if (!arch_virt_to_phys(table_pages, &phys) ||
	    !memblock_add(map, phys, nr_pages * PAGE_SIZE))
		return NULL;
	return sv39_alloc_root(map);
}

static bool resolves(const struct sv39_table *root, uintptr_t va, u64 pa,
		     unsigned int prot)
{
	u64 actual_pa;
	unsigned int actual_prot;

	return sv39_lookup(root, va, &actual_pa, &actual_prot) &&
	       actual_pa == pa && actual_prot == prot;
}

static void test_walk(void)
{
	struct memblock map = { 0 };
	struct sv39_table *root = test_root(&map, 4);
	const uintptr_t va_4k = 0x1000;
	const uintptr_t va_2m = 0x200000;
	const uintptr_t va_1g = SV39_HIGH_START;
	const u64 pa_4k = (1ULL << 56) - PAGE_SIZE;
	const u64 pa_2m = 0x80400000;
	const u64 pa_1g = 0x80000000;
	const unsigned int rw = SV39_READ | SV39_WRITE;
	const unsigned int rx = SV39_READ | SV39_EXEC;

	check(root != NULL, "Sv39: allocate a root page");
	if (!root)
		return;
	check(!sv39_lookup(root, va_4k, NULL, NULL),
	      "Sv39: a new root has no mappings");
	check(sv39_map_leaf(root, &map, va_4k, pa_4k, 0, rw) &&
	      resolves(root, va_4k + 0xfff, pa_4k + 0xfff, rw),
	      "Sv39: translate a 4 KiB leaf at the highest encodable PA");
	check(!sv39_lookup(root, va_4k + PAGE_SIZE, NULL, NULL),
	      "Sv39: new child tables leave neighboring pages unmapped");
	check(sv39_map_leaf(root, &map, va_2m, pa_2m, 1, rx) &&
	      resolves(root, va_2m + 0x1fffff, pa_2m + 0x1fffff, rx),
	      "Sv39: translate a 2 MiB leaf and its last byte");
	check(sv39_map_leaf(root, &map, va_1g, pa_1g, 2, SV39_READ) &&
	      resolves(root, va_1g + 0x3fffffff, pa_1g + 0x3fffffff, SV39_READ),
	      "Sv39: translate a 1 GiB leaf in the upper canonical half");

	check(!sv39_map_leaf(root, &map, va_4k, 0, 0, rx) &&
	      !sv39_map_leaf(root, &map, va_2m + PAGE_SIZE, 0, 0, rx) &&
	      !sv39_map_leaf(root, &map, 0, 0, 1, rx) &&
	      resolves(root, va_4k, pa_4k, rw) &&
	      resolves(root, va_2m + PAGE_SIZE, pa_2m + PAGE_SIZE, rx),
	      "Sv39: reject duplicate and overlapping leaves without overwriting them");
	check(!sv39_map_leaf(root, &map, SV39_LOW_END, 0, 0, rx) &&
	      !sv39_lookup(root, SV39_LOW_END + va_4k, NULL, NULL) &&
	      !sv39_lookup(root, va_1g - 1, NULL, NULL),
	      "Sv39: reject noncanonical addresses on both sides of the gap");
	check(!sv39_map_leaf(root, &map, 0x400000 + PAGE_SIZE, 0, 1, rw) &&
	      !sv39_map_leaf(root, &map, 0x400000, PAGE_SIZE, 1, rw) &&
	      !sv39_map_leaf(root, &map, 0x400000, 1ULL << 56, 0, rw) &&
	      !sv39_map_leaf(root, &map, 0x400000, 0, SV39_LEVELS, rw),
	      "Sv39: reject misaligned addresses, out of range PA and invalid levels");
	check(!sv39_map_leaf(root, &map, 0x400000, 0, 1, SV39_WRITE) &&
	      !sv39_map_leaf(root, &map, 0x400000, 0, 1, 0) &&
	      !sv39_map_leaf(root, &map, 0x400000, 0, 1, SV39_READ | 0x10) &&
	      sv39_map_leaf(root, &map, 0x400000, 0, 1, SV39_EXEC) &&
	      resolves(root, 0x400123, 0x123, SV39_EXEC),
	      "Sv39: reject invalid permissions and allow execute only");
}

static void test_allocation_failure(void)
{
	struct memblock map = { 0 };
	struct sv39_table *root = test_root(&map, 2);
	struct memblock_region reserved;
	u64 extra;

	check(root != NULL, "Sv39: set up a root with one remaining table page");
	if (!root)
		return;
	reserved = map.reserved.regions[0];
	check(!sv39_map_leaf(root, &map, 0, 0, 0, SV39_READ) &&
	      !sv39_lookup(root, 0, NULL, NULL) &&
	      map.reserved.count == 1 &&
	      map.reserved.regions[0].start == reserved.start &&
	      map.reserved.regions[0].end == reserved.end,
	      "Sv39: failure on the second child rolls back the path and reservation");
	check(sv39_map_leaf(root, &map, 0, 0, 1, SV39_READ) &&
	      resolves(root, 0x1000, 0x1000, SV39_READ) &&
	      !memblock_alloc_pages(&map, 1, &extra),
	      "Sv39: reuse the page released by the failed insertion");
}

struct probe_result {
	unsigned long cause;
	uintptr_t address;
};

extern struct probe_result sv39_probe_read(uintptr_t addr);
extern struct probe_result sv39_probe_write(uintptr_t addr);
extern struct probe_result sv39_probe_exec(uintptr_t addr);
extern char _kernel_start[];
extern void riscv_trap_entry(void);

static const u32 rodata_word = 0x12345678;
static volatile u32 data_word = 0x00008067; /* jalr zero, 0(ra) */

static bool boot_page_reserved(u64 phys)
{
	const struct memblock_type *reserved = &boot_memblock.reserved;

	for (unsigned int i = 0; i < reserved->count; i++) {
		const struct memblock_region *range = &reserved->regions[i];

		if (range->start <= phys && phys + PAGE_SIZE <= range->end)
			return true;
	}
	return false;
}

static void test_active_paging(void)
{
	const uintptr_t hole = SV39_HIGH_START + PAGE_SIZE;
	const uintptr_t old_text = 0x80200000;
	const uintptr_t rodata = (uintptr_t)&rodata_word;
	const uintptr_t data = (uintptr_t)&data_word;
	const uintptr_t text = (uintptr_t)_kernel_start;
	struct probe_result result;
	unsigned long satp;
	uintptr_t pc, sp, stvec;
	u64 root, phys;

	asm volatile("csrr %0, satp" : "=r"(satp));
	check(satp >> 60 == 8, "Sv39: hardware is using Sv39");
	asm volatile("auipc %0, 0\n\t"
		     "mv %1, sp\n\t"
		     "csrr %2, stvec"
		     : "=r"(pc), "=r"(sp), "=r"(stvec));
	check(pc >= SV39_HIGH_START && sp >= SV39_HIGH_START,
	      "Sv39: execution and the kernel stack use high addresses");
	check(stvec >= SV39_HIGH_START && stvec == (uintptr_t)riscv_trap_entry,
	      "Sv39: the active trap entry uses its high address");
	check(text >= SV39_HIGH_START && arch_virt_to_phys((const void *)text, &phys) &&
	      phys == old_text,
	      "Sv39: high kernel text maps to the physical boot address");
	root = (satp & ((1UL << 44) - 1)) << PAGE_SHIFT;
	check(root && arch_virt_to_phys(arch_phys_to_virt(root), &phys) &&
	      phys == root && boot_page_reserved(root),
	      "Sv39: root is accessible at a high address and reserved from buddy");
	check(!arch_virt_to_phys((void *)old_text, &phys),
	      "Sv39: the boot identity mapping is gone");
	check(!arch_virt_to_phys((void *)hole, &phys),
	      "Sv39: a hole does not translate through the kernel mapping");

	result = sv39_probe_read(hole);
	check(result.cause == 13 && result.address == hole,
	      "Sv39: loading from an unmapped page raises a load page fault");
	result = sv39_probe_read(old_text);
	check(result.cause == 13 && result.address == old_text,
	      "Sv39: the former low kernel address raises a load page fault");
	result = sv39_probe_write(text);
	check(result.cause == 15 && result.address == text,
	      "Sv39: writing kernel text raises a store page fault");
	result = sv39_probe_write(rodata);
	check(result.cause == 15 && result.address == rodata,
	      "Sv39: writing kernel rodata raises a store page fault");
	result = sv39_probe_write(data);
	check(result.cause == 0, "Sv39: kernel data remains writable");
	result = sv39_probe_exec(data);
	check(result.cause == 12 && result.address == data,
	      "Sv39: executing kernel data raises an instruction page fault");
}

static void test_linear_mapping(void)
{
	struct page *page = alloc_pages(0);
	u8 *addr;
	u64 phys;

	check(page != NULL, "Sv39: allocate a RAM page for the linear mapping test");
	if (!page)
		return;
	addr = (u8 *)page_address(page) + PAGE_SIZE - 1;
	check((uintptr_t)addr >= SV39_HIGH_START &&
	      arch_virt_to_phys(addr, &phys) &&
	      phys == page_to_phys(page) + PAGE_SIZE - 1 &&
	      virt_to_page(addr) == page,
	      "Sv39: a buddy page uses the high mapping and retains its page offset");
	check(free_pages(page, 0), "Sv39: release the linear mapping test page");
}

static void probe_user_root(u64 root, uintptr_t va, u8 value, bool writable)
{
	const unsigned long sum = 1UL << 18;
	struct probe_result result;
	unsigned long status;
	bool active = sv39_enable(root);

	check(active, "Sv39: switch to a user root and keep running in the high half");
	if (!active)
		return;
	asm volatile("csrrc %0, sstatus, %1" : "=r"(status) : "r"(sum) : "memory");
	result = sv39_probe_read(va);
	check(result.cause == 13 && result.address == va,
	      "Sv39: SUM=0 blocks supervisor loads from a user page");
	asm volatile("csrs sstatus, %0" : : "r"(sum) : "memory");
	result = sv39_probe_read(va);
	check(!result.cause && *(const volatile u8 *)va == value,
	      "Sv39: the same user address reads this root's private data");
	result = sv39_probe_write(va);
	check(writable ? !result.cause : result.cause == 15 && result.address == va,
	      "Sv39: a user leaf enforces the VMA write permission");
	result = sv39_probe_read(va + PAGE_SIZE);
	check(result.cause == 13 && result.address == va + PAGE_SIZE,
	      "Sv39: an unpopulated page in a VMA has no mapping");
	asm volatile("csrw sstatus, %0" : : "r"(status) : "memory");
}

static void test_mm_paging(void)
{
	const uintptr_t va = 0x400000;
	u64 before = buddy_free_pages();
	struct mm_struct *a = mm_alloc(), *b = mm_alloc();
	struct page *page_a, *page_b;
	u64 root_a, root_b, boot_root;
	unsigned long satp;
	bool ready;

	ready = a && b &&
		mm_add_vma(a, va, va + 2 * PAGE_SIZE, VM_READ | VM_WRITE) &&
		mm_add_vma(b, va, va + 2 * PAGE_SIZE, VM_READ) &&
		arch_virt_to_phys(a->pgd, &root_a) && arch_virt_to_phys(b->pgd, &root_b) &&
		root_a != root_b;
	check(ready, "Sv39: two mm objects have distinct hardware roots and VMAs");
	if (!ready)
		goto out;

	page_a = mm_populate_page(a, va);
	page_b = mm_populate_page(b, va);
	check(page_a && page_b && page_a != page_b,
	      "Sv39: populate the same virtual page with distinct physical pages");
	if (!page_a || !page_b || page_a == page_b)
		goto out;
	*(u8 *)page_address(page_a) = 0x11;
	*(u8 *)page_address(page_b) = 0x22;

	asm volatile("csrr %0, satp" : "=r"(satp));
	boot_root = (satp & ((1UL << 44) - 1)) << PAGE_SHIFT;
	probe_user_root(root_a, va, 0x11, true);
	probe_user_root(root_b, va, 0x22, false);
	ready = sv39_enable(boot_root);
	check(ready, "Sv39: restore the boot root before destroying address spaces");
	if (!ready)
		return;

out:
	mm_destroy(a);
	mm_destroy(b);
	check(buddy_free_pages() == before, "Sv39: reclaim both private roots and their pages");
}

static struct page *map_test_page(struct arch_pgd *pgd, uintptr_t va, u8 fill,
				  unsigned int vm_flags)
{
	struct page *page = alloc_pages(0);

	if (!page)
		return NULL;
	memset(page_address(page), fill, PAGE_SIZE);
	if (!arch_pgd_map(pgd, va, page, vm_flags)) {
		free_pages(page, 0);
		return NULL;
	}
	return page;
}

static void test_unmap(void)
{
	const uintptr_t va0 = sv39_level_size(1) - PAGE_SIZE; /* L1#0, L0#0 末页 */
	const uintptr_t va1 = sv39_level_size(1);             /* L1#0, L0#1 首页 */
	const uintptr_t va2 = va1 + PAGE_SIZE;                /* L1#0, L0#1 次页 */
	const uintptr_t va3 = sv39_level_size(2);             /* L1#1, L0#0 首页 */
	const unsigned long sum = 1UL << 18;
	const unsigned int rw = VM_READ | VM_WRITE;
	u64 baseline = buddy_free_pages(), before_map, root_pa, boot_root;
	struct arch_pgd *pgd = arch_pgd_alloc(), *child = NULL;
	struct page *p0, *p1, *p2, *p3, *shared;
	struct probe_result result;
	unsigned long satp, status;
	bool active;

	check(pgd != NULL, "Sv39: allocate a user root for unmap tests");
	if (!pgd)
		return;
	before_map = buddy_free_pages();
	p0 = map_test_page(pgd, va0, 0x11, rw);
	p1 = map_test_page(pgd, va1, 0x22, rw);
	p2 = map_test_page(pgd, va2, 0x33, rw);
	p3 = map_test_page(pgd, va3, 0x44, rw);
	check(p0 && p1 && p2 && p3 && buddy_free_pages() == before_map - 9,
	      "Sv39: map four pages across two L0 and two L1 child tables");
	if (!p0 || !p1 || !p2 || !p3)
		goto out;

	arch_pgd_unmap_range(pgd, va0, va0);
	arch_pgd_unmap_range(pgd, PAGE_SIZE, 3 * PAGE_SIZE);
	check(buddy_free_pages() == before_map - 9 &&
	      arch_pgd_user_page(pgd, va0, NULL) == p0,
	      "Sv39: empty and unmapped ranges leave existing mappings intact");

	asm volatile("csrr %0, satp" : "=r"(satp));
	boot_root = (satp & ((1UL << 44) - 1)) << PAGE_SHIFT;
	active = arch_virt_to_phys(pgd, &root_pa) && sv39_enable(root_pa);
	check(active, "Sv39: activate user root before unmapping a live page");
	if (!active)
		goto out;

	asm volatile("csrrs %0, sstatus, %1" : "=r"(status) : "r"(sum) : "memory");
	result = sv39_probe_read(va1);
	check(!result.cause && *(const volatile u8 *)va1 == 0x22,
	      "Sv39: live page is readable before unmap");
	arch_pgd_unmap_range(pgd, va1, va1 + PAGE_SIZE);
	result = sv39_probe_read(va1);
	check(!arch_pgd_user_page(pgd, va1, NULL) &&
	      result.cause == 13 && result.address == va1,
	      "Sv39: unmapped live page clears lookup and raises a load page fault");
	result = sv39_probe_read(va2);
	check(!result.cause && *(const volatile u8 *)va2 == 0x33 &&
	      !sv39_probe_read(va0).cause && *(const volatile u8 *)va0 == 0x11,
	      "Sv39: neighboring pages on both sides remain accessible after unmap");
	asm volatile("csrw sstatus, %0" : : "r"(status) : "memory");
	check(sv39_enable(boot_root), "Sv39: restore boot root after live unmap");
	check(buddy_free_pages() == before_map - 8,
	      "Sv39: partial L0 unmap returns only the data page");

	arch_pgd_unmap_range(pgd, va2, va2 + PAGE_SIZE);
	check(!arch_pgd_user_page(pgd, va2, NULL) &&
	      buddy_free_pages() == before_map - 6,
	      "Sv39: unmapping the last page in an L0 table reclaims the empty L0 table");

	arch_pgd_unmap_range(pgd, va0, va3 + PAGE_SIZE);
	check(!arch_pgd_user_page(pgd, va0, NULL) &&
	      !arch_pgd_user_page(pgd, va3, NULL) &&
	      buddy_free_pages() == before_map,
	      "Sv39: spanning unmap reclaims all remaining data, L0 and L1 pages");

	shared = map_test_page(pgd, va1, 0x77, rw);
	child = arch_pgd_alloc();
	check(shared && child && arch_pgd_fork(child, pgd) && page_ref_count(shared) == 2,
	      "Sv39: share a COW page between two roots before unmap");
	if (shared && child && page_ref_count(shared) == 2) {
		arch_pgd_unmap_range(child, va1, va1 + PAGE_SIZE);
		check(!arch_pgd_user_page(child, va1, NULL) &&
		      arch_pgd_user_page(pgd, va1, NULL) == shared &&
		      page_ref_count(shared) == 1 &&
		      *(const u8 *)page_address(shared) == 0x77,
		      "Sv39: unmapping a COW page drops only one reference and preserves the peer");
		arch_pgd_unmap_range(pgd, va1, va1 + PAGE_SIZE);
		check(!arch_pgd_user_page(pgd, va1, NULL),
		      "Sv39: unmapping the last COW reference clears the mapping");
	}
out:
	if (child)
		arch_pgd_free(child);
	arch_pgd_free(pgd);
	check(buddy_free_pages() == baseline,
	      "Sv39: unmap tests leave no leaked data or page-table pages");
}

void test_sv39(void)
{
	test_walk();
	test_allocation_failure();
	test_active_paging();
	test_linear_mapping();
	test_mm_paging();
	test_unmap();
}
