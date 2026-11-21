#include <minux/buddy.h>
#include <minux/irq.h>
#include <minux/memblock.h>
#include <minux/page.h>
#include <minux/slab.h>
#include "test.h"

struct free_layout {
	u64 pages;
	u64 fitted_blocks[BUDDY_MAX_ORDER + 1];
};

static bool count_layout(u64 start, u64 end, void *data)
{
	struct free_layout *layout = data;

	layout->pages += end - start;
	for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; order++) {
		u64 size = 1ULL << order;
		u64 first = (start + size - 1) / size;
		u64 last = end / size;

		if (first < last)
			layout->fitted_blocks[order] += last - first;
	}
	return true;
}

static bool matches_layout(const struct free_layout *layout)
{
	if (buddy_free_pages() != layout->pages)
		return false;
	for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; order++) {
		u64 blocks = layout->fitted_blocks[order];

		/* 能放下的块中，扣掉已组成更高阶块的两个子块。 */
		if (order < BUDDY_MAX_ORDER)
			blocks -= 2 * layout->fitted_blocks[order + 1];
		if (buddy_free_blocks(order) != blocks)
			return false;
	}
	return true;
}

struct pfn_span {
	u64 start;
	u64 end;
	bool found;
};

static bool check_span(u64 start, u64 end, void *data)
{
	struct pfn_span *span = data;

	if (start > span->start || span->end > end)
		return true;
	span->found = true;
	return false;
}

static bool span_is_usable(u64 phys, u64 pages)
{
	struct pfn_span span = { .start = phys / PAGE_SIZE };

	span.end = span.start + pages;
	memblock_for_each_free_range(&boot_memblock, check_span, &span);
	return span.found;
}

static void test_block(const struct free_layout *layout)
{
	unsigned int order = BUDDY_MAX_ORDER;
	struct page *page;
	u64 phys, pages;

	check(!alloc_pages(BUDDY_MAX_ORDER + 1) && !free_pages(NULL, 0),
	      "buddy: reject an invalid order or a null page");
	while (order && !buddy_free_blocks(order))
		order--;
	page = alloc_pages(order);
	check(page != NULL, "buddy: allocate a block at the largest available order");
	if (!page)
		return;

	pages = 1ULL << order;
	phys = page_to_phys(page);
	check(phys % (pages * PAGE_SIZE) == 0 && span_is_usable(phys, pages),
	      "buddy: the whole block is aligned and inside one usable range");
	check(virt_to_page((char *)page_address(page) + PAGE_SIZE - 1) == page,
	      "buddy: mapped addresses resolve to their page, including interior bytes");
	check(buddy_free_pages() == layout->pages - pages,
	      "buddy: allocation accounts for every page in the block");
	check(!free_pages(page, order ? order - 1 : 1) &&
	      buddy_free_pages() == layout->pages - pages,
	      "buddy: wrong-order free leaves the allocator unchanged");
	check(free_pages(page, order) && !free_pages(page, order) && matches_layout(layout),
	      "buddy: free restores the layout and rejects a repeated free");
}

struct held_page {
	struct held_page *next;
	struct page *page;
	u64 number;
};

static void test_exhaustion(const struct free_layout *layout)
{
	struct held_page *head = NULL;
	struct page *tail_page = NULL;
	u64 allocated = 0;
	bool contents_ok = true;

	/* 页仍归测试持有时，用页里的空间串起所有分配结果。 */
	while (allocated < layout->pages) {
		struct page *page = alloc_pages(0);
		struct held_page *node;
		u64 phys;

		if (!page)
			break;
		phys = page_to_phys(page);
		if (phys % PAGE_SIZE || !span_is_usable(phys, 1)) {
			free_pages(page, 0);
			break;
		}
		if (!tail_page && (phys & PAGE_SIZE) && span_is_usable(phys - PAGE_SIZE, 2))
			tail_page = page;
		node = page_address(page);
		*node = (struct held_page){ head, page, ++allocated };
		head = node;
	}
	check(allocated == layout->pages && !buddy_free_pages() && !alloc_pages(0),
	      "buddy: split blocks until every usable page is allocated");
	check(!kmalloc(16) && !kmalloc(PAGE_SIZE),
	      "slab: small and large allocations fail when buddy is exhausted");

	/* 编号应按相反顺序读回，重复分配同一页会覆盖先前的编号。 */
	for (u64 number = allocated; number && head; number--) {
		struct held_page *node = head;

		head = node->next;
		if (node->number != number || !free_pages(node->page, 0)) {
			contents_ok = false;
			break;
		}
	}
	check(contents_ok && !head && matches_layout(layout),
	      "buddy: allocations were distinct and merging restored every order");
	check(tail_page && !free_pages(tail_page, 0) && matches_layout(layout),
	      "buddy: a former order-0 head is rejected after becoming a block tail");
}

static bool irq_state_preserved(unsigned long expected)
{
	unsigned long flags = arch_local_irq_save();

	arch_local_irq_restore(expected);
	return flags == expected;
}

static void test_irq_restore(const struct free_layout *layout)
{
	unsigned long initial_flags = arch_local_irq_save();

	for (unsigned int enabled = 0; enabled < 2; enabled++) {
		struct page *page;
		unsigned long flags;
		bool rejected, freed;
		u64 pages;

		/* 启动自测在设备中断初始化前运行，可临时打开总开关。 */
		if (enabled)
			arch_local_irq_enable();
		flags = arch_local_irq_save();
		arch_local_irq_restore(flags);

		page = alloc_pages(0);
		check(irq_state_preserved(flags) && page, "buddy: allocation preserves IRQ state");
		rejected = !free_pages(page, 1);
		check(irq_state_preserved(flags) && rejected, "buddy: wrong-order free preserves IRQ state");
		freed = free_pages(page, 0);
		check(irq_state_preserved(flags) && freed, "buddy: free preserves IRQ state");
		pages = buddy_free_pages();
		check(irq_state_preserved(flags) && pages == layout->pages,
		      "buddy: free-page count preserves IRQ state and matches the initial layout");
		buddy_free_blocks(0);
		check(irq_state_preserved(flags), "buddy: free-block count preserves IRQ state");
	}
	arch_local_irq_restore(initial_flags);
}

void test_buddy(void)
{
	struct free_layout layout = { 0 };

	memblock_for_each_free_range(&boot_memblock, count_layout, &layout);
	check(layout.pages && matches_layout(&layout),
	      "buddy: initial blocks cover exactly the post-reservation free ranges");
	test_block(&layout);
	test_exhaustion(&layout);
	test_irq_restore(&layout);
}
