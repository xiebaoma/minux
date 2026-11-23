#include <minux/buddy.h>
#include <minux/elf.h>
#include <minux/errno.h>
#include <minux/mm.h>
#include <minux/page.h>
#include <minux/paging.h>
#include <minux/stddef.h>
#include <minux/string.h>
#include "test.h"

struct fixture {
	struct elf64_ehdr eh;
	struct elf64_phdr ph[2];
	u8 text[4], data[4];
};

#define TEXT_OFF offsetof(struct fixture, text)
#define DATA_OFF offsetof(struct fixture, data)
#define TEXT_VA (PAGE_SIZE + TEXT_OFF)
#define DATA_VA (4 * PAGE_SIZE + DATA_OFF)

static const struct fixture valid = {
	.eh = {
		.ident = { 0x7f, 'E', 'L', 'F', 2, 1, 1 }, .type = 2, .machine = 243,
		.version = 1, .entry = TEXT_VA, .ehsize = sizeof(struct elf64_ehdr),
		.phoff = offsetof(struct fixture, ph), .phnum = 2, .phentsize = sizeof(struct elf64_phdr),
	},
	.ph = {
		{ .type = 1, .flags = 5, .vaddr = TEXT_VA, .offset = TEXT_OFF,
		  .filesz = 4, .memsz = 4, .align = PAGE_SIZE },
		{ .type = 1, .flags = 6, .vaddr = DATA_VA, .offset = DATA_OFF,
		  .filesz = 4, .memsz = PAGE_SIZE + 16, .align = PAGE_SIZE },
	},
	.text = { 1, 2, 3, 4 }, .data = { 5, 6, 7, 8 },
};

static void expect_invalid(const struct fixture *file, size_t size, const char *why)
{
	u64 before = buddy_free_pages();
	struct mm_struct *mm = NULL;
	uintptr_t pc = 0;
	int err = elf_load(file, size, &mm, &pc);

	check(err == -ENOEXEC && !mm && !pc && buddy_free_pages() == before, why);
	mm_destroy(mm);
}

void test_elf(void)
{
	struct fixture file = valid;
	struct mm_struct *mm = NULL;
	uintptr_t pc = 0;
	struct page *page;
	unsigned int access;
	u64 baseline = buddy_free_pages();

	check(!elf_load(&valid, sizeof(valid), &mm, &pc) && mm && pc == TEXT_VA,
	      "elf: accept a static executable with non-page-aligned segment starts");
	if (mm) {
		page = arch_pgd_user_page(mm->pgd, pc, &access);
		check(page && access == (VM_READ | VM_EXEC) &&
		      !memcmp((u8 *)page_address(page) + TEXT_OFF, valid.text, 4),
		      "elf: place executable file bytes at their in-page offset");
		page = arch_pgd_user_page(mm->pgd, DATA_VA, &access);
		check(page && access == (VM_READ | VM_WRITE) &&
		      !memcmp((u8 *)page_address(page) + DATA_OFF, valid.data, 4) &&
		      !((u8 *)page_address(page))[DATA_OFF + 4],
		      "elf: preserve data, zero the tail and enforce segment permissions");
		page = mm_user_page(mm, DATA_VA + PAGE_SIZE, VM_WRITE);
		check(page && !((u8 *)page_address(page))[DATA_OFF] &&
		      !mm_user_page(mm, DATA_VA, VM_EXEC) && !mm_user_page(mm, pc, VM_WRITE),
		      "elf: demand-zero full BSS pages and preserve RX/RW separation");
		mm_destroy(mm);
	}
	check(buddy_free_pages() == baseline, "elf: unload all segment and page-table pages");

	expect_invalid(&file, sizeof(file.eh) - 1, "elf: truncated header");
	file.eh.phoff = ~(u64)0;
	expect_invalid(&file, sizeof(file), "elf: program headers outside the file");
	file = valid;
	file.eh.machine = 0;
	expect_invalid(&file, sizeof(file), "elf: incompatible machine");
	file = valid;
	file.ph[1].type = 3;
	expect_invalid(&file, sizeof(file), "elf: interpreter after a valid segment rolls back");
	file = valid;
	file.ph[1].memsz = 1;
	expect_invalid(&file, sizeof(file), "elf: file size exceeds memory size");
	file = valid;
	file.ph[1].memsz = ~(u64)0;
	expect_invalid(&file, sizeof(file), "elf: overflowing virtual range");
	expect_invalid(&valid, sizeof(valid) - 1, "elf: truncated segment data");
	file = valid;
	file.ph[1].vaddr = PAGE_SIZE + DATA_OFF;
	expect_invalid(&file, sizeof(file), "elf: reject overlapping page permissions");
	file = valid;
	file.ph[1].align = 3;
	expect_invalid(&file, sizeof(file), "elf: invalid segment alignment");
	file = valid;
	file.eh.entry = DATA_VA;
	expect_invalid(&file, sizeof(file), "elf: entry outside executable segments");
}
