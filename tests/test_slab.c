#include <minux/buddy.h>
#include <minux/irq.h>
#include <minux/limits.h>
#include <minux/page.h>
#include <minux/slab.h>
#include <minux/string.h>
#include "test.h"

static bool all_bytes(const u8 *ptr, size_t size, u8 value)
{
	for (size_t i = 0; i < size; i++) {
		if (ptr[i] != value)
			return false;
	}
	return true;
}

static void test_small_classes(void)
{
	for (size_t size = 16; size <= PAGE_SIZE / 4; size <<= 1) {
		size_t request = size == 16 ? 1 : size / 2 + 1;
		u8 *first = kmalloc(request);
		u8 *second = kmalloc(size);

		check(first && second, "slab: allocate both ends of a small size class");
		if (first && second) {
			check(first != second && virt_to_page(first) == virt_to_page(second) &&
			      (uintptr_t)first % size == 0 && (uintptr_t)second % size == 0,
			      "slab: same class shares a page with distinct aligned slots");
			memset(first, 0xa5, request);
			memset(second, 0x5a, size);
			check(all_bytes(first, request, 0xa5) && all_bytes(second, size, 0x5a),
			      "slab: writes across both allocations stay independent");
		}
		kfree(first);
		kfree(second);
	}
}

static void test_reuse(void)
{
	size_t size = PAGE_SIZE / 4;
	u64 before = buddy_free_pages();
	u8 *objects[4];

	for (size_t i = 0; i < sizeof(objects) / sizeof(objects[0]); i++)
		objects[i] = kmalloc(size);
	check(objects[0] && objects[1] && objects[2] && objects[3],
	      "slab: allocate enough objects to fill a slab and open another");
	if (objects[0] && objects[1] && objects[2] && objects[3]) {
		struct page *first = virt_to_page(objects[0]);
		u8 *reused, *extra;

		check(first == virt_to_page(objects[1]) && first == virt_to_page(objects[2]) &&
		      first != virt_to_page(objects[3]) && buddy_free_pages() == before - 2,
		      "slab: a full slab stays live while allocations use another page");
		memset(objects[1], 0xa5, size);
		kfree(objects[1] + 1);
		check(all_bytes(objects[1], size, 0xa5),
		      "slab: an interior pointer does not free an object");
		kfree(objects[1]);
		kfree(objects[1]);
		reused = kzalloc(size);
		extra = kmalloc(size);
		check(reused == objects[1] && all_bytes(reused, size, 0) && extra &&
		      extra != reused && virt_to_page(extra) != first,
		      "slab: freeing a slot makes a full slab reusable exactly once");
		objects[1] = reused;
		kfree(extra);
	}
	for (size_t i = 0; i < sizeof(objects) / sizeof(objects[0]); i++)
		kfree(objects[i]);
	check(buddy_free_pages() == before, "slab: empty slabs return their pages to buddy");
}

static void test_large_allocations(void)
{
	static const struct {
		size_t size;
		u64 pages;
	} cases[] = {
		{ PAGE_SIZE / 4 + 1, 1 },
		{ PAGE_SIZE, 1 },
		{ PAGE_SIZE + 1, 2 },
		{ 2 * PAGE_SIZE + 1, 4 },
		{ PAGE_SIZE << BUDDY_MAX_ORDER, 1ULL << BUDDY_MAX_ORDER },
	};
	u64 before = buddy_free_pages();

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		size_t size = cases[i].size;
		u8 *ptr = kmalloc(size);

		check(ptr && (uintptr_t)ptr % PAGE_SIZE == 0 &&
		      buddy_free_pages() == before - cases[i].pages,
		      "slab: large allocations use the expected buddy order");
		if (ptr) {
			ptr[0] = 0xa5;
			ptr[size - 1] = 0x5a;
			kfree(ptr + 1);
			if (size > PAGE_SIZE)
				kfree(ptr + PAGE_SIZE);
			check(buddy_free_pages() == before - cases[i].pages &&
			      ptr[0] == 0xa5 && ptr[size - 1] == 0x5a,
			      "slab: only the start of a large allocation may be freed");
			kfree(ptr);
			kfree(ptr);
		}
		check(buddy_free_pages() == before, "slab: large free returns the entire block");
	}
}

static void test_irq_restore(void)
{
	const size_t sizes[] = { 16, PAGE_SIZE };
	unsigned long initial_flags = arch_local_irq_save();
	u64 before = buddy_free_pages();

	for (unsigned int enabled = 0; enabled < 2; enabled++) {
		unsigned long expected;

		/* 此时设备中断尚未初始化，可临时打开总开关。 */
		if (enabled)
			arch_local_irq_enable();
		expected = arch_local_irq_save();
		arch_local_irq_restore(expected);

		for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
			void *ptr = kmalloc(sizes[i]);
			unsigned long flags = arch_local_irq_save();

			arch_local_irq_restore(expected);
			check(ptr && flags == expected && buddy_free_pages() + 1 == before,
			      "slab: taking a buddy page preserves IRQ state");
			kfree(ptr);
			flags = arch_local_irq_save();
			arch_local_irq_restore(expected);
			check(flags == expected && buddy_free_pages() == before,
			      "slab: returning a buddy page preserves IRQ state");
		}
	}
	arch_local_irq_restore(initial_flags);
}

void test_slab(void)
{
	u64 blocks[BUDDY_MAX_ORDER + 1];
	struct page *raw_page;
	bool restored = true;

	for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; order++)
		blocks[order] = buddy_free_blocks(order);
	check(!kmalloc(0) && !kzalloc(0) && !kmalloc(SIZE_MAX) &&
	      !kmalloc((PAGE_SIZE << BUDDY_MAX_ORDER) + 1),
	      "slab: reject zero and oversized requests");
	kfree(NULL);
	kfree(blocks);
	raw_page = alloc_pages(0);
	if (raw_page)
		kfree(page_address(raw_page));
	check(raw_page && free_pages(raw_page, 0),
	      "slab: kfree leaves a page owned by a buddy caller alone");

	test_small_classes();
	test_reuse();
	test_large_allocations();
	test_irq_restore();
	for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; order++)
		restored &= blocks[order] == buddy_free_blocks(order);
	check(restored, "slab: buddy free blocks return to their original counts at every order");
}
