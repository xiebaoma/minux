#ifndef _MINUX_UACCESS_H
#define _MINUX_UACCESS_H

#include <minux/types.h>

/*
 * 访问当前任务的用户空间，返回未拷贝的字节数。
 * copy_from_user() 将内核目标缓冲区中未拷贝的尾部清零。
 */
/* 成功时返回含 NUL 的长度，越界或缓冲区不足时返回负 errno。 */
long copy_string_from_user(char *to, uintptr_t from, size_t limit);
size_t copy_from_user(void *to, uintptr_t from, size_t size);
size_t copy_to_user(uintptr_t to, const void *from, size_t size);

/* 小型系统调用结果：先准备所有目标页，失败时用户缓冲区保持原样。 */
int copy_to_user_exact(uintptr_t to, const void *from, size_t size);

#endif /* _MINUX_UACCESS_H */
