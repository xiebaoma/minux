#include <asm/elf.h>
#include <minux/buddy.h>
#include <minux/elf.h>
#include <minux/errno.h>
#include <minux/mm.h>
#include <minux/page.h>
#include <minux/paging.h>
#include <minux/string.h>

#define PT_LOAD   1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_TLS    7
#define PF_X      1
#define PF_W      2
#define PF_R      4
#define STACK_PAGES 8

_Static_assert(sizeof(struct elf64_ehdr) == 64, "ELF64 header layout");
_Static_assert(sizeof(struct elf64_phdr) == 56, "ELF64 program header layout");

static int load_segment(struct mm_struct *mm, const u8 *file, size_t size,
			const struct elf64_phdr *ph, uintptr_t limit, uintptr_t *previous_end)
{
	uintptr_t start = ph->vaddr & ~(PAGE_SIZE - 1);
	uintptr_t end;
	unsigned int access = 0;

	if (ph->filesz > ph->memsz || ph->offset > size || ph->filesz > size - ph->offset ||
	    ph->vaddr > limit || ph->memsz > limit - ph->vaddr ||
	    ((ph->offset ^ ph->vaddr) & (PAGE_SIZE - 1)) ||
	    (ph->align && ((ph->align & (ph->align - 1)) ||
			   ((ph->offset ^ ph->vaddr) & (ph->align - 1)))))
		return -ENOEXEC;
	if (!ph->memsz)
		return 0;
	if (ph->flags & PF_R)
		access |= VM_READ;
	if (ph->flags & PF_W)
		access |= VM_WRITE;
	if (ph->flags & PF_X)
		access |= VM_EXEC;
	if (start < *previous_end || !access || ((access & VM_WRITE) && !(access & VM_READ)))
		return -ENOEXEC;
	end = pfn_up(ph->vaddr + ph->memsz) * PAGE_SIZE;
	if (!mm_add_vma(mm, start, end, access | VM_ANON))
		return -ENOMEM;
	*previous_end = end;
	/* 文件部分逐页复制。尾页剩余字节已经清零，纯 BSS 页按需分配。 */
	for (size_t copied = 0; copied < ph->filesz;) {
		uintptr_t va = ph->vaddr + copied;
		size_t offset = va % PAGE_SIZE;
		size_t count = PAGE_SIZE - offset;
		struct page *page = mm_populate_page(mm, va - offset);

		if (!page)
			return -ENOMEM;
		if (count > ph->filesz - copied)
			count = ph->filesz - copied;
		memcpy((u8 *)page_address(page) + offset, file + ph->offset + copied, count);
		copied += count;
	}
	return 0;
}

int elf_load(const void *data, size_t size, struct mm_struct **result, uintptr_t *entry)
{
	static const u8 ident[] = { 0x7f, 'E', 'L', 'F', 2, 1, 1 };
	const uintptr_t stack_start = arch_user_limit() - STACK_PAGES * PAGE_SIZE;
	const u8 *file = data;
	struct elf64_ehdr eh;
	struct mm_struct *mm;
	uintptr_t previous_end = PAGE_SIZE;
	bool executable_entry = false;
	int err;

	if (size < sizeof(eh))
		return -ENOEXEC;
	memcpy(&eh, file, sizeof(eh));
	if (memcmp(eh.ident, ident, sizeof(ident)) || eh.type != 2 || eh.version != 1 ||
	    !arch_elf_supported(eh.machine, eh.flags, eh.entry) || eh.ehsize != sizeof(eh) ||
	    eh.phentsize != sizeof(struct elf64_phdr) || !eh.phnum || eh.phoff < sizeof(eh) ||
	    eh.phoff > size || eh.phnum > (size - eh.phoff) / sizeof(struct elf64_phdr))
		return -ENOEXEC;
	mm = mm_alloc();
	if (!mm)
		return -ENOMEM;
	for (unsigned int i = 0; i < eh.phnum; i++) {
		struct elf64_phdr ph;

		memcpy(&ph, file + eh.phoff + i * sizeof(ph), sizeof(ph));
		if (ph.type == PT_INTERP || ph.type == PT_DYNAMIC || ph.type == PT_TLS)
			goto invalid;
		if (ph.type != PT_LOAD)
			continue;
		err = load_segment(mm, file, size, &ph, stack_start - PAGE_SIZE, &previous_end);
		if (err)
			goto fail;
		if ((ph.flags & PF_X) && eh.entry >= ph.vaddr && eh.entry - ph.vaddr < ph.memsz)
			executable_entry = true;
	}
	if (!executable_entry)
		goto invalid;
	if (!mm_add_vma(mm, stack_start, arch_user_limit(), VM_READ | VM_WRITE | VM_ANON)) {
		err = -ENOMEM;
		goto fail;
	}
	*result = mm;
	*entry = eh.entry;
	return 0;
invalid:
	err = -ENOEXEC;
fail:
	mm_destroy(mm);
	return err;
}
