/*
 * include/minux/list.h、container_of.h、stddef.h 的测试。
 *
 * 为了在实现还没写好时不卡死，这里遍历链表时都做了保护：
 * 遇到 NULL 指针或者走了太多步就判定失败并停止，而不是继续访问。
 */
#include <minux/list.h>
#include "test.h"

struct item {
	int val;			/* 故意放在 node 前面：node 的偏移不为 0 */
	struct list_head node;
};

/* 一个对象可以同时挂在多条链表上：这正是侵入式链表的优势 */
struct task {
	int id;
	struct list_head all;		/* 挂在“所有任务”链表上 */
	struct list_head ready;		/* 挂在“就绪”链表上 */
};

/*
 * 按 next 方向检查链表里的节点依次是 items[order[0]], items[order[1]] ...，
 * 再按 prev 方向反向检查一遍。只比较指针，不依赖 container_of。
 */
static bool list_is(struct list_head *head, struct item *items,
		    const int *order, int n)
{
	struct list_head *p = head;

	for (int i = 0; i < n; i++) {
		p = p->next;
		if (p == NULL || p != &items[order[i]].node)
			return false;
	}
	if (p->next != head)
		return false;

	for (int i = n - 1; i >= 0; i--) {
		if (p == NULL || p != &items[order[i]].node)
			return false;
		p = p->prev;
	}
	return p == head;
}

static void init_items(struct item *items, int n)
{
	for (int i = 0; i < n; i++) {
		items[i].val = i;
		items[i].node.next = NULL;
		items[i].node.prev = NULL;
	}
}

static void test_offsetof_container_of(void)
{
	struct item it;
	struct task t;

	check(offsetof(struct item, node) ==
	      (size_t)((char *)&it.node - (char *)&it),
	      "offsetof: struct item.node");
	check(offsetof(struct task, ready) ==
	      (size_t)((char *)&t.ready - (char *)&t),
	      "offsetof: struct task.ready");
	check(offsetof(struct item, val) == 0, "offsetof: first member is 0");

	check(container_of(&it.node, struct item, node) == &it,
	      "container_of: struct item.node");
	check(container_of(&t.ready, struct task, ready) == &t,
	      "container_of: struct task.ready");
	check(container_of(&t.all, struct task, all) == &t,
	      "container_of: struct task.all");
}

static void test_init(void)
{
	LIST_HEAD(a);
	struct list_head b = LIST_HEAD_INIT(b);
	struct list_head c = { NULL, NULL };

	INIT_LIST_HEAD(&c);

	check(a.next == &a && a.prev == &a, "LIST_HEAD: points to itself");
	check(b.next == &b && b.prev == &b, "LIST_HEAD_INIT: points to itself");
	check(c.next == &c && c.prev == &c, "INIT_LIST_HEAD: points to itself");
	check(list_empty(&a), "list_empty: new list is empty");
}

static void test_add(void)
{
	struct item items[3];
	LIST_HEAD(head);

	init_items(items, 3);
	list_add(&items[0].node, &head);
	list_add(&items[1].node, &head);
	list_add(&items[2].node, &head);
	check(list_is(&head, items, (const int[]){ 2, 1, 0 }, 3),
	      "list_add: inserts at front (2,1,0)");
	check(!list_empty(&head), "list_empty: false after add");

	init_items(items, 3);
	INIT_LIST_HEAD(&head);
	list_add_tail(&items[0].node, &head);
	list_add_tail(&items[1].node, &head);
	list_add_tail(&items[2].node, &head);
	check(list_is(&head, items, (const int[]){ 0, 1, 2 }, 3),
	      "list_add_tail: inserts at back (0,1,2)");
}

static void test_del(void)
{
	struct item items[3];
	LIST_HEAD(head);

	init_items(items, 3);
	list_add_tail(&items[0].node, &head);
	list_add_tail(&items[1].node, &head);
	list_add_tail(&items[2].node, &head);

	list_del(&items[1].node);
	check(list_is(&head, items, (const int[]){ 0, 2 }, 2),
	      "list_del: middle (0,2)");

	list_del(&items[0].node);
	check(list_is(&head, items, (const int[]){ 2 }, 1),
	      "list_del: first (2)");

	list_del_init(&items[2].node);
	check(list_empty(&head), "list_del: list empty after removing all");
	check(list_empty(&items[2].node), "list_del_init: entry is empty");
}

static void test_entry(void)
{
	struct item items[2];
	LIST_HEAD(head);

	init_items(items, 2);
	list_add_tail(&items[0].node, &head);
	list_add_tail(&items[1].node, &head);

	check(list_entry(&items[1].node, struct item, node) == &items[1],
	      "list_entry");
	check(list_first_entry(&head, struct item, node) == &items[0],
	      "list_first_entry");
}

static void test_for_each(void)
{
	struct item items[4];
	struct item *pos, *n;
	struct list_head *p;
	LIST_HEAD(head);
	int count, order[4];

	init_items(items, 4);
	for (int i = 0; i < 4; i++)
		list_add_tail(&items[i].node, &head);

	count = 0;
	list_for_each(p, &head) {
		if (++count > 10)
			break;
	}
	check(count == 4, "list_for_each: visits every node");

	count = 0;
	list_for_each_entry(pos, &head, node) {
		if (count >= 4)
			break;
		order[count++] = pos->val;
	}
	check(count == 4 && order[0] == 0 && order[1] == 1 &&
	      order[2] == 2 && order[3] == 3,
	      "list_for_each_entry: visits in order (0,1,2,3)");

	count = 0;
	LIST_HEAD(empty);
	list_for_each_entry(pos, &empty, node)
		count++;
	check(count == 0, "list_for_each_entry: empty list, no iteration");

	/* 边遍历边删除偶数项 */
	count = 0;
	list_for_each_entry_safe(pos, n, &head, node) {
		if (++count > 10)
			break;
		if (pos->val % 2 == 0)
			list_del(&pos->node);
	}
	check(count == 4, "list_for_each_entry_safe: visits every element");
	check(list_is(&head, items, (const int[]){ 1, 3 }, 2),
	      "list_for_each_entry_safe: delete while iterating (1,3)");
}

static void test_multi_list(void)
{
	struct task tasks[3];
	struct task *pos;
	LIST_HEAD(all);
	LIST_HEAD(ready);
	int sum_all = 0, sum_ready = 0, guard = 0;

	for (int i = 0; i < 3; i++) {
		tasks[i].id = i + 1;		/* 1, 2, 3 */
		list_add_tail(&tasks[i].all, &all);
	}
	list_add_tail(&tasks[0].ready, &ready);
	list_add_tail(&tasks[2].ready, &ready);

	list_for_each_entry(pos, &all, all) {
		if (++guard > 10)
			break;
		sum_all += pos->id;
	}
	guard = 0;
	list_for_each_entry(pos, &ready, ready) {
		if (++guard > 10)
			break;
		sum_ready += pos->id;
	}
	check(sum_all == 6, "multi-list: all = 1+2+3");
	check(sum_ready == 4, "multi-list: ready = 1+3");
}

void test_list(void)
{
	test_offsetof_container_of();
	test_init();
	test_add();
	test_del();
	test_entry();
	test_for_each();
	test_multi_list();
}
