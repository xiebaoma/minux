#include <fcntl.h>
#include <string.h>
#include "output.h"

static void report_error(const char *path, const char *message)
{
	write_all(STDERR_FILENO, "cat: ", 5);
	write_all(STDERR_FILENO, path, strlen(path));
	write_all(STDERR_FILENO, message, strlen(message));
}

int main(int argc, char **argv)
{
	char buf[512];
	char *standard_input[] = { "-", NULL };
	char **paths = argc > 1 ? argv + 1 : standard_input;
	int status = 0;

	for (; *paths; paths++) {
		const char *path = *paths;
		int from_stdin = !strcmp(path, "-");
		int fd = from_stdin ? STDIN_FILENO : open(path, O_RDONLY);
		if (fd < 0) {
			report_error(path, ": open failed\n");
			status = 1;
			continue;
		}
		ssize_t n;
		while ((n = read(fd, buf, sizeof(buf))) > 0) {
			if (write_all(STDOUT_FILENO, buf, n)) {
				report_error("stdout", ": write failed\n");
				if (!from_stdin)
					close(fd);
				return 1;
			}
		}
		if (n < 0) {
			report_error(path, ": read failed\n");
			status = 1;
		}
		if (!from_stdin && close(fd) < 0) {
			report_error(path, ": close failed\n");
			status = 1;
		}
	}
	return status;
}
