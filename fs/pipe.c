#include <minux/errno.h>
#include <minux/fcntl.h>
#include <minux/fs.h>
#include <minux/irq.h>
#include <minux/limits.h>
#include <minux/slab.h>
#include <minux/syscall.h>
#include <minux/task.h>
#include <minux/uaccess.h>

struct pipe {
	struct wait_queue readers, writers;
	bool read_open, write_open;
	size_t head, used;
	u8 data[PIPE_BUF];
};

struct pipe_write_wait {
	struct pipe *pipe;
	size_t needed;
};

static bool readable(void *arg)
{
	struct pipe *p = arg;
	return p->used || !p->write_open;
}

static bool writable(void *arg)
{
	struct pipe_write_wait *w = arg;
	return !w->pipe->read_open || PIPE_BUF - w->pipe->used >= w->needed;
}

/* 单 hart 上用本地中断保护条件检查和入队。wait_event 可以切换任务，
 * 所以整个管道路径均不持有文件位置锁或 VFS 锁。 */
static ssize_t pipe_read(struct file *file, uintptr_t addr, size_t count)
{
	struct pipe *p = file->private_data;
	unsigned long irq = arch_local_irq_save();
	size_t done = 0;

	//等待“管道里有数据，或者写端已经关闭”
	wait_event(&p->readers, readable, p);
	if (count > p->used)
		count = p->used;
	while (done < count) {
		size_t chunk = PIPE_BUF - p->head;
		if (chunk > count - done)
			chunk = count - done;
		size_t copied = chunk - copy_to_user(addr + done, p->data + p->head, chunk);
		p->head = (p->head + copied) % PIPE_BUF;
		p->used -= copied;
		done += copied;
		if (copied != chunk)
			break;
	}
	if (done)
		wake_up_all(&p->writers);
	arch_local_irq_restore(irq);
	return count && !done ? -EFAULT : (ssize_t)done;
}

static ssize_t pipe_write(struct file *file, uintptr_t addr, size_t count)
{
	struct pipe *p = file->private_data;
	/* 小写入等到整段都能容纳；大写入可分次发布，让读者腾出空间。 */
	struct pipe_write_wait w = { p, count <= PIPE_BUF ? count : 1 };
	unsigned long irq = arch_local_irq_save();
	size_t done = 0;
	int err = 0;

	while (done < count) {
		wait_event(&p->writers, writable, &w);
		if (!p->read_open) {
			err = -EPIPE;
			break;
		}
		size_t tail = (p->head + p->used) % PIPE_BUF;
		size_t chunk = PIPE_BUF - p->used;
		if (chunk > PIPE_BUF - tail)
			chunk = PIPE_BUF - tail;
		if (chunk > count - done)
			chunk = count - done;
		size_t copied = chunk - copy_from_user(p->data + tail, addr + done, chunk);
		done += copied;
		p->used += copied;
		if (copied)
			wake_up_all(&p->readers);
		if (copied != chunk) {
			err = -EFAULT;
			break;
		}
		/* 小写入已预留足够空间；绕回环首时剩余部分可直接写入。 */
		w.needed = 1;
	}
	arch_local_irq_restore(irq);
	return done ? (ssize_t)done : err;
}

/* fork 复制的是打开文件引用。该端最后一个引用消失时才通知另一端。 */
static void pipe_release(struct file *file)
{
	struct pipe *p = file->private_data;
	unsigned long irq = arch_local_irq_save();

	if (file->access == O_RDONLY) {
		p->read_open = false;
		wake_up_all(&p->writers);
	} else {
		p->write_open = false;
		wake_up_all(&p->readers);
	}
	if (!p->read_open && !p->write_open)
		kfree(p);
	arch_local_irq_restore(irq);
}

static const struct stream_operations pipe_ops = {
	.read = pipe_read, .write = pipe_write, .release = pipe_release,
};

long sys_pipe2(uintptr_t pipefd, int flags)
{
	struct fdtable *table = &task_current()->files;
	int fds[2], found = 0;
	struct file *reader, *writer;
	struct pipe *p;
	int err;

	if (flags)
		return -EINVAL;
	for (int fd = 0; fd < FD_MAX && found < 2; fd++)
		if (!table->slots[fd])
			fds[found++] = fd;
	if (found < 2)
		return -EMFILE;
	p = kzalloc(sizeof(*p));
	reader = kmalloc(sizeof(*reader));
	writer = kmalloc(sizeof(*writer));
	if (!p || !reader || !writer) {
		err = -ENOMEM;
		goto fail;
	}
	init_wait_queue(&p->readers);
	init_wait_queue(&p->writers);
	p->read_open = p->write_open = true;
	*reader = (struct file){ .stream = &pipe_ops, .private_data = p, .refs = 1, .access = O_RDONLY };
	*writer = (struct file){ .stream = &pipe_ops, .private_data = p, .refs = 1, .access = O_WRONLY };
	err = copy_to_user_exact(pipefd, fds, sizeof(fds));
	if (err)
		goto fail;
	table->slots[fds[0]] = reader;
	table->slots[fds[1]] = writer;
	return 0;
fail:
	kfree(reader);
	kfree(writer);
	kfree(p);
	return err;
}
