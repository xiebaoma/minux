#include <asm/page.h>
#include <minux/limits.h>
#include <minux/memblock.h>
#include <minux/string.h>

struct memblock boot_memblock;

static bool add_range(struct memblock_type *type, u64 start, u64 size)
{
	struct memblock_region *regions = type->regions;
	unsigned int first = 0, last;
	u64 end;

	if (!size || start > ULLONG_MAX - size)
		return false;
	end = start + size;

	while (first < type->count && regions[first].end < start)
		first++;
	/* [first, last) 是与新区间重叠或相邻、需要合并的表项。 */
	for (last = first; last < type->count && regions[last].start <= end;
	     last++) {
		if (regions[last].start < start)
			start = regions[last].start;
		if (regions[last].end > end)
			end = regions[last].end;
	}
	if (first == last && type->count == MEMBLOCK_MAX_REGIONS)
		return false;
	if (last < type->count)
		memmove(&regions[first + 1], &regions[last],
			(type->count - last) * sizeof(*regions));
	regions[first] = (struct memblock_region){ start, end };
	type->count = type->count - (last - first) + 1;
	return true;
}

bool memblock_add(struct memblock *map, u64 start, u64 size)
{
	return add_range(&map->memory, start, size);
}

bool memblock_reserve(struct memblock *map, u64 start, u64 size)
{
	return add_range(&map->reserved, start, size);
}

u64 memblock_memory_pages(const struct memblock *map)
{
	u64 pages = 0;

	for (unsigned int i = 0; i < map->memory.count; i++) {
		const struct memblock_region *ram = &map->memory.regions[i];
		u64 start = pfn_up(ram->start);
		u64 end = ram->end / PAGE_SIZE;

		if (start < end)
			pages += end - start;
	}
	return pages;
}

void memblock_for_each_free_range(const struct memblock *map,
				  memblock_free_visitor visit, void *data)
{
	for (unsigned int i = 0; i < map->memory.count; i++) {
		const struct memblock_region *ram = &map->memory.regions[i];
		/* RAM 只计完整的页，保留区覆盖与其相交的每一页。 */
		u64 cursor = pfn_up(ram->start);
		u64 end = ram->end / PAGE_SIZE;

		for (unsigned int j = 0; j < map->reserved.count && cursor < end;
		     j++) {
			const struct memblock_region *res = &map->reserved.regions[j];
			u64 res_start = res->start / PAGE_SIZE;
			u64 res_end = pfn_up(res->end);

			if (res_start >= end)
				break;
			if (res_end <= cursor)
				continue;
			if (cursor < res_start && !visit(cursor, res_start, data))
				return;
			cursor = res_end;
		}
		if (cursor < end && !visit(cursor, end, data))
			return;
	}
}

struct boot_page_request {
	u64 nr_pages;
	u64 start_pfn;
};

static bool find_boot_pages(u64 start, u64 end, void *data)
{
	struct boot_page_request *request = data;

	/* 申请结果会用作启动期指针，因此跳过物理地址 0。 */
	if (!start)
		start = 1;
	if (start >= end || request->nr_pages > end - start)
		return true;
	request->start_pfn = start;
	return false;
}

bool memblock_alloc_pages(struct memblock *map, u64 nr_pages, u64 *phys)
{
	struct boot_page_request request = { .nr_pages = nr_pages };
	u64 start;

	if (!nr_pages)
		return false;
	/* 找到首个合适区间即结束遍历，再插入保留区。 */
	memblock_for_each_free_range(map, find_boot_pages, &request);
	if (!request.start_pfn)
		return false;
	start = request.start_pfn * PAGE_SIZE;
	if (!memblock_reserve(map, start, nr_pages * PAGE_SIZE))
		return false;
	*phys = start;
	return true;
}
