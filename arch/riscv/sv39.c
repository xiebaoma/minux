#include <asm/sv39.h>
#include <minux/buddy.h>
#include <minux/bug.h>
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
#define PTE_COW      (1ULL << 8) /* RSW 软件位。 */
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
	    (vm_flags & ~VM_ACCESS_MASK))
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
	if (!entry || !install_user_path(entry, va, level, leaf))
		return false;

	/* 缺页时可能新挂入非叶项，刷新本 hart 的全部地址翻译。 */
	asm volatile("sfence.vma" : : : "memory");
	return true;
}

static bool table_empty(const struct sv39_table *table)
{
	for (unsigned int i = 0; i < SV39_ENTRIES; i++) {
		if (table->entries[i] & PTE_VALID)
			return false;
	}
	return true;
}

static bool unmap_user_range(struct sv39_table *table, unsigned int level,
			     uintptr_t start, uintptr_t end, bool *needs_flush)
{
	const u64 step = sv39_level_size(level);
	bool changed = false;

	for (uintptr_t va = start; va < end;) {
		uintptr_t next = (va & ~(step - 1)) + step;
		unsigned int idx = vpn_index(va, level);
		u64 pte = table->entries[idx];

		if (next > end)
			next = end;
		if (!(pte & PTE_VALID)) {
			va = next;
			continue;
		}
		if (!level) {
			BUG_ON(!(pte & PTE_LEAF) || !(pte & PTE_USER));
			table->entries[idx] = 0;
			put_page(virt_to_page(arch_phys_to_virt(pte_phys(pte))));
			*needs_flush = true;
			changed = true;
		} else {
			struct sv39_table *child;

			BUG_ON(pte & PTE_LEAF);
			child = arch_phys_to_virt(pte_phys(pte));
			if (unmap_user_range(child, level - 1, va, next, needs_flush) &&
			    table_empty(child)) {
				table->entries[idx] = 0;
				/* 释放空子表前先刷新翻译，避免页表遍历缓存指向已归还页。 */
				asm volatile("sfence.vma" : : : "memory");
				*needs_flush = false;
				free_pages(virt_to_page(child), 0);
				changed = true;
			}
		}
		va = next;
	}
	return changed;
}

void arch_pgd_unmap_range(struct arch_pgd *pgd, uintptr_t start, uintptr_t end)
{
	bool needs_flush = false;

	if (start >= end)
		return;
	BUG_ON(!pgd || end > SV39_LOW_END || (start | end) % PAGE_SIZE);
	unmap_user_range(&pgd->root, SV39_LEVELS - 1, start, end, &needs_flush);
	if (needs_flush)
		asm volatile("sfence.vma" : : : "memory");
}

//如果页表项原本可写，就清除写权限、加上 PTE_COW 标记；原本不可写的项保持不变。
static u64 write_protect(u64 pte)
{
	return (pte & SV39_WRITE) ? ((pte & ~SV39_WRITE) | PTE_COW) : pte;
}

//逐项复制页表
static bool clone_user_entries(struct sv39_table *dst, const struct sv39_table *src,
			       unsigned int count)
{
	for (unsigned int i = 0; i < count; i++) {
		u64 pte = src->entries[i];
		void *addr;
		struct page *page;
		struct sv39_table *child;

		if (!(pte & PTE_VALID))
			continue;
		addr = arch_phys_to_virt(pte_phys(pte));
		if (pte & PTE_LEAF) {
			if (!get_page(virt_to_page(addr)))
				return false;
			dst->entries[i] = write_protect(pte);
			continue;
		}
		page = alloc_pages(0);
		if (!page)
			return false;
		child = memset(page_address(page), 0, PAGE_SIZE);
		/* 先挂入空子表，使失败路径能从根表释放所有已取得的引用。 */
		dst->entries[i] = pte_encode_pa(page_to_phys(page)) | PTE_VALID;
		if (!clone_user_entries(child, addr, SV39_ENTRIES))
			return false;
	}
	return true;
}

static void protect_user_entries(struct sv39_table *table, unsigned int count)
{
	for (unsigned int i = 0; i < count; i++) {
		u64 pte = table->entries[i];

		if (!(pte & PTE_VALID))
			continue;
		if (pte & PTE_LEAF)
			table->entries[i] = write_protect(pte);
		else
			protect_user_entries(arch_phys_to_virt(pte_phys(pte)), SV39_ENTRIES);
	}
}

bool arch_pgd_fork(struct arch_pgd *dst, struct arch_pgd *src)
{
	if (!clone_user_entries(&dst->root, &src->root, SV39_ENTRIES / 2))
		return false;
	//递归遍历父页表，把父进程原本可写的叶项也改为 COW。
	protect_user_entries(&src->root, SV39_ENTRIES / 2);
	asm volatile("sfence.vma" : : : "memory");
	return true;
}

static void free_user_entries(struct sv39_table *table, unsigned int count)
{
	for (unsigned int i = 0; i < count; i++) {
		u64 pte = table->entries[i];
		void *addr;

		if (!(pte & PTE_VALID))
			continue;
		addr = arch_phys_to_virt(pte_phys(pte));
		if (pte & PTE_LEAF) {
			put_page(virt_to_page(addr));
		} else {
			free_user_entries(addr, SV39_ENTRIES);
			free_pages(virt_to_page(addr), 0);
		}
	}
}

void arch_pgd_free(struct arch_pgd *pgd)
{
	/* 高半区根项共享启动页表的子树。 */
	free_user_entries(&pgd->root, SV39_ENTRIES / 2);
	free_pages(virt_to_page(pgd), 0);
}

static const u64 *sv39_find_leaf(const struct sv39_table *root, uintptr_t va, u64 *pa)
{
	const struct sv39_table *table = root;

	if (!root || !canonical(va))
		return NULL;
	for (int level = SV39_LEVELS - 1; level >= 0; level--) {
		const u64 *entry = &table->entries[vpn_index(va, level)];
		u64 pte = *entry;

		if (!(pte & PTE_VALID))
			return NULL;
		if (pte & PTE_LEAF) {
			if (pa)
				*pa = pte_phys(pte) | (va & (sv39_level_size(level) - 1));
			return entry;
		}
		table = arch_phys_to_virt(pte_phys(pte));
	}
	return NULL;
}

bool sv39_lookup(const struct sv39_table *root, uintptr_t va, u64 *pa,
		 unsigned int *prot)
{
	const u64 *entry = sv39_find_leaf(root, va, pa);

	if (entry && prot)
		*prot = *entry & PTE_LEAF;
	return entry != NULL;
}

//这个函数查询某个用户虚拟地址在页表中映射到了哪一页，
//并可顺便告诉调用者该页的读、写、执行权限。
//它只查询，不分配内存，也不建立映射。
struct page *arch_pgd_user_page(const struct arch_pgd *pgd, uintptr_t va,
				unsigned int *access)
{
	const u64 *entry;
	u64 pte;

	//地址不在用户地址范围内，返回 NULL。
	if (va >= SV39_LOW_END)
		return NULL;
	entry = sv39_find_leaf(&pgd->root, va, NULL);
	if (!entry || !(*entry & PTE_USER))
		return NULL;
	pte = *entry;
	if (access) {
		*access = (pte & PTE_COW) ? PGD_COW : 0;
		if (pte & SV39_READ)
			*access |= VM_READ;
		if (pte & SV39_WRITE)
			*access |= VM_WRITE;
		if (pte & SV39_EXEC)
			*access |= VM_EXEC;
	}
	return virt_to_page(arch_phys_to_virt(pte_phys(pte)));
}

void arch_pgd_resolve_cow(struct arch_pgd *pgd, uintptr_t va, struct page *page)
{
	u64 *entry = (u64 *)sv39_find_leaf(&pgd->root, va, NULL);
	u64 flags;

	BUG_ON(va >= SV39_LOW_END || !entry || !(*entry & PTE_COW));
	flags = *entry & ~((PTE_PPN_MASK << PTE_PPN_SHIFT) | PTE_COW);
	*entry = pte_encode_pa(page_to_phys(page)) | flags | SV39_WRITE | PTE_DIRTY;
	asm volatile("sfence.vma %0, zero" : : "r"(va) : "memory");
	if (flags & SV39_EXEC)
		asm volatile("fence.i" : : : "memory");
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
