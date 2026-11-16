/*
 * 侵入式双向循环链表。对照 Linux 的 include/linux/list.h。
 *
 * 结构：链表头 head 本身也是一个 list_head 节点（“哨兵”），不属于任何元素。
 *
 *     空链表:     head.next == &head,  head.prev == &head
 *
 *     三个元素:   head <-> a <-> b <-> c <-> (回到 head)
 *                 head.next == &a.node, head.prev == &c.node
 *
 * 因为是循环的、并且总有哨兵，插入和删除都不需要判断“是不是空链表”、
 * “是不是第一个/最后一个”——这是这个设计最漂亮的地方。
 *
 * 注意：还没有异常处理（v0.2 才做），如果代码访问了空指针，内核会
 * 静悄悄地卡住而不是报错。`make test` 卡在 "list:" 之后没有输出，多半就是这个原因。
 */
#ifndef _MINUX_LIST_H
#define _MINUX_LIST_H

#include <minux/types.h>
#include <minux/container_of.h>

/* Linux 把它定义在 linux/types.h 里，这里为了集中放在 list.h */
struct list_head {
	struct list_head *next, *prev;
};

/* ---------------- 初始化 ---------------- */

/*
 * 静态初始化器，用法：struct list_head h = LIST_HEAD_INIT(h);
 * 空链表：next 和 prev 都指向自己。
 */
#define LIST_HEAD_INIT(name)	{ &(name), &(name) }

/* 定义并初始化一个链表头变量，用法：LIST_HEAD(my_list); */
#define LIST_HEAD(name)		struct list_head name = LIST_HEAD_INIT(name)

/* 运行时初始化（比如结构体里的 list_head 成员） */
static inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

/* 链表是否为空：哨兵的下一个还是自己 */
static inline bool list_empty(const struct list_head *head)
{
	return head->next == head;
}

/* ---------------- 插入 ---------------- */

/*
 * 内部函数：把 new 插到已知相邻的 prev 和 next 之间。
 *       prev <-> next   变成   prev <-> new <-> next
 * 一共改 4 个指针：new 的两个，加上两个邻居各一个。
 */
static inline void __list_add(struct list_head *new,
			      struct list_head *prev,
			      struct list_head *next)
{
	next->prev = new;
	new->next = next;
	new->prev = prev;
	prev->next = new;
}

/* 插到 head 之后（链表最前面），适合实现栈 */
static inline void list_add(struct list_head *new, struct list_head *head)
{
	__list_add(new, head, head->next);
}

/* 插到 head 之前——因为是循环链表，也就是链表最后面，适合实现队列 */
static inline void list_add_tail(struct list_head *new, struct list_head *head)
{
	__list_add(new, head->prev, head);
}

/* ---------------- 删除 ---------------- */

/* 内部函数：让 prev 和 next 直接互相指向，跳过它们中间的节点 */
static inline void __list_del(struct list_head *prev, struct list_head *next)
{
	next->prev = prev;
	prev->next = next;
}

/*
 * 把 entry 从它所在的链表里摘下来。
 * Linux 会把 next/prev 设成“毒药”值（LIST_POISON1/2），我们先设成 NULL：
 * 摘下后再误用 entry，会访问空指针，而不是悄悄改坏原来的邻居。
 */
static inline void list_del(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	entry->next = NULL;
	entry->prev = NULL;
}

/* 摘下来，并重新初始化成空链表（之后 list_empty(entry) 为真） */
static inline void list_del_init(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
	INIT_LIST_HEAD(entry);
}

/* ---------------- 取元素 ---------------- */

/* 由节点指针得到外层结构体指针 */
#define list_entry(ptr, type, member) \
	container_of(ptr, type, member)

/* 第一个元素（调用者保证链表非空）：第一个节点就是 head->next */
#define list_first_entry(head, type, member) \
	list_entry((head)->next, type, member)

/* ---------------- 遍历 ---------------- */

/* 遍历每个节点，pos 是 struct list_head *；绕一圈回到 head 就停 */
#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)

/*
 * 遍历每个元素，pos 直接是外层结构体指针，member 是 list_head 成员名。
 *
 * 链表为空或走到末尾时，pos 会是用 head 算出来的“假元素”
 * （container_of(head, ...)），它不是合法对象，但我们只比较
 * &pos->member 和 head，不解引用它，所以没问题。
 */
#define list_for_each_entry(pos, head, member)				\
	for (pos = list_first_entry(head, typeof(*pos), member);	\
	     &pos->member != (head);					\
	     pos = list_entry(pos->member.next, typeof(*pos), member))

/*
 * 和上面一样，但允许在循环体里 list_del(pos)。
 * 普通版本在循环体结束后才读 pos->member.next，可那时 pos 可能已经被删掉
 * （next 变成了 NULL）。所以进入循环体之前，先用 n 记下下一个元素。
 */
#define list_for_each_entry_safe(pos, n, head, member)			\
	for (pos = list_first_entry(head, typeof(*pos), member),	\
	     n = list_entry(pos->member.next, typeof(*pos), member);	\
	     &pos->member != (head);					\
	     pos = n, n = list_entry(n->member.next, typeof(*n), member))

#endif /* _MINUX_LIST_H */