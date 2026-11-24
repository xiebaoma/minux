#ifndef _MINUX_FDTABLE_H
#define _MINUX_FDTABLE_H

#define FD_MAX 16

#ifndef __ASSEMBLER__
struct file;

struct fdtable {
	struct file *slots[FD_MAX];
};

int file_open(const char *path, int flags, struct file **out);
void file_put(struct file *file);
void fdtable_inherit(struct fdtable *child, const struct fdtable *parent);
void fdtable_release(struct fdtable *table);
#endif
#endif
