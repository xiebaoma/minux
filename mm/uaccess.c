#include <minux/buddy.h>
#include <minux/errno.h>
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

int copy_to_user_exact(uintptr_t to, const void *from, size_t size)
{
	struct mm_struct *mm = task_current()->mm;
	uintptr_t limit = arch_user_limit();

	if (!size)
		return 0;
	if (!mm || to >= limit || size > limit - to)
		return -EFAULT;
	/* 当前任务独占 mm，预先解决缺页和 COW 后，复制期间映射保持有效。 */
	for (uintptr_t addr = to; addr < to + size; addr = (addr & ~(PAGE_SIZE - 1)) + PAGE_SIZE)
		if (!mm_user_page(mm, addr, VM_WRITE))
			return -EFAULT;
	return copy_to_user(to, from, size) ? -EFAULT : 0;
}

/* 按页读取，遇到 NUL 即停止，字符串末尾之后的页可以未映射。 */
long copy_string_from_user(char *to, uintptr_t from, size_t limit)
{
	struct mm_struct *mm = task_current()->mm;
	size_t copied = 0;

	if (!mm)
		return -EFAULT;
	while (copied < limit) {
		size_t offset = from % PAGE_SIZE;
		size_t count = PAGE_SIZE - offset;
		struct page *page = mm_user_page(mm, from, VM_READ);
		const char *src;

		if (!page)
			return -EFAULT;
		src = (const char *)page_address(page) + offset;
		if (count > limit - copied)
			count = limit - copied;
		for (size_t i = 0; i < count; i++) {
			to[copied++] = src[i];
			if (!src[i])
				return copied;
		}
		from += count;
	}
	return -E2BIG;
}
