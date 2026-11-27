#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define REQUIRE(expr) do { if (!(expr)) _exit(__LINE__); } while (0)

static void check_libc_abi(void)
{
	char *args[] = { "/no-such-program", NULL };
	char c;

	errno = 777;
	REQUIRE(getpid() > 0 && errno == 777);

	REQUIRE(open("/no-such-file", O_RDONLY) == -1 && errno == ENOENT);
	errno = 777;
	int fd = open("/test/truncated", O_RDONLY);
	REQUIRE(fd >= 0 && errno == 777);

	REQUIRE(read(-1, &c, 1) == -1 && errno == EBADF);
	REQUIRE(read(fd, NULL, 1) == -1 && errno == EFAULT);
	REQUIRE(write(fd, "x", 1) == -1 && errno == EBADF);
	REQUIRE(dup3(fd, fd, 0) == -1 && errno == EINVAL);

	errno = 777;
	REQUIRE(close(fd) == 0 && errno == 777);
	REQUIRE(close(fd) == -1 && errno == EBADF);

	REQUIRE(pipe(NULL) == -1 && errno == EFAULT);
	REQUIRE(waitpid(-1, NULL, 0) == -1 && errno == ECHILD);
	REQUIRE(waitpid(-2, NULL, 0) == -1 && errno == EINVAL);
	REQUIRE(execve(args[0], args, NULL) == -1 && errno == ENOENT);

	REQUIRE(mmap(NULL, 0, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED &&
		errno == EINVAL);
	REQUIRE(mmap(NULL, (size_t)-1, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED &&
		errno == ENOMEM);
	REQUIRE(munmap((void *)1, 4096) == -1 && errno == EINVAL);

	errno = 555;
	unsigned char *pages = mmap(NULL, 3 * 4096, PROT_READ | PROT_WRITE,
				    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	REQUIRE(pages != MAP_FAILED && errno == 555);

	memset(pages + 4090, 0x5a, 12);
	for (size_t i = 0; i < 12; i++)
		REQUIRE(pages[4090 + i] == 0x5a);

	const char sample[] = "0123456789abcdef";
	memcpy(pages + 4088, sample, sizeof(sample));
	REQUIRE(!memcmp(pages + 4088, sample, sizeof(sample)));
	memmove(pages + 4092, pages + 4088, 12); /* 重叠区间向高地址移动。 */
	REQUIRE(!memcmp(pages + 4092, "0123456789ab", 12));
	memmove(pages + 4088, pages + 4092, 12); /* 重叠区间向低地址移动。 */
	REQUIRE(!memcmp(pages + 4088, "0123456789ab", 12));

	pages[2 * 4096] = 0x33;
	REQUIRE(munmap(pages + 4096, 4096) == 0 && errno == 555);
	REQUIRE(pages[4088] == '0' && pages[2 * 4096] == 0x33);
	REQUIRE(munmap(pages, 3 * 4096) == 0 && errno == 555);
}

static void check_heap(void)
{
	unsigned char *blocks[24];
	size_t sizes[24];

	errno = 444;
	REQUIRE(malloc(0) == NULL && errno == 444);
	free(NULL);
	REQUIRE(errno == 444);
	REQUIRE(realloc(NULL, 0) == NULL && errno == 444);
	REQUIRE(malloc((size_t)-1) == NULL && errno == ENOMEM);
	REQUIRE(malloc((size_t)-8) == NULL && errno == ENOMEM);

	for (size_t i = 0; i < 24; i++) {
		sizes[i] = i * 37 + 1;
		blocks[i] = malloc(sizes[i]);
		REQUIRE(blocks[i] != NULL && ((__UINTPTR_TYPE__)blocks[i] % 16) == 0);
		memset(blocks[i], (int)(0x20 + i), sizes[i]);
	}
	for (size_t i = 1; i < 24; i += 2)
		free(blocks[i]);
	for (size_t i = 0; i < 24; i += 2) {
		for (size_t j = 0; j < sizes[i]; j++)
			REQUIRE(blocks[i][j] == (unsigned char)(0x20 + i));
		free(blocks[i]);
	}

	/* 全部释放后再次分配中等块，验证空闲块合并及空 arena 归还后再分配正常。 */
	unsigned char *merged = malloc(7000);
	REQUIRE(merged != NULL && ((__UINTPTR_TYPE__)merged % 16) == 0);
	memset(merged, 0xaa, 7000);
	free(merged);

	unsigned char *p = realloc(NULL, 24);
	REQUIRE(p != NULL && ((__UINTPTR_TYPE__)p % 16) == 0);
	for (size_t i = 0; i < 24; i++)
		p[i] = (unsigned char)(i + 1);

	/* 后继空闲块充足时原地扩容。 */
	unsigned char *same = realloc(p, 128);
	REQUIRE(same == p);
	for (size_t i = 0; i < 24; i++)
		REQUIRE(p[i] == (unsigned char)(i + 1));

	/* 紧邻块被占用时触发迁移扩容，随后原地缩容。 */
	unsigned char *blocker = malloc(64);
	REQUIRE(blocker != NULL);
	memset(blocker, 0xee, 64);
	p = realloc(p, 600);
	REQUIRE(p != NULL && p != same && ((__UINTPTR_TYPE__)p % 16) == 0);
	for (size_t i = 0; i < 24; i++)
		REQUIRE(p[i] == (unsigned char)(i + 1));
	for (size_t i = 24; i < 600; i++)
		p[i] = (unsigned char)(i & 0xff);

	same = realloc(p, 80);
	REQUIRE(same == p);
	for (size_t i = 0; i < 24; i++)
		REQUIRE(p[i] == (unsigned char)(i + 1));

	/* 从小块迁移为跨多页大块，再继续扩容、原地缩容并验证失败不破坏原块。 */
	p = realloc(p, 20000);
	REQUIRE(p != NULL && ((__UINTPTR_TYPE__)p % 16) == 0);
	for (size_t i = 0; i < 24; i++)
		REQUIRE(p[i] == (unsigned char)(i + 1));
	for (size_t i = 24; i < 80; i++)
		REQUIRE(p[i] == (unsigned char)(i & 0xff));
	memset(p + 80, 0x5c, 20000 - 80);

	p = realloc(p, 36000);
	REQUIRE(p != NULL && p[0] == 1 && p[19999] == 0x5c);

	same = realloc(p, 96);
	REQUIRE(same == p && p[0] == 1 && p[23] == 24 && p[80] == 0x5c);

	errno = 0;
	REQUIRE(realloc(p, (size_t)-1) == NULL && errno == ENOMEM);
	REQUIRE(p[0] == 1 && p[23] == 24 && p[80] == 0x5c);

	errno = 333;
	REQUIRE(realloc(p, 0) == NULL && errno == 333);
	for (size_t i = 0; i < 64; i++)
		REQUIRE(blocker[i] == 0xee);
	free(blocker);

	for (unsigned int round = 0; round < 4; round++) {
		void *batch[12];
		for (size_t i = 0; i < 12; i++) {
			size_t sz = (i % 3 == 0) ? 12000 : (i * 512 + 48);
			batch[i] = malloc(sz);
			REQUIRE(batch[i] != NULL);
			memset(batch[i], (int)(round + i), sz);
		}
		for (size_t i = 0; i < 12; i++)
			free(batch[i]);
	}
}

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
	check_libc_abi();
	check_heap();

	/* /test/truncated 的 4 字节后接含 NUL 的文件，最后一次 read 只返回 1 字节。 */
	static char joined[4 + 2 * 4096 + 1]; /* 超过管道容量，要求读写两端同时运行。 */
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

	const char script[] = "/bin/cat</tmp/binary|/bin/cat>/tmp/piped\n/bin/cat</tmp/piped\n";
	fd = open("/tmp/script", O_WRONLY | O_CREAT | O_TRUNC, 0600);
	REQUIRE(fd >= 0 && write(fd, script, sizeof(script) - 1) == sizeof(script) - 1);
	REQUIRE(close(fd) == 0 && close(STDIN_FILENO) == 0);
	REQUIRE(open("/tmp/script", O_RDONLY) == STDIN_FILENO);
	fd = open("/tmp/prompts", O_WRONLY | O_CREAT, 0600);
	REQUIRE(fd == STDOUT_FILENO && dup3(fd, STDERR_FILENO, 0) == STDERR_FILENO);
	REQUIRE(close(fd) == 0);
	char *shell[] = { "/bin/sh", NULL };
	check_command(shell, 0, data, length);
	return 0;
}
