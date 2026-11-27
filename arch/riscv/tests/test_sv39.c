#include <asm/selftest.h>
#include <asm/sv39.h>
#include <minux/buddy.h>
#include <minux/memblock.h>
#include <minux/mm.h>
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

void test_sv39(void)
{
	test_walk();
	test_allocation_failure();
	test_active_paging();
	test_linear_mapping();
	test_mm_paging();
}
