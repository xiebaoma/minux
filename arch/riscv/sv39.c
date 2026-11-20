#include <asm/sv39.h>
#include <minux/buddy.h>
#include <minux/memblock.h>
#include <minux/mm.h>
#include <minux/paging.h>
#include <minux/string.h>

#define SV39_ENTRIES (1U << SV39_VPN_BITS)
#define SV39_PPN_BITS 44
#define SV39_PA_END (1ULL << (PAGE_SHIFT + SV39_PPN_BITS))
#define SATP_MODE_SV39 (8UL << 60)

#define PTE_VALID    (1ULL << 0)
#define PTE_USER     (1ULL << 4)
#define PTE_ACCESSED (1ULL << 6)
#define PTE_DIRTY    (1ULL << 7)
#define PTE_PPN_SHIFT 10
#define PTE_PPN_MASK ((1ULL << SV39_PPN_BITS) - 1)
#define PTE_LEAF (SV39_READ | SV39_WRITE | SV39_EXEC)

struct sv39_table {
	u64 entries[SV39_ENTRIES];
};

_Static_assert(sizeof(struct sv39_table) == PAGE_SIZE,
	       "each Sv39 table must occupy one base page");

struct arch_pgd {
	struct sv39_table root;
};

static bool canonical(uintptr_t va)
{
	return va < SV39_LOW_END || va >= SV39_HIGH_START;
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

static u64 make_leaf(u64 pa, unsigned int prot)
{
	u64 leaf;

	if (!(prot & PTE_LEAF) || ((prot & SV39_WRITE) && !(prot & SV39_READ)))
		return 0;
	/* 预置 A/D，访存不依赖硬件更新这些位。 */
	leaf = pte_encode_pa(pa) | prot | PTE_VALID | PTE_ACCESSED;
	if (prot & SV39_WRITE)
		leaf |= PTE_DIRTY;
	return leaf;
}

/* 找到映射路径上第一个空项，已有映射或子树冲突时返回 NULL。 */
static u64 *find_empty_entry(struct sv39_table *table, uintptr_t va,
			     unsigned int target, unsigned int *missing_level)
{
	for (unsigned int level = SV39_LEVELS - 1;; level--) {
		u64 *entry = &table->entries[vpn_index(va, level)];

		if (!(*entry & PTE_VALID)) {
			*missing_level = level;
			return entry;
		}
		if (level == target || (*entry & PTE_LEAF))
			return NULL;
		table = arch_phys_to_virt(pte_phys(*entry));
	}
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
	u64 *entry, size, leaf;
	unsigned int current;

	if (!root || level >= SV39_LEVELS || !canonical(va) || pa >= SV39_PA_END ||
	    (prot & ~PTE_LEAF))
		return false;
	size = sv39_level_size(level);
	if ((va | pa) & (size - 1))
		return false;
	leaf = make_leaf(pa, prot);
	if (!leaf)
		return false;
	entry = find_empty_entry(root, va, level, &current);
	if (!entry)
		return false;
	if (current > level)
		return install_path(map, entry, va, current, level, leaf);
	*entry = leaf;
	return true;
}

uintptr_t arch_user_limit(void)
{
	return SV39_LOW_END;
}

struct arch_pgd *sv39_alloc_user_root(const struct sv39_table *kernel)
{
	const unsigned int half = SV39_ENTRIES / 2;
	struct page *page = alloc_pages(0);
	struct arch_pgd *pgd;

	if (!page)
		return NULL;
	pgd = memset(page_address(page), 0, PAGE_SIZE);
	memcpy(pgd->root.entries + half, kernel->entries + half,
	       half * sizeof(u64));
	return pgd;
}

static bool install_user_path(u64 *entry, uintptr_t va, unsigned int level, u64 leaf)
{
	struct page *page;
	struct sv39_table *child;

	if (!level) {
		*entry = leaf;
		return true;
	}
	page = alloc_pages(0);
	if (!page)
		return false;
	child = memset(page_address(page), 0, PAGE_SIZE);
	level--;
	/* 子树完整后才发布父项。失败时释放尚未挂入页表的子表。 */
	if (!install_user_path(&child->entries[vpn_index(va, level)], va, level, leaf)) {
		free_pages(page, 0);
		return false;
	}
	*entry = pte_encode_pa(page_to_phys(page)) | PTE_VALID;
	return true;
}

bool arch_pgd_map(struct arch_pgd *pgd, uintptr_t va, struct page *page,
		  unsigned int vm_flags)
{
	unsigned int level, prot = PTE_USER;
	u64 *entry, leaf;

	if (va >= SV39_LOW_END || va % PAGE_SIZE ||
	    (vm_flags & ~(VM_READ | VM_WRITE | VM_EXEC)))
		return false;
	if (vm_flags & VM_READ)
		prot |= SV39_READ;
	if (vm_flags & VM_WRITE)
		prot |= SV39_WRITE;
	if (vm_flags & VM_EXEC)
		prot |= SV39_EXEC;
	leaf = make_leaf(page_to_phys(page), prot);
	if (!leaf)
		return false;
	entry = find_empty_entry(&pgd->root, va, 0, &level);
	return entry && install_user_path(entry, va, level, leaf);
}

static void free_user_entries(struct sv39_table *table, unsigned int count)
{
	for (unsigned int i = 0; i < count; i++) {
		u64 pte = table->entries[i];
		void *addr;

		if (!(pte & PTE_VALID))
			continue;
		addr = arch_phys_to_virt(pte_phys(pte));
		if (!(pte & PTE_LEAF))
			free_user_entries(addr, SV39_ENTRIES);
		free_pages(virt_to_page(addr), 0);
	}
}

void arch_pgd_free(struct arch_pgd *pgd)
{
	/* 高半区根项共享启动页表的子树。 */
	free_user_entries(&pgd->root, SV39_ENTRIES / 2);
	free_pages(virt_to_page(pgd), 0);
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
		if (pte & PTE_LEAF) {
			if (pa)
				*pa = pte_phys(pte) | (va & (sv39_level_size(level) - 1));
			if (prot)
				*prot = pte & PTE_LEAF;
			return true;
		}
		table = arch_phys_to_virt(pte_phys(pte));
	}
	return false;
}

bool sv39_enable(u64 root_pa)
{
	unsigned long satp, active;

	if (root_pa >= SV39_PA_END || (root_pa & (PAGE_SIZE - 1)))
		return false;
	satp = SATP_MODE_SV39 | (root_pa >> PAGE_SHIFT);
	asm volatile("sfence.vma\n\t"
		     "csrw satp, %1\n\t"
		     "sfence.vma\n\t"
		     "csrr %0, satp"
		     : "=r"(active) : "r"(satp) : "memory");
	return active == satp;
}

void sv39_clear_low_half(struct sv39_table *root)
{
	memset(root->entries, 0, sizeof(root->entries) / 2);
	asm volatile("sfence.vma" : : : "memory");
}
