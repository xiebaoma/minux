#include <asm/sv39.h>
#include <minux/memblock.h>
#include <minux/string.h>

#define SV39_ENTRIES (1U << SV39_VPN_BITS)
#define SV39_PPN_BITS 44
#define SV39_PA_END (1ULL << (PAGE_SHIFT + SV39_PPN_BITS))
#define SATP_MODE_SV39 (8UL << 60)

#define PTE_VALID    (1ULL << 0)
#define PTE_ACCESSED (1ULL << 6)
#define PTE_DIRTY    (1ULL << 7)
#define PTE_PPN_SHIFT 10
#define PTE_PPN_MASK ((1ULL << SV39_PPN_BITS) - 1)
#define PTE_PROT (SV39_READ | SV39_WRITE | SV39_EXEC)

struct sv39_table {
	u64 entries[SV39_ENTRIES];
};

_Static_assert(sizeof(struct sv39_table) == PAGE_SIZE,
	       "each Sv39 table must occupy one base page");

static bool canonical(uintptr_t va)
{
	return va < SV39_LOW_END || va >= -SV39_LOW_END;
}

static unsigned int vpn_index(uintptr_t va, unsigned int level)
{
	return (va / sv39_level_size(level)) & (SV39_ENTRIES - 1);
}

static u64 pte_encode_pa(u64 pa)
{
	return (pa >> PAGE_SHIFT) << PTE_PPN_SHIFT;
}

static u64 pte_phys(u64 pte)
{
	return ((pte >> PTE_PPN_SHIFT) & PTE_PPN_MASK) << PAGE_SHIFT;
}

static struct sv39_table *alloc_table(struct memblock *map, u64 *pa)
{
	if (!memblock_alloc_pages(map, 1, pa))
		return NULL;
	return memset(arch_phys_to_virt(*pa), 0, PAGE_SIZE);
}

struct sv39_table *sv39_alloc_root(struct memblock *map)
{
	u64 pa;

	return alloc_table(map, &pa);
}

static bool install_path(struct memblock *map, u64 *entry, uintptr_t va,
			 unsigned int level, unsigned int target, u64 leaf)
{
	struct memblock pending = *map;
	u64 first = 0;
	u64 *link = &first;

	/* 在所有子表分配成功后，再提交保留区并连接到原页表。 */
	do {
		struct sv39_table *child;
		u64 pa;

		child = alloc_table(&pending, &pa);
		if (!child)
			return false;
		*link = pte_encode_pa(pa) | PTE_VALID;
		level--;
		link = &child->entries[vpn_index(va, level)];
	} while (level > target);
	*link = leaf;
	map->reserved = pending.reserved;
	*entry = first;
	return true;
}

bool sv39_map_leaf(struct sv39_table *root, struct memblock *map, uintptr_t va,
		   u64 pa, unsigned int level, unsigned int prot)
{
	struct sv39_table *table = root;
	u64 *entry, size, leaf;
	unsigned int current = SV39_LEVELS - 1;

	if (!root || level >= SV39_LEVELS || !canonical(va) || pa >= SV39_PA_END ||
	    !prot || (prot & ~PTE_PROT) ||
	    ((prot & SV39_WRITE) && !(prot & SV39_READ)))
		return false;
	size = sv39_level_size(level);
	if ((va | pa) & (size - 1))
		return false;

	/* 预置 A/D，访存不依赖硬件更新这些位。 */
	leaf = pte_encode_pa(pa) | prot | PTE_VALID | PTE_ACCESSED;
	if (prot & SV39_WRITE)
		leaf |= PTE_DIRTY;

	for (; current > level; current--) {
		entry = &table->entries[vpn_index(va, current)];
		if (!(*entry & PTE_VALID))
			return install_path(map, entry, va, current, level, leaf);
		if (*entry & PTE_PROT)
			return false;
		table = arch_phys_to_virt(pte_phys(*entry));
	}
	entry = &table->entries[vpn_index(va, level)];
	if (*entry & PTE_VALID)
		return false;

	*entry = leaf;
	return true;
}

bool sv39_lookup(const struct sv39_table *root, uintptr_t va, u64 *pa,
		 unsigned int *prot)
{
	const struct sv39_table *table = root;

	if (!root || !canonical(va))
		return false;
	for (int level = SV39_LEVELS - 1; level >= 0; level--) {
		u64 pte = table->entries[vpn_index(va, level)];

		if (!(pte & PTE_VALID))
			return false;
		if (pte & PTE_PROT) {
			if (pa)
				*pa = pte_phys(pte) | (va & (sv39_level_size(level) - 1));
			if (prot)
				*prot = pte & PTE_PROT;
			return true;
		}
		table = arch_phys_to_virt(pte_phys(pte));
	}
	return false;
}

bool sv39_enable(const struct sv39_table *root)
{
	u64 phys;
	unsigned long satp, active;

	if (!root || !arch_virt_to_phys(root, &phys) || phys >= SV39_PA_END ||
	    (phys & (PAGE_SIZE - 1)))
		return false;
	satp = SATP_MODE_SV39 | (phys >> PAGE_SHIFT);
	asm volatile("sfence.vma\n\t"
		     "csrw satp, %1\n\t"
		     "sfence.vma\n\t"
		     "csrr %0, satp"
		     : "=r"(active) : "r"(satp) : "memory");
	return active == satp;
}
