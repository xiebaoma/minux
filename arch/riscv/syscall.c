#include <asm/trap.h>
#include <asm/unistd.h>
#include <minux/errno.h>
#include <minux/selftest.h>
#include <minux/syscall.h>

static long dispatch_syscall(struct pt_regs *regs)
{
	/* Linux RV64 从 a7 取 32 位调用号。参数依次位于 a0 至 a5。 */
	switch ((int)regs->x[17]) {
	case __NR_exit:
		sys_exit((int)regs->x[10]);
	case __NR_getpid:
		return sys_getpid();
	case __NR_clone:
		return sys_clone(regs->x[10], regs->x[11], regs);
	case __NR_execve:
		return sys_execve(regs->x[10], regs->x[11], regs->x[12], regs);
	case __NR_wait4:
		return sys_wait4((int)regs->x[10], regs->x[11],
				 (int)regs->x[12], regs->x[13]);
#ifdef CONFIG_SELFTEST
	case __NR_test_copy:
		return selftest_user_copy(regs->x[10], regs->x[11], regs->x[12]);
	case __NR_test_wait:
		return selftest_user_wait(regs->x[10]);
#endif
	default:
		return -ENOSYS;
	}
}

void riscv_handle_syscall(struct pt_regs *regs)
{
	regs->epc += 4; /* ecall 是一条 32 位指令。 */
	regs->x[10] = dispatch_syscall(regs);
}
