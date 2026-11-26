#include <minux/console.h>
#include <minux/errno.h>
#include <minux/fs.h>
#include <minux/irq.h>
#include <minux/uaccess.h>
#include <minux/wait.h>

#define TTY_BUF_SIZE 256
#define TTY_EOF 4 /* Ctrl-D 在队列中标记行结束，读时不交给用户。 */

/* 单 hart：接收中断与 read 在关中断区间操作同一环。下标单调递增，
 * [head, committed) 已提交，[committed, tail) 仍可退格。 */
static u8 input[TTY_BUF_SIZE];
static size_t head, committed, tail;
static struct wait_queue readers = { .tasks = LIST_HEAD_INIT(readers.tasks) };

void tty_receive_char(u8 ch)
{
	if (ch == '\b' || ch == 0x7f) {
		if (tail != committed) {
			tail--;
			early_console_write("\b \b", 3);
		}
		return;
	}
	if (ch == '\r')
		ch = '\n';
	bool end = ch == '\n' || ch == TTY_EOF;
	/* 留一个位置给换行或 EOF，使长行始终能提交。 */
	if (tail - head >= (end ? TTY_BUF_SIZE : TTY_BUF_SIZE - 1))
		return;
	input[tail++ % TTY_BUF_SIZE] = ch;
	if (ch != TTY_EOF)
		early_console_write((const char *)&ch, 1);
	if (end) {
		committed = tail;
		wake_up_all(&readers);
	}
}

static bool line_ready(void *arg)
{
	(void)arg;
	return head != committed;
}

static ssize_t console_read(struct file *file, uintptr_t addr, size_t count)
{
	u8 line[TTY_BUF_SIZE];
	size_t n = 0;
	bool eof = false;

	(void)file;
	if (!count)
		return 0;
	unsigned long irq = arch_local_irq_save();
	wait_event(&readers, line_ready, NULL);
	while (head + n != committed) {
		u8 ch = input[(head + n) % TTY_BUF_SIZE];
		if (ch == TTY_EOF) {
			eof = true;
			break;
		}
		if (n == count)
			break;
		line[n++] = ch;
		if (ch == '\n')
			break;
	}
	size_t copied = n - copy_to_user(addr, line, n);
	head += copied;
	/* 恰好读完 Ctrl-D 前的数据时一并取走标记；空行首的 Ctrl-D 返回一次 0。 */
	if (eof && copied == n)
		head++;
	arch_local_irq_restore(irq);
	return n && !copied ? -EFAULT : (ssize_t)copied;
}

static ssize_t console_write(struct file *file, uintptr_t addr, size_t count)
{
	char buf[256];
	size_t done = 0;

	(void)file;
	while (done < count) {
		size_t chunk = count - done;
		if (chunk > sizeof(buf))
			chunk = sizeof(buf);
		size_t copied = chunk - copy_from_user(buf, addr + done, chunk);

		early_console_write(buf, copied);
		done += copied;
		if (copied != chunk)
			break;
	}
	return done ? (ssize_t)done : -EFAULT;
}

const struct stream_operations console_operations = {
	.read = console_read,
	.write = console_write,
};
