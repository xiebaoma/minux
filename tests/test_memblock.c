#include <minux/limits.h>
#include <minux/memblock.h>
#include <minux/page.h>
#include "test.h"

struct free_ranges {
	struct memblock_region ranges[3];
	unsigned int count;
	unsigned int stop_after;
};

static bool capture_free_range(u64 start, u64 end, void *data)
{
	struct free_ranges *result = data;

	if (result->count < 3)
		result->ranges[result->count] = (struct memblock_region){ start, end };
	result->count++;
	return result->count != result->stop_after;
}

static void test_free_ranges(void)
{
	struct memblock map = { 0 };
	struct free_ranges free = { 0 };

	check(memblock_add(&map, 8 * PAGE_SIZE + 1, 5 * PAGE_SIZE + 1) &&
	      memblock_add(&map, 3 * PAGE_SIZE + 1, 2 * PAGE_SIZE + 1) &&
	      memblock_add(&map, PAGE_SIZE + 1, 2 * PAGE_SIZE) &&
	      memblock_add(&map, 2 * PAGE_SIZE, 2 * PAGE_SIZE),
	      "memblock: add out-of-order, adjacent and overlapping RAM");
	check(map.memory.count == 2 && memblock_memory_pages(&map) == 7,
	      "memblock: merge RAM before rounding to full pages");
	check(memblock_reserve(&map, 11 * PAGE_SIZE, 1) &&
	      memblock_reserve(&map, 2 * PAGE_SIZE - 1, 2) &&
	      memblock_reserve(&map, 2 * PAGE_SIZE, 4) &&
	      memblock_reserve(&map, 6 * PAGE_SIZE, PAGE_SIZE) &&
	      memblock_reserve(&map, 7 * PAGE_SIZE + 1, 2 * PAGE_SIZE),
	      "memblock: add overlapping and unaligned reservations");
	memblock_for_each_free_range(&map, capture_free_range, &free);
	check(free.count == 3 &&
	      free.ranges[0].start == 3 && free.ranges[0].end == 5 &&
	      free.ranges[1].start == 10 && free.ranges[1].end == 11 &&
	      free.ranges[2].start == 12 && free.ranges[2].end == 13,
	      "memblock: subtract reservations across RAM banks and holes");

	free.count = 0;
	free.stop_after = 1;
	memblock_for_each_free_range(&map, capture_free_range, &free);
	check(free.count == 1 && free.ranges[0].end == 5,
	      "memblock: a visitor can stop at the end of a RAM bank");

	free.count = 0;
	free.stop_after = 2;
	memblock_for_each_free_range(&map, capture_free_range, &free);
	check(free.count == 2 && free.ranges[1].end == 11,
	      "memblock: a visitor can stop before a reserved range");
}

static void test_range_limits(void)
{
	struct memblock map = { 0 };
	struct free_ranges free = { 0 };
	u64 top = ULLONG_MAX;
	bool filled = true;

	check(!memblock_add(&map, 0, 0) && !memblock_reserve(&map, top, 1),
	      "memblock: reject empty or overflowing ranges");
	check(memblock_add(&map, top - 2 * PAGE_SIZE + 1, 2 * PAGE_SIZE - 1) &&
	      memblock_reserve(&map, top - 2 * PAGE_SIZE + 1, 2 * PAGE_SIZE - 1),
	      "memblock: record ranges that end at the maximum address");
	memblock_for_each_free_range(&map, capture_free_range, &free);
	check(free.count == 0, "memblock: round high reservations without overflow");

	map.memory.count = 0;
	for (unsigned int i = 0; i < MEMBLOCK_MAX_REGIONS; i++)
		filled &= memblock_add(&map, 2 * i * PAGE_SIZE, PAGE_SIZE);
	check(filled && !memblock_add(&map, 2 * MEMBLOCK_MAX_REGIONS * PAGE_SIZE,
				      PAGE_SIZE) &&
	      memblock_memory_pages(&map) == MEMBLOCK_MAX_REGIONS,
	      "memblock: reject an insertion into a full table without losing RAM");
	check(memblock_add(&map, PAGE_SIZE, PAGE_SIZE) &&
	      map.memory.count == MEMBLOCK_MAX_REGIONS - 1 &&
	      memblock_memory_pages(&map) == MEMBLOCK_MAX_REGIONS + 1,
	      "memblock: merge existing ranges even when the table is full");
}

static void test_alloc_pages(void)
{
	struct memblock map = { 0 };
	struct free_ranges free = { 0 };
	u64 phys = ULLONG_MAX;

	check(memblock_add(&map, 0, 6 * PAGE_SIZE) &&
	      memblock_reserve(&map, 2 * PAGE_SIZE + 1, 1),
	      "memblock: set up startup page allocation");
	check(memblock_alloc_pages(&map, 2, &phys) && phys == 3 * PAGE_SIZE,
	      "memblock: allocate whole pages beyond a partial-page reservation");
	memblock_for_each_free_range(&map, capture_free_range, &free);
	check(free.count == 2 &&
	      free.ranges[0].start == 0 && free.ranges[0].end == 2 &&
	      free.ranges[1].start == 5 && free.ranges[1].end == 6,
	      "memblock: allocated pages are excluded from free ranges");
	check(!memblock_alloc_pages(&map, 0, &phys) &&
	      !memblock_alloc_pages(&map, 2, &phys) && phys == 3 * PAGE_SIZE,
	      "memblock: failed startup allocation leaves the result untouched");
}

void test_memblock(void)
{
	test_free_ranges();
	test_range_limits();
	test_alloc_pages();
}
