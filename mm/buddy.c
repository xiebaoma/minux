#include <minux/buddy.h>
#include <minux/limits.h>
#include <minux/memblock.h>
#include <minux/page.h>
#include "page_internal.h"

/* 每段只为实际存在的页建描述符，段之间的物理地址空洞不占空间。 */
struct page_bank {
	u64 start_pfn;
	u64 end_pfn;
	struct page *pages;
};

struct free_area {
	struct list_head blocks;
	u64 nr_blocks;
};

static struct page *page_map;
static u64 page_map_bytes;
static struct page_bank banks[MEMBLOCK_MAX_REGIONS];
static unsigned int nr_banks;
static struct free_area free_areas[BUDDY_MAX_ORDER + 1];

static const struct page_bank *find_bank(u64 pfn)
{
	for (unsigned int i = 0; i < nr_banks; i++) {
		if (pfn >= banks[i].start_pfn && pfn < banks[i].end_pfn)
			return &banks[i];
	}
	return NULL;
}

static void add_free_block(struct page *page, unsigned int order)
{
	struct free_area *area = &free_areas[order];

	page->state = PAGE_FREE_HEAD;
	page->order = order;
	list_add(&page->buddy_link, &area->blocks);
	area->nr_blocks++;
}

static void remove_free_block(struct page *page)
{
	list_del(&page->buddy_link);
	free_areas[page->order].nr_blocks--;
	/* 摘下的首帧可能在合并后变成块内页。 */
	page->state = PAGE_BLOCK_TAIL;
}

static bool seed_range(u64 start, u64 end, void *data)
{
	const struct page_bank *bank = find_bank(start);
	struct page *pages = bank->pages;

	(void)data;
	for (u64 pfn = start; pfn < end; pfn++)
		pages[pfn - bank->start_pfn].state = PAGE_BLOCK_TAIL;

	while (start < end) {
		unsigned int order = BUDDY_MAX_ORDER;
		u64 nr_pages = 1ULL << order;

		/* 每次选能完整放入剩余区间、且按绝对 PFN 对齐的最大块。 */
		while (nr_pages > end - start || (start & (nr_pages - 1))) {
			order--;
			nr_pages >>= 1;
		}
		add_free_block(&pages[start - bank->start_pfn], order);
		start += nr_pages;
	}
	return true;
}

bool buddy_init(struct memblock *map)
{
	u64 nr_pages = memblock_memory_pages(map);
	u64 bytes, phys;
	struct page *next;

	if (page_map || !nr_pages || nr_pages > SIZE_MAX / sizeof(*page_map))
		return false;
	bytes = nr_pages * sizeof(*page_map);
	/* 先预留描述符占用的整页，后续遍历才会将这些页排除。 */
	if (!memblock_alloc_pages(map, pfn_up(bytes), &phys))
		return false;
	page_map = arch_phys_to_virt(phys);
	page_map_bytes = bytes;
	next = page_map;

	for (unsigned int i = 0; i < map->memory.count; i++) {
		u64 start = pfn_up(map->memory.regions[i].start);
		u64 end = map->memory.regions[i].end / PAGE_SIZE;

		if (start >= end)
			continue;
		banks[nr_banks++] = (struct page_bank){ start, end, next };
		for (u64 pfn = start; pfn < end; pfn++)
			*next++ = (struct page){ .pfn = pfn, .state = PAGE_RESERVED };
	}
	for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; order++)
		INIT_LIST_HEAD(&free_areas[order].blocks);
	memblock_for_each_free_range(map, seed_range, NULL);
	return true;
}

struct page *alloc_pages(unsigned int order)
{
	unsigned int current = order;
	struct page *page;

	if (!page_map || order > BUDDY_MAX_ORDER)
		return NULL;
	while (current <= BUDDY_MAX_ORDER && list_empty(&free_areas[current].blocks))
		current++;
	if (current > BUDDY_MAX_ORDER)
		return NULL;

	page = list_first_entry(&free_areas[current].blocks, struct page, buddy_link);
	remove_free_block(page);
	/* 保留左半块继续拆分，右半块归还到刚降到的阶。 */
	while (current > order) {
		current--;
		add_free_block(page + (1ULL << current), current);
	}
	page->state = PAGE_ALLOCATED_HEAD;
	page->order = order;
	page->owner = PAGE_OWNER_BUDDY;
	return page;
}

bool free_pages(struct page *page, unsigned int order)
{
	uintptr_t base = (uintptr_t)page_map;
	uintptr_t addr = (uintptr_t)page;
	const struct page_bank *bank;

	/* 先验证描述符的地址，再读取块状态，失败时链表不变。 */
	if (order > BUDDY_MAX_ORDER || addr < base || addr - base >= page_map_bytes ||
	    (addr - base) % sizeof(*page))
		return false;
	if (page->state != PAGE_ALLOCATED_HEAD || page->order != order)
		return false;
	bank = find_bank(page->pfn);
	page->state = PAGE_BLOCK_TAIL;

	while (order < BUDDY_MAX_ORDER) {
		u64 buddy_pfn = page->pfn ^ (1ULL << order);
		struct page *buddy;

		/* 翻转阶数对应的 PFN 位找到同一父块的另一半。 */
		if (buddy_pfn < bank->start_pfn || buddy_pfn >= bank->end_pfn)
			break;
		buddy = &bank->pages[buddy_pfn - bank->start_pfn];
		if (buddy->state != PAGE_FREE_HEAD || buddy->order != order)
			break;
		remove_free_block(buddy);
		if (buddy_pfn < page->pfn)
			page = buddy;
		order++;
	}
	add_free_block(page, order);
	return true;
}

u64 page_to_phys(const struct page *page)
{
	return page->pfn * PAGE_SIZE;
}

void *page_address(const struct page *page)
{
	return arch_phys_to_virt(page_to_phys(page));
}

struct page *virt_to_page(const void *addr)
{
	const struct page_bank *bank;
	u64 phys, pfn;

	if (!arch_virt_to_phys(addr, &phys))
		return NULL;
	pfn = phys / PAGE_SIZE;
	bank = find_bank(pfn);
	return bank ? &bank->pages[pfn - bank->start_pfn] : NULL;
}

u64 buddy_free_pages(void)
{
	u64 pages = 0;

	for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; order++)
		pages += buddy_free_blocks(order) << order;
	return pages;
}

u64 buddy_free_blocks(unsigned int order)
{
	return order <= BUDDY_MAX_ORDER ? free_areas[order].nr_blocks : 0;
}
