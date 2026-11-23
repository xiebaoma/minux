#ifndef _MINUX_SYSCALL_H
#define _MINUX_SYSCALL_H

#define WNOHANG 1

#ifndef __ASSEMBLER__
#include <minux/compiler.h>
#include <minux/types.h>

struct pt_regs;

long sys_clone(unsigned long flags, uintptr_t stack, const struct pt_regs *regs);
long sys_execve(uintptr_t filename, uintptr_t argv, uintptr_t envp, struct pt_regs *regs);
long sys_getpid(void);
void sys_exit(int code) __noreturn;
long sys_wait4(int pid, uintptr_t status_addr, int options, uintptr_t rusage_addr);
#endif

#endif /* _MINUX_SYSCALL_H */
