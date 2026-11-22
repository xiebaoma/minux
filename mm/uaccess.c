#include <minux/buddy.h>
#include <minux/mm.h>
#include <minux/page.h>
#include <minux/paging.h>
#include <minux/string.h>
#include <minux/task.h>
#include <minux/uaccess.h>

static size_t copy_user(void *to, const void *from, size_t size, bool to_user)
{
	struct mm_struct *mm = task_current()->mm;
	uintptr_t addr = (uintptr_t)(to_user ? to : from);
	uintptr_t limit = arch_user_limit();
	unsigned int access = to_user ? VM_WRITE : VM_READ;

	/* 用减法检查整个区间，避免 addr + size 溢出。 */
	if (!size || !mm || addr >= limit || size > limit - addr)
		return size;

	while (size) {
		size_t offset = addr & (PAGE_SIZE - 1);
		size_t chunk = PAGE_SIZE - offset;
		struct page *page = mm_user_page(mm, addr, access);
		char *mapped;

		if (!page)
			break;
		if (chunk > size)
			chunk = size;
		/* 只访问检查过权限的物理页在线性映射中的地址。 */
		mapped = (char *)page_address(page) + offset;
		if (to_user) {
			memcpy(mapped, from, chunk);
			from = (const char *)from + chunk;
		} else {
			memcpy(to, mapped, chunk);
			to = (char *)to + chunk;
		}
		addr += chunk;
		size -= chunk;
	}
	return size;
}

size_t copy_from_user(void *to, uintptr_t from, size_t size)
{
	size_t remaining = copy_user(to, (const void *)from, size, false);

	if (remaining)
		memset((char *)to + (size - remaining), 0, remaining);
	return remaining;
}

size_t copy_to_user(uintptr_t to, const void *from, size_t size)
{
	return copy_user((void *)to, from, size, true);
}
