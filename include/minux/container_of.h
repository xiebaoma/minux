/*
 * container_of：已知某结构体中一个成员的地址，反推出整个结构体的地址。
 * 对照 Linux 的 include/linux/container_of.h。
 *
 *     struct item { int val; struct list_head node; };
 *
 *     地址:  it                    &it.node
 *            |<--- offsetof --->|
 *            [  val  | padding  |  node  ]
 *
 *     container_of(&it.node, struct item, node) == &it
 */
#ifndef _MINUX_CONTAINER_OF_H
#define _MINUX_CONTAINER_OF_H

#include <minux/stddef.h>

/* a 和 b 的类型是否相同（GNU C 内建，Linux 在 compiler_types.h 里也这么定义） */
#define __same_type(a, b)	__builtin_types_compatible_p(typeof(a), typeof(b))

/*
 * container_of - 由成员指针得到外层结构体指针
 * @ptr:    指向成员的指针
 * @type:   外层结构体的类型
 * @member: 成员在结构体里的名字
 */
#define container_of(ptr, type, member) ({				\
	char *__mptr = (char *)(ptr);					\
	_Static_assert(__same_type(*(ptr), ((type *)0)->member) ||	\
		       __same_type(*(ptr), void),			\
		       "pointer type mismatch in container_of()");	\
	((type *)(__mptr - offsetof(type, member))); })

#endif /* _MINUX_CONTAINER_OF_H */