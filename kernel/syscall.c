#include <minux/errno.h>
#include <minux/printk.h>
#include <minux/syscall.h>
#include <minux/signal.h>
#include <minux/task.h>
#include <minux/uaccess.h>

long sys_getpid(void)
{
	return task_current()->pid;
}

void sys_exit(int code)
{
	pr_info("user task %d: exit %d\n", task_current()->pid, code & 0xff);
	task_exit_current((code & 0xff) << 8);
}

long sys_clone(unsigned long flags, uintptr_t stack, const struct pt_regs *regs)
{
	if (flags != SIGCHLD || stack)
		return -EINVAL;
	return task_fork(regs);
}

long sys_wait4(int pid, uintptr_t status_addr, int options, uintptr_t rusage_addr)
{
	int status, child_pid;

	if ((pid <= 0 && pid != -1) || (options & ~WNOHANG) || rusage_addr)
		return -EINVAL;

	child_pid = task_wait_child(pid, options & WNOHANG, &status);
	if (child_pid > 0 && status_addr &&
	    copy_to_user(status_addr, &status, sizeof(status)))
		return -EFAULT;
	return child_pid;
}
