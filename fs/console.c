#include <minux/console.h>
#include <minux/errno.h>
#include <minux/fs.h>
#include <minux/uaccess.h>

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
	.write = console_write,
};
