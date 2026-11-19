#include <asm/sv39.h>
#include <minux/memblock.h>
#include <minux/page.h>
#include <minux/paging.h>
#include <minux/panic.h>
#include <minux/printk.h>

extern char _kernel_start[], _kernel_rodata[], _kernel_data[], _kernel_end[];

static const struct sv39_table *kernel_root;

struct boot_paging {
	struct sv39_table *root;
	struct memblock *map;
};

bool arch_virt_to_phys(const void *addr, u64 *phys)
{
	uintptr_t va = (uintptr_t)addr;
	u64 pa;

	if (kernel_root && (!sv39_lookup(kernel_root, va, &pa, NULL) || pa != va))
		return false;
	*phys = va;
	return true;
}

static void map_identity(struct boot_paging *paging, u64 start, u64 end,
			 unsigned int prot)
{
	if (start > end || end > SV39_LOW_END || (start | end) % PAGE_SIZE)
		panic("invalid Sv39 identity range: [0x%llx, 0x%llx)", start, end);

	while (start < end) {
		unsigned int level = SV39_LEVELS - 1;
		u64 size = sv39_level_size(level);

		/* 叶项不能跨越区间边界，或覆盖相邻的不同权限区间。 */
		while (size > end - start || start % size)
			size = sv39_level_size(--level);
		if (!sv39_map_leaf(paging->root, paging->map, start, start, level, prot))
			panic("cannot map Sv39 identity page at 0x%llx", start);
		start += size;
	}
}

static bool map_ram(u64 start_pfn, u64 end_pfn, void *data)
{
	map_identity(data, start_pfn * PAGE_SIZE, end_pfn * PAGE_SIZE,
		     SV39_READ | SV39_WRITE);
	return true;
}

void paging_init(struct memblock *map, const struct memblock_region *mmio,
		 size_t nr_mmio)
{
	/* 快照保留页表分配前的空闲区间，使新页表也落在可访问的 RAM 内。 */
	struct memblock usable_ram = *map;
	struct boot_paging paging = { .map = map };

	if (map->memory.count &&
	    map->memory.regions[map->memory.count - 1].end > SV39_LOW_END)
		panic("physical RAM outside the Sv39 identity window");
	paging.root = sv39_alloc_root(map);
	if (!paging.root)
		panic("cannot allocate Sv39 root page table");

	map_identity(&paging, (u64)_kernel_start, (u64)_kernel_rodata,
		     SV39_READ | SV39_EXEC);
	map_identity(&paging, (u64)_kernel_rodata, (u64)_kernel_data, SV39_READ);
	map_identity(&paging, (u64)_kernel_data, (u64)_kernel_end,
		     SV39_READ | SV39_WRITE);
	memblock_for_each_free_range(&usable_ram, map_ram, &paging);
	for (size_t i = 0; i < nr_mmio; i++) {
		u64 start = mmio[i].start;
		u64 end = mmio[i].end;

		if (start >= end || end > SV39_LOW_END)
			panic("invalid Sv39 MMIO range: [0x%llx, 0x%llx)", start, end);
		map_identity(&paging, start & ~(PAGE_SIZE - 1), pfn_up(end) * PAGE_SIZE,
			     SV39_READ | SV39_WRITE);
	}

	if (!sv39_enable(paging.root))
		panic("cannot enable Sv39 paging");
	kernel_root = paging.root;
	pr_info("paging: Sv39 enabled with identity mappings\n");
}
