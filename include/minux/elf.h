#ifndef _MINUX_ELF_H
#define _MINUX_ELF_H

#include <minux/types.h>

struct mm_struct;

/*
ELF 文件
├─ elf64_ehdr：这是什么程序？入口在哪？程序头表在哪？
├─ elf64_phdr × phnum：哪些内容要装入内存、装到哪里？
└─ 实际的指令和数据
*/

struct elf64_ehdr {
	u8 ident[16];
	u16 type, machine;
	u32 version;
	u64 entry, phoff, shoff;
	u32 flags;
	u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};

struct elf64_phdr {
	u32 type, flags;
	u64 offset, vaddr, paddr, filesz, memsz, align;
};

/* 成功时发布独立地址空间和入口，失败时由加载器释放临时资源。 */
int elf_load(const void *data, size_t size, struct mm_struct **mm, uintptr_t *entry);
int start_init_process(void);

#endif
