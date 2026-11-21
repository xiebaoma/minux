#ifndef _MINUX_SPINLOCK_H
#define _MINUX_SPINLOCK_H

#include <minux/irq.h>
#include <minux/types.h>

struct spinlock {
	u32 locked;
};

#define SPINLOCK_INIT { .locked = 0 }

/* 获取锁前关闭本地中断，持锁期间禁止调度。 */
static inline unsigned long spin_lock_irqsave(struct spinlock *lock)
{
	unsigned long flags = arch_local_irq_save();

	while (__atomic_exchange_n(&lock->locked, 1, __ATOMIC_ACQUIRE))
		;
	return flags;
}

/* 先释放锁，再用获取锁时返回的 flags 恢复中断。 */
static inline void spin_unlock_irqrestore(struct spinlock *lock,
					  unsigned long flags)
{
	__atomic_store_n(&lock->locked, 0, __ATOMIC_RELEASE);
	arch_local_irq_restore(flags);
}

#endif /* _MINUX_SPINLOCK_H */
