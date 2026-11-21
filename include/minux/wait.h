#ifndef _MINUX_WAIT_H
#define _MINUX_WAIT_H

#include <minux/list.h>
#include <minux/types.h>

struct wait_queue {
	struct list_head tasks;
};

static inline void init_wait_queue(struct wait_queue *queue)
{
	INIT_LIST_HEAD(&queue->tasks);
}

/*
 * 普通线程在 idle 路径建立后调用。ready 在关中断区间内检查，
 * 不得阻塞。每次唤醒后重新检查条件，返回时保留调用者的中断状态。
 * 睡眠期间队列及条件数据必须保持有效，且调用者不得持有自旋锁。
 */
// 让当前task_struct睡眠，知道指定条件成立
void wait_event(struct wait_queue *queue, bool (*ready)(void *), void *arg);

/* 发布条件后调用，将所有等待者按入队次序移到就绪队列。可在中断中调用。 */
void wake_up_all(struct wait_queue *queue);

#endif /* _MINUX_WAIT_H */
