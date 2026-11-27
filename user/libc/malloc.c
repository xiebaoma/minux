#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define HEAP_ALIGN      16UL
#define PAGE_BYTES      4096UL
#define ARENA_BYTES     (4 * PAGE_BYTES)
#define LARGE_THRESHOLD (ARENA_BYTES / 2)

struct arena {
	struct arena *prev;
	struct arena *next;
	size_t map_size;
	size_t pad;
};

struct block {
	struct arena *owner; /* 大块为 NULL，小/中块指向所属 arena。 */
	size_t size;         /* 小/中块为可用载荷字节数；大块为整段映射字节数。 */
	struct block *prev;  /* 同一 arena 内紧邻的前一块，首块为 NULL。 */
	unsigned long used;
};

_Static_assert(sizeof(struct arena) % HEAP_ALIGN == 0, "arena header alignment");
_Static_assert(sizeof(struct block) % HEAP_ALIGN == 0, "block header alignment");
_Static_assert(LARGE_THRESHOLD <= ARENA_BYTES - sizeof(struct arena) - sizeof(struct block),
	       "large threshold must fit inside one arena");

static struct arena *arenas;

static int size_overflows(size_t size)
{
	return size > (size_t)-1 - sizeof(struct block) - (PAGE_BYTES - 1);
}

static size_t align_up(size_t n, size_t align)
{
	return (n + align - 1) & ~(align - 1);
}

static struct block *next_block(const struct block *b)
{
	char *end = (char *)(b + 1) + b->size;
	char *limit = (char *)b->owner + b->owner->map_size;

	return end < limit ? (struct block *)end : NULL;
}

static void coalesce_next(struct block *b)
{
	struct block *after = next_block(b);
	struct block *beyond;

	if (!after || after->used)
		return;
	b->size += sizeof(struct block) + after->size;
	beyond = next_block(b);
	if (beyond)
		beyond->prev = b;
}

static void split_block(struct block *b, size_t needed)
{
	struct block *after, *rem;

	if (b->size - needed < sizeof(struct block) + HEAP_ALIGN)
		return;
	after = next_block(b);
	rem = (struct block *)((char *)(b + 1) + needed);
	rem->owner = b->owner;
	rem->size = b->size - needed - sizeof(struct block);
	rem->prev = b;
	rem->used = 0;
	b->size = needed;
	if (after)
		after->prev = rem;
	coalesce_next(rem);
}

static void *alloc_large(size_t size)
{
	size_t map_size = align_up(sizeof(struct block) + size, PAGE_BYTES);
	struct block *b = mmap(NULL, map_size, PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (b == MAP_FAILED)
		return NULL;
	b->owner = NULL;
	b->size = map_size;
	b->prev = NULL;
	b->used = 1;
	return b + 1;
}

static struct block *grow_arena(void)
{
	struct arena *a = mmap(NULL, ARENA_BYTES, PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	struct block *first;

	if (a == MAP_FAILED)
		return NULL;
	a->prev = NULL;
	a->next = arenas;
	a->map_size = ARENA_BYTES;
	a->pad = 0;
	if (arenas)
		arenas->prev = a;
	arenas = a;

	first = (struct block *)(a + 1);
	first->owner = a;
	first->size = ARENA_BYTES - sizeof(*a) - sizeof(*first);
	first->prev = NULL;
	first->used = 0;
	return first;
}

void *malloc(size_t size)
{
	size_t aligned;
	struct block *b;

	if (!size)
		return NULL;
	if (size_overflows(size)) {
		errno = ENOMEM;
		return NULL;
	}
	aligned = align_up(size, HEAP_ALIGN);
	if (aligned > LARGE_THRESHOLD)
		return alloc_large(aligned);

	for (struct arena *a = arenas; a; a = a->next) {
		for (b = (struct block *)(a + 1); b; b = next_block(b)) {
			if (!b->used && b->size >= aligned) {
				split_block(b, aligned);
				b->used = 1;
				return b + 1;
			}
		}
	}
	b = grow_arena();
	if (!b)
		return NULL;
	split_block(b, aligned);
	b->used = 1;
	return b + 1;
}

void free(void *ptr)
{
	struct block *b;
	struct arena *a;

	if (!ptr)
		return;
	b = (struct block *)ptr - 1;
	a = b->owner;
	//大块直接归还
	if (!a) {
		munmap(b, b->size);
		return;
	}

	b->used = 0;
	coalesce_next(b);
	if (b->prev && !b->prev->used) {
		b = b->prev;
		coalesce_next(b);
	}
	if (!b->prev && !next_block(b)) {
		if (a->prev)
			a->prev->next = a->next;
		else
			arenas = a->next;
		if (a->next)
			a->next->prev = a->prev;
		munmap(a, a->map_size);
	}
}

void *realloc(void *ptr, size_t size)
{
	struct block *b, *after;
	size_t aligned, copy_len;
	void *dst;

	if (!ptr)
		return malloc(size);
	if (!size) {
		free(ptr);
		return NULL;
	}
	if (size_overflows(size)) {
		errno = ENOMEM;
		return NULL;
	}
	aligned = align_up(size, HEAP_ALIGN);
	b = (struct block *)ptr - 1;

	if (!b->owner) {
		size_t old_cap = b->size - sizeof(struct block);
		size_t new_map = align_up(sizeof(struct block) + aligned, PAGE_BYTES);

		if (new_map <= b->size) {
			if (new_map < b->size &&
			    munmap((char *)b + new_map, b->size - new_map) == 0)
				b->size = new_map;
			return ptr;
		}
		dst = malloc(size);
		if (!dst)
			return NULL;
		memcpy(dst, ptr, old_cap);
		free(ptr);
		return dst;
	}

	if (aligned <= b->size) {
		split_block(b, aligned);
		return ptr;
	}
	after = next_block(b);
	if (after && !after->used &&
	    b->size + sizeof(struct block) + after->size >= aligned) {
		coalesce_next(b);
		split_block(b, aligned);
		return ptr;
	}

	copy_len = b->size;
	dst = malloc(size);
	if (!dst)
		return NULL;
	memcpy(dst, ptr, copy_len);
	free(ptr);
	return dst;
}
