#include <minux/buddy.h>
#include <minux/page.h>
#include <minux/slab.h>
#include <minux/string.h>
#include "page_internal.h"

/* 每个小对象 slab 至少包含三个槽位，页首留给元数据。 */
#define SLAB_MIN_SHIFT 4
#define SLAB_MAX_SHIFT (PAGE_SHIFT - 2)
#define SLAB_MIN_SIZE  (1UL << SLAB_MIN_SHIFT)
#define SLAB_MAX_SIZE  (1UL << SLAB_MAX_SHIFT)
#define SLAB_BITMAP_WORDS ((PAGE_SIZE / SLAB_MIN_SIZE + 63) / 64)

struct kmalloc_cache {
	struct list_head partial;
	size_t size;
	unsigned int capacity;
};

struct free_object {
	struct free_object *next;
};

struct slab {
	struct list_head link;
	struct kmalloc_cache *cache;
	struct free_object *freelist;
	unsigned int inuse;
	u64 allocated[SLAB_BITMAP_WORDS];
};

_Static_assert(sizeof(struct slab) <= SLAB_MAX_SIZE,
	       "slab header must fit in a quarter page");
_Static_assert(sizeof(struct free_object) <= SLAB_MIN_SIZE,
	       "free list link must fit in the smallest object");

static struct kmalloc_cache caches[SLAB_MAX_SHIFT - SLAB_MIN_SHIFT + 1];
static bool slab_ready;

void slab_init(void)
{
	if (slab_ready)
		return;
	for (unsigned int i = 0; i < sizeof(caches) / sizeof(caches[0]); i++) {
		struct kmalloc_cache *cache = &caches[i];

		cache->size = 1UL << (SLAB_MIN_SHIFT + i);
		cache->capacity = (PAGE_SIZE - sizeof(struct slab)) / cache->size;
		INIT_LIST_HEAD(&cache->partial);
	}
	slab_ready = true;
}

static char *slab_objects(struct slab *slab)
{
	/* 对象从页尾向前排，每个槽位自然对齐到缓存的对象大小。 */
	return (char *)slab + PAGE_SIZE - slab->cache->capacity * slab->cache->size;
}

static struct slab *new_slab(struct kmalloc_cache *cache)
{
	struct page *page = alloc_pages(0);
	struct slab *slab;
	char *objects;

	if (!page)
		return NULL;
	slab = page_address(page);
	memset(slab, 0, sizeof(*slab));
	slab->cache = cache;
	objects = slab_objects(slab);
	for (unsigned int i = cache->capacity; i > 0; i--) {
		struct free_object *object = (void *)(objects + (i - 1) * cache->size);

		object->next = slab->freelist;
		slab->freelist = object;
	}
	page->owner = PAGE_OWNER_SLAB;
	list_add(&slab->link, &cache->partial);
	return slab;
}

static void *alloc_large(size_t size)
{
	unsigned int order = 0;
	struct page *page;

	/* 用 (size - 1) 算出覆盖请求所需的最小阶，避免向上取整时溢出。 */
	for (size_t pages = (size - 1) / PAGE_SIZE; pages; pages >>= 1)
		order++;
	page = alloc_pages(order);
	if (!page)
		return NULL;
	page->owner = PAGE_OWNER_KMALLOC;
	return page_address(page);
}

void *kmalloc(size_t size)
{
	struct kmalloc_cache *cache = caches;
	struct slab *slab;
	struct free_object *object;
	unsigned int slot;

	if (!slab_ready || !size)
		return NULL;
	if (size > SLAB_MAX_SIZE)
		return alloc_large(size);

	while (cache->size < size)
		cache++;
	if (list_empty(&cache->partial)) {
		slab = new_slab(cache);
		if (!slab)
			return NULL;
	} else {
		slab = list_first_entry(&cache->partial, struct slab, link);
	}

	object = slab->freelist;
	slab->freelist = object->next;
	slot = ((char *)object - slab_objects(slab)) / cache->size;
	slab->allocated[slot / 64] |= 1ULL << (slot % 64);
	slab->inuse++;
	if (!slab->freelist)
		list_del(&slab->link);
	return object;
}

void *kzalloc(size_t size)
{
	void *ptr = kmalloc(size);

	if (ptr)
		memset(ptr, 0, size);
	return ptr;
}

static void free_object(struct page *page, struct slab *slab, const void *ptr)
{
	struct kmalloc_cache *cache = slab->cache;
	uintptr_t offset = (uintptr_t)ptr - (uintptr_t)slab_objects(slab);
	struct free_object *object;
	unsigned int slot;
	u64 mask;

	if (offset >= cache->capacity * cache->size || offset % cache->size)
		return;
	slot = offset / cache->size;
	mask = 1ULL << (slot % 64);
	/* 空闲对象仍位于有效槽位，检查位图后才能重新加入空闲链表。 */
	if (!(slab->allocated[slot / 64] & mask))
		return;

	slab->allocated[slot / 64] &= ~mask;
	if (!slab->freelist)
		list_add(&slab->link, &cache->partial);
	object = (void *)ptr;
	object->next = slab->freelist;
	slab->freelist = object;
	if (--slab->inuse == 0) {
		list_del(&slab->link);
		free_pages(page, 0);
	}
}

void kfree(const void *ptr)
{
	struct page *page;
	void *base;

	if (!ptr)
		return;
	page = virt_to_page(ptr);
	if (!page || page->state != PAGE_ALLOCATED_HEAD)
		return;

	base = page_address(page);
	if (page->owner == PAGE_OWNER_SLAB)
		free_object(page, base, ptr);
	else if (page->owner == PAGE_OWNER_KMALLOC && ptr == base)
		free_pages(page, page->order);
}
