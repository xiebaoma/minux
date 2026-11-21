#include <minux/spinlock.h>
#include "test.h"

void test_spinlock(void)
{
	struct spinlock outer = SPINLOCK_INIT;
	struct spinlock inner = SPINLOCK_INIT;
	unsigned long initial_flags = arch_local_irq_save();
	unsigned long disabled_flags = arch_local_irq_save();
	unsigned long enabled_flags, outer_flags, inner_flags, restored_flags;

	/* 此时 PLIC、UART 和定时器尚未初始化，临时打开总中断。 */
	arch_local_irq_enable();
	enabled_flags = arch_local_irq_save();
	arch_local_irq_restore(enabled_flags);
	check(enabled_flags != disabled_flags, "spinlock: IRQ test starts enabled");

	outer_flags = spin_lock_irqsave(&outer);
	check(outer_flags == enabled_flags, "spinlock: saves the enabled IRQ state");
	check(__atomic_load_n(&outer.locked, __ATOMIC_RELAXED) == 1,
	      "spinlock: acquisition marks the lock held");

	inner_flags = spin_lock_irqsave(&inner);
	check(inner_flags == disabled_flags, "spinlock: acquisition disables local IRQs");
	spin_unlock_irqrestore(&inner, inner_flags);
	restored_flags = arch_local_irq_save();
	check(restored_flags == disabled_flags, "spinlock: nested release keeps IRQs disabled");
	check(__atomic_load_n(&inner.locked, __ATOMIC_RELAXED) == 0,
	      "spinlock: nested release clears the inner lock");

	spin_unlock_irqrestore(&outer, outer_flags);
	restored_flags = arch_local_irq_save();
	arch_local_irq_restore(initial_flags);
	check(restored_flags == enabled_flags, "spinlock: outer release restores enabled IRQs");
	check(__atomic_load_n(&outer.locked, __ATOMIC_RELAXED) == 0,
	      "spinlock: outer release clears the lock");
}
