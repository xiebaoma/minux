/**
ELF 是一种可执行文件的格式，全名是 Executable and Linkable Format。可以把它理解成程序交给操作系统的一份“装载说明书”。

它不只存着机器指令，还告诉内核：
- 程序从哪个地址开始运行；
- 哪些内容要放进内存；
- 哪些内存可以执行、读取或写入；
- 哪些区域需要初始化为零。

*/

#ifndef _ASM_RISCV_ELF_H
#define _ASM_RISCV_ELF_H

#include <minux/types.h>


/*
这个函数用来判断 ELF 文件头里的三个架构信息是否符合 Minux 的要求。三个条件必须同时成立：
machine == 243        // 机器类型是 RISC-V
!(flags & ~1U)        // flags 只能是 0 或 1
!(entry & 1)          // 程序入口地址必须是偶数
*/
static inline bool arch_elf_supported(u16 machine, u32 flags, u64 entry)
{
	return machine == 243 && !(flags & ~1U) && !(entry & 1);
}

#define ARCH_STACK_ALIGN 16

#endif
