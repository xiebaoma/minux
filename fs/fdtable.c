#include <minux/errno.h>
#include <minux/fcntl.h>
#include <minux/fs.h>
#include <minux/limits.h>
#include <minux/slab.h>
#include <minux/string.h>
#include <minux/syscall.h>
#include <minux/task.h>
#include <minux/uaccess.h>

/* 每张表只由所属任务修改。不同进程的槽位可引用同一个打开文件。 */
void file_put(struct file *file)
{
	if (__atomic_sub_fetch(&file->refs, 1, __ATOMIC_ACQ_REL) == 0) {
		if (file->stream && file->stream->release)
			file->stream->release(file);
		kfree(file);
	}
}

void fdtable_inherit(struct fdtable *child, const struct fdtable *parent)
{
	for (int fd = 0; fd < FD_MAX; fd++) {
		struct file *file = parent->slots[fd];

		if (file)
			__atomic_add_fetch(&file->refs, 1, __ATOMIC_RELAXED);
		child->slots[fd] = file;
	}
}

void fdtable_release(struct fdtable *table)
{
	for (int fd = 0; fd < FD_MAX; fd++) {
		if (table->slots[fd]) {
			file_put(table->slots[fd]);
			table->slots[fd] = NULL;
		}
	}
}

static struct file *fd_get_file(int fd)
{
	if (fd < 0 || fd >= FD_MAX)
		return NULL;
	return task_current()->files.slots[fd];
}

long sys_close(int fd)
{
	struct file *file = fd_get_file(fd);

	if (!file)
		return -EBADF;
	task_current()->files.slots[fd] = NULL;
	file_put(file);
	return 0;
}

int file_open(const char *path, int flags, struct file **out)
{
	struct file *file = kmalloc(sizeof(*file));
	int err;

	if (!file)
		return -ENOMEM;
	err = vfs_open_flags(root_inode, path, flags, file);
	if (err)
		kfree(file);
	else
		*out = file;
	return err;
}

long sys_openat(int dirfd, uintptr_t pathname, int flags, unsigned int mode)
{
	struct fdtable *table = &task_current()->files;
	char path[VFS_PATH_MAX];
	int fd;
	long err = copy_string_from_user(path, pathname, sizeof(path));

	(void)mode; /* 当前没有权限模型。 */
	if (err < 0)
		return err == -E2BIG ? -ENAMETOOLONG : err;
	if (!path[0])
		return -ENOENT;
	if (path[0] != '/') {
		/* 此阶段 cwd 固定为根目录，fd 表中没有目录句柄。 */
		if (dirfd != AT_FDCWD)
			return fd_get_file(dirfd) ? -ENOTDIR : -EBADF;
		if ((size_t)err == sizeof(path))
			return -ENAMETOOLONG;
		memmove(path + 1, path, err);
		path[0] = '/';
	}
	/* 把可能失败的 fd 分配放在创建或截断文件之前。 */
	for (fd = 0; fd < FD_MAX && table->slots[fd]; fd++)
		;
	if (fd == FD_MAX)
		return -EMFILE;
	err = file_open(path, flags, &table->slots[fd]);
	return err ? err : fd;
}

static ssize_t read_to_user(struct file *file, uintptr_t addr, void *buf, size_t count)
{
	ssize_t n = vfs_read_at(file, file->pos, buf, count);
	size_t copied;

	if (n <= 0)
		return n;
	copied = n - copy_to_user(addr, buf, n);
	return copied ? (ssize_t)copied : -EFAULT;
}

static ssize_t write_from_user(struct file *file, uintptr_t addr, void *buf, size_t count)
{
	size_t copied = count - copy_from_user(buf, addr, count);

	if (!copied)
		return -EFAULT;
	return vfs_write_at(file, file->pos, buf, copied);
}

static long file_io(int fd, uintptr_t addr, size_t count, bool write)
{
	struct file *file = fd_get_file(fd);
	char buf[512];
	size_t done = 0;
	ssize_t n = 0;
	unsigned long irq;

	if (!file || (write && file->access == O_RDONLY) || (!write && file->access == O_WRONLY))
		return -EBADF;
	if (count > LONG_MAX)
		return -EINVAL;
	if (!count)
		return 0;
	if (file->stream) {
		if (write)
			return file->stream->write(file, addr, count);
		return file->stream->read(file, addr, count);
	}
	/* fork 后一次系统调用的所有分块共同推进同一个偏移。 */
	irq = spin_lock_irqsave(&file->pos_lock);
	while (done < count) {
		size_t chunk = count - done;

		if (chunk > sizeof(buf))
			chunk = sizeof(buf);
		if (write)
			n = write_from_user(file, addr + done, buf, chunk);
		else
			n = read_to_user(file, addr + done, buf, chunk);
		if (n <= 0)
			break;
		done += n;
		file->pos += n;
		if ((size_t)n < chunk)
			break;
	}
	spin_unlock_irqrestore(&file->pos_lock, irq);
	return done ? (long)done : n;
}

long sys_read(int fd, uintptr_t buf, size_t count)
{
	return file_io(fd, buf, count, false);
}

long sys_write(int fd, uintptr_t buf, size_t count)
{
	return file_io(fd, buf, count, true);
}
