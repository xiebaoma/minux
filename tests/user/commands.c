#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#define REQUIRE(expr) do { if (!(expr)) _exit(__LINE__); } while (0)

/* fd 0 继承为输入文件，父进程把捕获文件打开为 fd 1，再 fork/exec。 */
static void check_command(char *const argv[], int expected_status,
			  const char *expected, size_t length)
{
	const char *output = "/tmp/command-output";
	int status = -1;
	REQUIRE(open(output, O_CREAT | O_TRUNC | O_WRONLY, 0600) == STDOUT_FILENO);
	pid_t pid = fork();
	REQUIRE(pid >= 0);
	if (!pid) {
		execve(argv[0], argv, NULL);
		_exit(127);
	}
	REQUIRE(waitpid(pid, &status, 0) == pid);
	REQUIRE(status == (expected_status << 8));
	REQUIRE(close(STDOUT_FILENO) == 0);

	int fd = open(output, O_RDONLY);
	REQUIRE(fd >= 0);
	char buf[137];
	ssize_t n;
	size_t offset = 0;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		REQUIRE((size_t)n <= length - offset);
		for (ssize_t i = 0; i < n; i++)
			REQUIRE(buf[i] == expected[offset++]);
	}
	REQUIRE(n == 0 && offset == length);
	REQUIRE(close(fd) == 0);
}

int main(void)
{
	/* /test/truncated 的 4 字节后接含 NUL 的文件，最后一次 read 只返回 1 字节。 */
	char joined[4 + 1537];
	char *data = joined + 4;
	size_t length = sizeof(joined) - 4;
	for (size_t i = 0; i < 4; i++)
		joined[i] = "\177ELF"[i];
	for (size_t i = 0; i < length; i++)
		data[i] = (char)(i * 17);

	REQUIRE(open("/test/truncated", O_RDONLY) == STDIN_FILENO);
	int fd = open("/tmp/binary", O_CREAT | O_WRONLY, 0600);
	REQUIRE(fd >= 0 && write(fd, data, length) == (ssize_t)length);
	REQUIRE(close(fd) == 0);
	fd = open("/tmp/empty", O_CREAT | O_WRONLY, 0600);
	REQUIRE(fd >= 0 && close(fd) == 0);

	char *echo[] = { "/bin/echo", "Minux", "", "two words", NULL };
	char *bare_echo[] = { "/bin/echo", NULL };
	char *cat[] = { "/bin/cat", "/test/truncated", "/tmp/empty", "/tmp/binary", NULL };
	char *missing[] = { "/bin/cat", "/missing", "/tmp/binary", NULL };
	char *stdin_cat[] = { "/bin/cat", NULL };
	check_command(echo, 0, "Minux  two words\n", sizeof("Minux  two words\n") - 1);
	check_command(bare_echo, 0, "\n", 1);
	check_command(cat, 0, joined, sizeof(joined));
	check_command(missing, 1, data, length);
	check_command(stdin_cat, 0, "\177ELF", 4);
	return 0;
}
