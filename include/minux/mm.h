#ifndef _MINUX_MM_H
#define _MINUX_MM_H

#include <minux/list.h>

struct arch_pgd;
struct page;

enum vm_flags {
	VM_READ  = 1U << 0,
	VM_WRITE = 1U << 1,
	VM_EXEC  = 1U << 2,
	VM_ANON  = 1U << 3, /* 缺页时可以分配零页。 */
};

#define VM_ACCESS_MASK (VM_READ | VM_WRITE | VM_EXEC)

struct mm_struct {
	struct arch_pgd *pgd;
	struct list_head vmas;
};

struct vm_area_struct {
	uintptr_t vm_start;			//两个端点必须按 4KB 对齐
	uintptr_t vm_end;
	unsigned int vm_flags;
	struct mm_struct *vm_mm;
	struct list_head vm_link;
};

/* 在 buddy 和 slab 就绪后使用。登记 VMA 和销毁时地址空间须未启用。 */
struct mm_struct *mm_alloc(void);
struct mm_struct *mm_dup(struct mm_struct *src);
void mm_destroy(struct mm_struct *mm);

/* 登记页对齐的 [start, end)。可写区间必须同时可读。返回的 VMA 由 mm 持有。 */
const struct vm_area_struct *mm_add_vma(struct mm_struct *mm, uintptr_t start,
				       uintptr_t end, unsigned int flags);
/* 返回第一个满足 addr < vm_end 的 VMA，找不到时返回 NULL。 */
const struct vm_area_struct *find_vma(const struct mm_struct *mm, uintptr_t addr);

/* 为 VMA 内未映射的页首地址分配零页。成功后页归 mm 持有，失败返回 NULL。 */
struct page *mm_populate_page(struct mm_struct *mm, uintptr_t addr);

/* 处理合法匿名缺页或写时复制。成功后可重试指令。 */
bool mm_handle_user_fault(struct mm_struct *mm, uintptr_t addr, unsigned int access);
/* 借用可按 access 访问的用户页，必要时补匿名页或完成写时复制。页的所有权仍归 mm。 */
struct page *mm_user_page(struct mm_struct *mm, uintptr_t addr, unsigned int access);

#endif /* _MINUX_MM_H */
