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

const struct vm_area_struct *mm_add_vma(struct mm_struct *mm, uintptr_t start,
				       uintptr_t end, unsigned int flags)
{
	struct list_head *pos;
	struct vm_area_struct *vma;

	if (start < PAGE_SIZE || start >= end || end > arch_user_limit() ||
	    (start | end) % PAGE_SIZE || !flags ||
	    (flags & ~(VM_READ | VM_WRITE | VM_EXEC)) ||
	    ((flags & VM_WRITE) && !(flags & VM_READ)))
		return NULL;
	list_for_each(pos, &mm->vmas) {
		vma = list_entry(pos, struct vm_area_struct, vm_link);
		if (end <= vma->vm_start)
			break;
		if (start < vma->vm_end)
			return NULL;
	}
	vma = kmalloc(sizeof(*vma));
	if (!vma)
		return NULL;
	vma->vm_start = start;
	vma->vm_end = end;
	vma->vm_flags = flags;
	vma->vm_mm = mm;
	list_add_tail(&vma->vm_link, pos);
	return vma;
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

struct page *mm_populate_page(struct mm_struct *mm, uintptr_t addr)
{
	const struct vm_area_struct *vma;
	struct page *page;

	if (addr % PAGE_SIZE)
		return NULL;
	vma = find_vma(mm, addr);
	if (!vma || addr < vma->vm_start)
		return NULL;
	page = alloc_pages(0);
	if (!page)
		return NULL;
	memset(page_address(page), 0, PAGE_SIZE);
	if (!arch_pgd_map(mm->pgd, addr, page, vma->vm_flags)) {
		free_pages(page, 0);
		return NULL;
	}
	return page;
}
