#ifndef _MINUX_TASK_H
#define _MINUX_TASK_H

#include <minux/list.h>
#include <minux/types.h>

#define TASK_STACK_ORDER 2

struct arch_thread_context;
struct mm_struct;
struct page;

enum task_state {
	TASK_RUNNING,
	TASK_RUNNABLE,
	TASK_BLOCKED,
	TASK_DEAD,
};

struct task_struct {
	enum task_state state;
	struct list_head run_list; /* 就绪队列和等待队列共用此节点。 */
	struct arch_thread_context *context; /* 架构保存在内核栈上的切换现场。 */
	struct page *stack_page;				//指向内核栈分配块的首个页描述符。Minux 为每个新线程分配 4 个连续页，共 16 KiB。创建时用 page_address() 从描述符取得栈底地址，销毁时用它把整块页还给 buddy 分配器。
	void (*entry)(void *);					//线程入口函数指针。指向一个接收 void * 参数、返回类型为 void 的函数。
	void *arg;								//传给入口函数的参数。通常指向调用者准备的数据，也可以传 NULL。
	struct mm_struct *mm; /* 用户任务拥有独立地址空间；内核任务使用 NULL。 */
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
 */
struct task_struct *task_create_user(struct mm_struct *mm, uintptr_t user_pc,
				     uintptr_t user_sp);
/* 销毁队列中尚未退出的任务。调用者须确保任务指针仍然有效。 */
bool task_destroy(struct task_struct *task);

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
