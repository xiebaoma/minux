#ifndef _MINUX_SWITCH_TO_H
#define _MINUX_SWITCH_TO_H

#include <minux/compiler.h>
#include <minux/types.h>

struct arch_thread_context;

/* 架构在新栈上建立初始现场，首次切入时调用 start。 */
struct arch_thread_context *arch_init_thread(void *stack_top, void (*start)(void));
/* 同时在内核栈上准备首次进入用户态的寄存器现场。 */
struct arch_thread_context *arch_init_user_thread(void *stack_top, void (*start)(void),
						 uintptr_t user_pc, uintptr_t user_sp);
/* 关本地中断时，从新任务的初始现场进入用户态。 */
void arch_enter_user(void *stack_top) __noreturn;
void arch_switch_to(struct arch_thread_context **prev,
		    struct arch_thread_context *next);

#endif /* _MINUX_SWITCH_TO_H */
