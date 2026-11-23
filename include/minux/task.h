#ifndef _MINUX_TASK_H
#define _MINUX_TASK_H

#include <minux/compiler.h>
#include <minux/list.h>
#include <minux/types.h>
#include <minux/wait.h>

#define TASK_STACK_ORDER 2

struct arch_thread_context;
struct mm_struct;
struct page;

enum task_state {
	TASK_RUNNING,
	TASK_RUNNABLE,
	TASK_BLOCKED,
	TASK_DEAD,
	TASK_ZOMBIE, /* 执行资源已释放，等待父进程回收。 */
};

struct task_struct {
	enum task_state state;
	int pid; 								/* 用户任务的进程号，内核线程使用 0。 */
	struct list_head run_list; 				/* 就绪队列和等待队列共用此节点。 */
	struct arch_thread_context *context; 	/* 架构保存在内核栈上的切换现场。 */
	struct page *stack_page;				//指向内核栈分配块的首个页描述符。Minux 为每个新线程分配 4 个连续页，共 16 KiB。创建时用 page_address() 从描述符取得栈底地址，销毁时用它把整块页还给 buddy 分配器。
	void (*entry)(void *);					//线程入口函数指针。指向一个接收 void * 参数、返回类型为 void 的函数。
	void *arg;								//传给入口函数的参数。通常指向调用者准备的数据，也可以传 NULL。
	struct mm_struct *mm; 					/* 用户任务拥有独立地址空间；内核任务使用 NULL。 */
	struct task_struct *parent;				//指向父任务。当前代码中，用户任务创建另一个用户任务时，创建者成为父任务。
	struct list_head children;				//当前任务的子任务链表表头。
	struct list_head sibling;				//当前任务挂在父任务 children 链表中的节点。因此，一个父任务可以有多个子任务。
	struct wait_queue child_wait;			//父任务等待子任务退出时使用的等待队列；子任务退出后会唤醒它。
	int exit_status;						//保存子任务的退出状态，供父任务通过 wait4 读取。
};

/* 启动时调用一次，将当前启动栈交给首个任务。 */
void task_init(void);
struct task_struct *task_current(void);

/*
 * 创建和销毁都保留调用者的本地中断状态。
 * 新任务在开中断状态下运行 entry(arg)，返回后自动回收。
 * 返回值是借用指针；调度启动后，访问它需要确保线程仍存活。
 * 例如在同一个关中断区间内创建任务并操作其初始状态。
 */
struct task_struct *task_create(void (*entry)(void *), void *arg);
/*
 * 从已映射的入口和符合用户 ABI 的栈指针启动用户任务。
 * 成功后任务接管独占的 mm，失败时 mm 仍归调用者。返回值同样是借用指针。
 * 创建者是用户任务时，新任务成为其子进程。
 */
struct task_struct *task_create_user(struct mm_struct *mm, uintptr_t user_pc,
				     uintptr_t user_sp);
/* 销毁队列中尚未退出的任务。调用者须确保任务指针仍然有效。 */
bool task_destroy(struct task_struct *task);
/* 切走并释放执行资源，将编码后的状态留给仍在的父进程。 */
void task_exit_current(int status) __noreturn;
/* 等待并回收当前任务的指定子进程，pid 为 -1 时匹配任意子进程。 */
int task_wait_child(int pid, bool nohang, int *status);

/* 单 hart，调用者须在普通内核上下文关闭本地中断后主动切换。 */
void task_switch(struct task_struct *next);
/*
 * 在普通内核上下文或定时器中断的处理尾部调度，恢复调用时的中断状态。
 * 无就绪任务时，阻塞或退出的任务切到 idle。
 */
void schedule(void);

/* 启动任务进入 idle 后反复调用，每次派发任务或等待一次中断。 */
void task_idle_once(void);

#endif /* _MINUX_TASK_H */
