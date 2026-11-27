#include <errno.h>
#include <fcntl.h>
#include <minux/signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define PAGE_BYTES 4096UL
#define REQUIRE(expr) do { if (!(expr)) _exit(__LINE__); } while (0)

static void test_errno_abi(void)
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
	REQUIRE(mmap(NULL, PAGE_BYTES, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED &&
		errno == EINVAL);
	REQUIRE(mmap(NULL, PAGE_BYTES, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1, 0) == MAP_FAILED &&
		errno == EINVAL);
	REQUIRE(mmap(NULL, PAGE_BYTES, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED &&
		errno == EINVAL);
	REQUIRE(mmap(NULL, PAGE_BYTES, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0) == MAP_FAILED &&
		errno == EINVAL);
	REQUIRE(mmap(NULL, PAGE_BYTES, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, PAGE_BYTES) == MAP_FAILED &&
		errno == EINVAL);
	REQUIRE(mmap(NULL, (size_t)-1, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED &&
		errno == ENOMEM);
	REQUIRE(munmap((void *)1, PAGE_BYTES) == -1 && errno == EINVAL);
	REQUIRE(munmap((void *)PAGE_BYTES, 0) == -1 && errno == EINVAL);
}

static void test_mmap_cow_and_split(void)
{
	errno = 555;
	unsigned char *pages = mmap(NULL, 3 * PAGE_BYTES, PROT_READ | PROT_WRITE,
				    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	REQUIRE(pages != MAP_FAILED && errno == 555);

	/* 跨页写入与重叠移动，同时触发三张匿名页的按需分配。 */
	for (size_t i = 0; i < 3 * PAGE_BYTES; i++)
		pages[i] = (unsigned char)((i * 29 + 7) & 0xff);
	const char sample[] = "0123456789abcdef";
	memcpy(pages + PAGE_BYTES - 8, sample, sizeof(sample));
	memmove(pages + PAGE_BYTES - 4, pages + PAGE_BYTES - 8, 12);
	REQUIRE(!memcmp(pages + PAGE_BYTES - 4, "0123456789ab", 12));
	memmove(pages + PAGE_BYTES - 8, pages + PAGE_BYTES - 4, 12);
	REQUIRE(!memcmp(pages + PAGE_BYTES - 8, "0123456789ab", 12));

	pid_t pid = fork();
	REQUIRE(pid >= 0);
	if (!pid) {
		REQUIRE(pages[0] == 7 && !memcmp(pages + PAGE_BYTES - 8, "0123456789ab", 12));
		memset(pages, 0xa5, 3 * PAGE_BYTES);
		for (size_t i = 0; i < 3 * PAGE_BYTES; i += 511)
			REQUIRE(pages[i] == 0xa5);
		_exit(0);
	}
	int status = -1;
	REQUIRE(waitpid(pid, &status, 0) == pid && status == 0);
	REQUIRE(pages[0] == 7 && !memcmp(pages + PAGE_BYTES - 8, "0123456789ab", 12));
	pages[0] = 0x42;
	pages[2 * PAGE_BYTES + 19] = 0x99;

	/* 解除中间一页，拆分 VMA；两侧仍可读写，子进程访问中间页应触发 SIGSEGV。 */
	REQUIRE(munmap(pages + PAGE_BYTES, PAGE_BYTES) == 0 && errno == 555);
	REQUIRE(pages[0] == 0x42 && pages[PAGE_BYTES - 8] == '0' &&
		pages[2 * PAGE_BYTES + 19] == 0x99);
	pages[PAGE_BYTES - 1] = 0x7e;
	pages[2 * PAGE_BYTES] = 0x81;
	REQUIRE(pages[PAGE_BYTES - 1] == 0x7e && pages[2 * PAGE_BYTES] == 0x81);

	for (int write_fault = 0; write_fault < 2; write_fault++) {
		pid = fork();
		REQUIRE(pid >= 0);
		if (!pid) {
			volatile unsigned char *mid = pages + PAGE_BYTES;
			if (write_fault)
				*mid = 1;
			else
				(void)*mid;
			_exit(0);
		}
		status = -1;
		REQUIRE(waitpid(pid, &status, 0) == pid && status == SIGSEGV);
	}

	REQUIRE(munmap(pages, 3 * PAGE_BYTES) == 0 && errno == 555);
}

static void test_heap(void)
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

	unsigned char *merged = malloc(7000);
	REQUIRE(merged != NULL && ((__UINTPTR_TYPE__)merged % 16) == 0);
	memset(merged, 0xaa, 7000);
	free(merged);

	unsigned char *p = realloc(NULL, 24);
	REQUIRE(p != NULL && ((__UINTPTR_TYPE__)p % 16) == 0);
	for (size_t i = 0; i < 24; i++)
		p[i] = (unsigned char)(i + 1);

	unsigned char *same = realloc(p, 128);
	REQUIRE(same == p);
	for (size_t i = 0; i < 24; i++)
		REQUIRE(p[i] == (unsigned char)(i + 1));

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

	for (unsigned int round = 0; round < 6; round++) {
		void *batch[16];
		for (size_t i = 0; i < 16; i++) {
			size_t sz = (i % 4 == 0) ? 14000 : (i * 512 + 32);
			batch[i] = malloc(sz);
			REQUIRE(batch[i] != NULL);
			memset(batch[i], (int)(round + i), sz);
			batch[i] = realloc(batch[i], sz + 1024);
			REQUIRE(batch[i] != NULL);
		}
		for (size_t i = 0; i < 16; i++)
			free(batch[i]);
	}

	/* 保留一块小分配和一块大分配由进程退出路径回收，同时验证运行期与退出回收。 */
	void *tail_small = malloc(256);
	void *tail_large = malloc(12288);
	REQUIRE(tail_small != NULL && tail_large != NULL);
	memset(tail_small, 0x12, 256);
	memset(tail_large, 0x34, 12288);
}

int main(void)
{
	test_errno_abi();
	test_mmap_cow_and_split();
	test_heap();
	return 0;
}
