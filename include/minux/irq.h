#ifndef _MINUX_IRQ_H
#define _MINUX_IRQ_H

/* 保存并关闭本地总中断开关，随后用返回值恢复。 */
unsigned long arch_local_irq_save(void);
void arch_local_irq_restore(unsigned long flags);

/* 总中断关闭时仍可等待已启用的中断源，返回后由调用者恢复总开关。 */
void arch_wait_for_interrupt(void);

#endif /* _MINUX_IRQ_H */
