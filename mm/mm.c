#include <minux/buddy.h>
#include <minux/mm.h>
#include <minux/page.h>
#include <minux/paging.h>
#include <minux/slab.h>
#include <minux/string.h>

struct mm_struct *mm_alloc(void)
{
	struct mm_struct *mm = kzalloc(sizeof(*mm));

	if (!mm)
		return NULL;
	mm->pgd = arch_pgd_alloc();
	if (!mm->pgd) {
		kfree(mm);
		return NULL;
	}
	INIT_LIST_HEAD(&mm->vmas);
	return mm;
}

void mm_destroy(struct mm_struct *mm)
{
	struct vm_area_struct *vma, *next;

	if (!mm)
		return;
	arch_pgd_free(mm->pgd);
	list_for_each_entry_safe(vma, next, &mm->vmas, vm_link) {
		list_del(&vma->vm_link);
		kfree(vma);
	}
	kfree(mm);
}

static struct vm_area_struct *insert_vma(struct mm_struct *mm, struct list_head *pos,
					uintptr_t start, uintptr_t end, unsigned int flags)
{
	struct vm_area_struct *vma = kmalloc(sizeof(*vma));

	if (!vma)
		return NULL;
	vma->vm_start = start;
	vma->vm_end = end;
	vma->vm_flags = flags;
	vma->vm_mm = mm;
	list_add_tail(&vma->vm_link, pos);
	return vma;
}

struct mm_struct *mm_dup(struct mm_struct *src)
{
	struct mm_struct *dst = mm_alloc();
	const struct vm_area_struct *vma;

	if (!dst)
		return NULL;
	list_for_each_entry(vma, &src->vmas, vm_link) {
		/* 源链表已按地址排序，可直接追加到副本。 */
		if (!insert_vma(dst, &dst->vmas, vma->vm_start, vma->vm_end, vma->vm_flags))
			goto fail;
	}
	if (!arch_pgd_fork(dst->pgd, src->pgd))
		goto fail;
	return dst;
fail:
	mm_destroy(dst);
	return NULL;
}

const struct vm_area_struct *mm_add_vma(struct mm_struct *mm, uintptr_t start,
				       uintptr_t end, unsigned int flags)
{
	struct list_head *pos;
	struct vm_area_struct *vma;

	if (start < PAGE_SIZE || start >= end || end > arch_user_limit() ||
	    (start | end) % PAGE_SIZE || !(flags & VM_ACCESS_MASK) ||
	    (flags & ~(VM_ACCESS_MASK | VM_ANON)) ||
	    ((flags & VM_WRITE) && !(flags & VM_READ)))
		return NULL;
	list_for_each(pos, &mm->vmas) {
		vma = list_entry(pos, struct vm_area_struct, vm_link);
		if (end <= vma->vm_start)
			break;
		if (start < vma->vm_end)
			return NULL;
	}
	return insert_vma(mm, pos, start, end, flags);
}

const struct vm_area_struct *find_vma(const struct mm_struct *mm, uintptr_t addr)
{
	const struct vm_area_struct *vma;

	list_for_each_entry(vma, &mm->vmas, vm_link) {
		if (addr < vma->vm_end)
			return vma;
	}
	return NULL;
}

static struct page *populate_vma_page(const struct vm_area_struct *vma, uintptr_t addr)
{
	struct page *page = alloc_pages(0);

	if (!page)
		return NULL;
	memset(page_address(page), 0, PAGE_SIZE);
	if (!arch_pgd_map(vma->vm_mm->pgd, addr, page, vma->vm_flags & VM_ACCESS_MASK)) {
		free_pages(page, 0);
		return NULL;
	}
	return page;
}

struct page *mm_populate_page(struct mm_struct *mm, uintptr_t addr)
{
	const struct vm_area_struct *vma;

	if (addr % PAGE_SIZE)
		return NULL;
	vma = find_vma(mm, addr);
	if (!vma || addr < vma->vm_start)
		return NULL;
	return populate_vma_page(vma, addr);
}

// 在进程要写一个写时复制页时，给这个进程准备可写的映射。
static struct page *resolve_cow(struct mm_struct *mm, uintptr_t addr, struct page *old)
{
	struct page *page = old;

	if (page_ref_count(old) > 1) {
		page = alloc_pages(0);
		if (!page)
			return NULL;
		memcpy(page_address(page), page_address(old), PAGE_SIZE);
	}
	//修改页表项
	arch_pgd_resolve_cow(mm->pgd, addr, page);
	if (page != old)
		put_page(old);
	return page;
}

static struct page *resolve_user_page(struct mm_struct *mm, uintptr_t addr,
				      unsigned int access, bool fault)
{
	const struct vm_area_struct *vma = find_vma(mm, addr);
	unsigned int mapped_access;
	struct page *page;

	if (!vma || addr < vma->vm_start || !access || (access & ~VM_ACCESS_MASK) ||
	    (vma->vm_flags & access) != access)
		return NULL;
	page = arch_pgd_user_page(mm->pgd, addr, &mapped_access);
	if (page && (access & VM_WRITE) && (mapped_access & PGD_COW) &&
	    ((mapped_access | VM_WRITE) & access) == access)
		return resolve_cow(mm, addr, page);
	if (page)
		return !fault && (mapped_access & access) == access ? page : NULL;
	if (!(vma->vm_flags & VM_ANON))
		return NULL;
	return populate_vma_page(vma, addr & ~(PAGE_SIZE - 1));
}

bool mm_handle_user_fault(struct mm_struct *mm, uintptr_t addr, unsigned int access)
{
	return resolve_user_page(mm, addr, access, true) != NULL;
}

//内核主动访问用户内存，例如 copy_from_user()	
//权限符合就返回已有页	struct page *，供内核访问该页
struct page *mm_user_page(struct mm_struct *mm, uintptr_t addr, unsigned int access)
{
	return resolve_user_page(mm, addr, access, false);
}
