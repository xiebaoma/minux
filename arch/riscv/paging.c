#include <asm/sv39.h>
#include <asm/trap.h>
#include <minux/memblock.h>
#include <minux/page.h>
#include <minux/paging.h>
#include <minux/panic.h>
#include <minux/printk.h>

extern char _kernel_start[], _kernel_rodata[], _kernel_data[], _kernel_end[];

static struct sv39_table *kernel_root;

extern void riscv_enter_high_half(u64 root_pa, void (*next)(void),
				  uintptr_t offset) __noreturn;

struct boot_paging {
	struct sv39_table *root;
	struct memblock *map;
};

struct arch_pgd *arch_pgd_alloc(void)
{
	return sv39_alloc_user_root(kernel_root);
}

void *arch_phys_to_virt(u64 phys)
{
	return (void *)(uintptr_t)(kernel_root ? SV39_HIGH_START + phys : phys);
}

bool arch_virt_to_phys(const void *addr, u64 *phys)
{
	if (kernel_root)
		return sv39_lookup(kernel_root, (uintptr_t)addr, phys, NULL);
	*phys = (uintptr_t)addr;
	return true;
}

void arch_pgd_activate(struct arch_pgd *pgd)
{
	const void *root = pgd ? (const void *)pgd : (const void *)kernel_root;
	u64 root_pa;

	if (!arch_virt_to_phys(root, &root_pa) || !sv39_enable(root_pa))
		panic("cannot activate Sv39 page table %p", root);
}

static void map_range(struct boot_paging *paging, uintptr_t va, u64 start,
		      u64 end, unsigned int prot)
{
	if (start > end || end > SV39_LOW_END || (va | start | end) % PAGE_SIZE)
		panic("invalid Sv39 range: VA 0x%lx, PA [0x%llx, 0x%llx)",
		      va, start, end);

	while (start < end) {
		unsigned int level = SV39_LEVELS - 1;
		u64 size = sv39_level_size(level);

		/* 叶项不能跨越区间边界，或覆盖相邻的不同权限区间。 */
		while (size > end - start || (va | start) % size)
			size = sv39_level_size(--level);
		if (!sv39_map_leaf(paging->root, paging->map, va, start, level, prot))
			panic("cannot map Sv39 VA 0x%lx to PA 0x%llx", va, start);
		va += size;
		start += size;
	}
}

static void map_kernel_segment(struct boot_paging *paging, const char *start,
			       const char *end, unsigned int prot)
{
	/* 分页前，PC 相对寻址得到各段的物理地址。 */
	u64 pa = (uintptr_t)start;
	u64 end_pa = (uintptr_t)end;

	map_range(paging, pa, pa, end_pa, prot);
	map_range(paging, SV39_HIGH_START + pa, pa, end_pa, prot);
}

static bool map_ram(u64 start_pfn, u64 end_pfn, void *data)
{
	u64 start = start_pfn * PAGE_SIZE;

	map_range(data, SV39_HIGH_START + start, start, end_pfn * PAGE_SIZE,
		  SV39_READ | SV39_WRITE);
	return true;
}

void paging_init(struct memblock *map, const struct memblock_region *mmio,
		 size_t nr_mmio, void (*next)(void))
{
	/* 快照保留页表分配前的空闲区间，使新页表也落在可访问的 RAM 内。 */
	struct memblock usable_ram = *map;
	struct boot_paging paging = { .map = map };
	u64 root_pa;

	if (map->memory.count &&
	    map->memory.regions[map->memory.count - 1].end > SV39_LOW_END)
		panic("physical RAM outside the Sv39 linear mapping window");
	paging.root = sv39_alloc_root(map);
	if (!paging.root)
		panic("cannot allocate Sv39 root page table");

	map_kernel_segment(&paging, _kernel_start, _kernel_rodata, SV39_READ | SV39_EXEC);
	map_kernel_segment(&paging, _kernel_rodata, _kernel_data, SV39_READ);
	map_kernel_segment(&paging, _kernel_data, _kernel_end, SV39_READ | SV39_WRITE);
	memblock_for_each_free_range(&usable_ram, map_ram, &paging);
	for (size_t i = 0; i < nr_mmio; i++) {
		u64 start = mmio[i].start;
		u64 end = mmio[i].end;

		if (start >= end || end > SV39_LOW_END)
			panic("invalid Sv39 MMIO range: [0x%llx, 0x%llx)", start, end);
		start &= ~(PAGE_SIZE - 1);
		map_range(&paging, SV39_HIGH_START + start, start, pfn_up(end) * PAGE_SIZE,
			  SV39_READ | SV39_WRITE);
	}

	root_pa = (uintptr_t)paging.root;
	if (!sv39_enable(root_pa))
		panic("cannot enable Sv39 paging");
	riscv_enter_high_half(root_pa, next, SV39_HIGH_START);
}

/* 由汇编在高地址栈上进入，此时可以撤销启动恒等映射。 */
__noreturn void riscv_paging_finish(u64 root_pa, void (*next)(void))
{
	kernel_root = (struct sv39_table *)(uintptr_t)(SV39_HIGH_START + root_pa);
	trap_init();
	sv39_clear_low_half(kernel_root);
	pr_info("paging: Sv39 enabled with high-half kernel mappings\n");
	next();
	panic("kernel continuation returned");
}
