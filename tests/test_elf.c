#include <minux/buddy.h>
#include <minux/elf.h>
#include <minux/errno.h>
#include <minux/container_of.h>
#include <minux/fs.h>
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

/* 模拟每次最多返回 7 字节的后端，验证 ELF 的所有读取都经过 VFS。 */
struct fixture_inode {
	struct inode inode;
	const void *bytes;
	size_t stop_at;
	int stop_result;
};

static ssize_t fixture_read(const struct inode *inode, size_t offset, void *buf, size_t count)
{
	const struct fixture_inode *src = container_of(inode, struct fixture_inode, inode);

	if (offset >= src->stop_at)
		return src->stop_result;
	if (count > src->stop_at - offset)
		count = src->stop_at - offset;
	if (count > 7)
		count = 7;
	memcpy(buf, (const u8 *)src->bytes + offset, count);
	return count;
}

static int load_fixture(const struct fixture *src, size_t size, size_t stop_at, int stop_result,
			struct mm_struct **mm, uintptr_t *pc)
{
	struct fixture_inode node = {
		.inode = { .size = size, .read = fixture_read },
		.bytes = src, .stop_at = stop_at, .stop_result = stop_result,
	};
	struct file file = { .inode = &node.inode };

	return elf_load(&file, mm, pc);
}

static void expect_invalid(const struct fixture *file, size_t size, const char *why)
{
	u64 before = buddy_free_pages();
	struct mm_struct *mm = NULL;
	uintptr_t pc = 0;
	int err = load_fixture(file, size, size, 0, &mm, &pc);

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

	check(!load_fixture(&valid, sizeof(valid), sizeof(valid), 0, &mm, &pc) && mm && pc == TEXT_VA,
	      "elf: accept a static executable with non-page-aligned segment starts");
	if (mm) {
		check(mm->mmap_limit == arch_user_limit() - 9 * PAGE_SIZE,
		      "elf: reserve stack and one-page guard above mmap_limit");
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

	mm = NULL;
	pc = 0;
	check(load_fixture(&valid, sizeof(valid), DATA_OFF + 1, 0, &mm, &pc) == -ENOEXEC &&
	      !mm && !pc && buddy_free_pages() == baseline,
	      "elf: premature EOF in a segment rolls back the new image");
	check(load_fixture(&valid, sizeof(valid), DATA_OFF, -EFAULT, &mm, &pc) == -EFAULT &&
	      !mm && !pc && buddy_free_pages() == baseline,
	      "elf: propagate a backend read error and reclaim the partial image");

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
