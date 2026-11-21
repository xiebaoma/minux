#ifndef _MINUX_SWITCH_TO_H
#define _MINUX_SWITCH_TO_H

struct arch_thread_context;

/* 架构在新栈上建立初始现场，首次切入时调用 start。 */
struct arch_thread_context *arch_init_thread(void *stack_top, void (*start)(void));
void arch_switch_to(struct arch_thread_context **prev,
		    struct arch_thread_context *next);

#endif /* _MINUX_SWITCH_TO_H */
