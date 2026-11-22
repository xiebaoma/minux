#include <minux/syscall.h>
#include <minux/task.h>

long sys_getpid(void)
{
	return task_current()->pid;
}
