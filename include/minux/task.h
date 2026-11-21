#ifndef _MINUX_TASK_H
#define _MINUX_TASK_H

#include <minux/list.h>
#include <minux/types.h>

#define TASK_STACK_ORDER 2

struct arch_thread_context;
struct page;

enum task_state {
	TASK_RUNNING,
	TASK_RUNNABLE,
	TASK_DEAD,
};

struct task_struct {
	enum task_state state;
	struct list_head run_list;
	struct arch_thread_context *context;	//记录的是任务上次切出时保存下来的 sp。它指向任务自己内核栈上的一块 112 字节的现场：
	struct page *stack_page;				//指向内核栈分配块的首个页描述符。Minux 为每个新线程分配 4 个连续页，共 16 KiB。创建时用 page_address() 从描述符取得栈底地址，销毁时用它把整块页还给 buddy 分配器。
	void (*entry)(void *);					//线程入口函数指针。指向一个接收 void * 参数、返回类型为 void 的函数。
	void *arg;								//传给入口函数的参数。通常指向调用者准备的数据，也可以传 NULL。
};

/* 启动时调用一次，将当前启动栈交给首个任务。 */
void task_init(void);
struct task_struct *task_current(void);

/*
 * 创建和销毁都保留调用者的本地中断状态。
 * 新任务首次切入时调用 entry(arg)。入口须在关中断时返回，返回后自动回收。
 */
struct task_struct *task_create(void (*entry)(void *), void *arg);
/* 销毁队列中尚未退出的任务。调用者须确保任务指针仍然有效。 */
bool task_destroy(struct task_struct *task);

/* 单 hart，调用者须在普通内核上下文关闭本地中断后主动切换。 */
void task_switch(struct task_struct *next);
/*
 * 在普通内核上下文调度，切回后恢复调用时的本地中断状态。
 * 无任务可选时，已退出任务切到 idle。
 */
void schedule(void);

/* 启动任务进入 idle 后反复调用，每次派发任务或等待一次中断。 */
void task_idle_once(void);

#endif /* _MINUX_TASK_H */
