#ifndef MINUX_USER_OUTPUT_H
#define MINUX_USER_OUTPUT_H

#include <unistd.h>

/* 管道或文件发生短写时，从尚未写出的字节继续。 */
static inline int write_all(int fd, const char *data, size_t len)
{
	while (len) {
		ssize_t n = write(fd, data, len);
		if (n <= 0)
			return -1;
		data += n;
		len -= n;
	}
	return 0;
}

#endif
