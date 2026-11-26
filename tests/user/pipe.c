/* 独立的 RV64 用户态 ELF。失败行号作为退出码显示在串口上。 */
#include "syscall.h"
#include <minux/errno.h>
#include <minux/fcntl.h>
#include <minux/fdtable.h>
#include <minux/limits.h>
#include <minux/signal.h>

static void __attribute__((noreturn)) finish(int status)
{
	call(__NR_exit, status, 0, 0, 0);
	__builtin_unreachable();
}

#define REQUIRE(expr) do { if (!(expr)) finish(__LINE__); } while (0)

static long pipe2(int fds[2], int flags)
{
	return call(__NR_pipe2, (long)fds, flags, 0, 0);
}

static long rd(int fd, void *dst, unsigned long n)
{
	return call(__NR_read, fd, (long)dst, n, 0);
}

static long wr(int fd, const void *src, unsigned long n)
{
	return call(__NR_write, fd, (long)src, n, 0);
}

static void close_fd(int fd)
{
	REQUIRE(call(__NR_close, fd, 0, 0, 0) == 0);
}

static long fork_child(void)
{
	long pid = call(__NR_clone, SIGCHLD, 0, 0, 0);
	REQUIRE(pid >= 0);
	return pid;
}

static void reap(long pid)
{
	int status = -1;
	REQUIRE(call(__NR_wait4, pid, (long)&status, 0, 0) == pid && status == 0);
}

static void test_errors(void)
{
	int fds[2] = { -1, -2 };
	char c;

	REQUIRE(pipe2(fds, 1) == -EINVAL && fds[0] == -1 && fds[1] == -2);
	REQUIRE(pipe2((void *)0, 0) == -EFAULT);
	REQUIRE(pipe2(fds, 0) == 0 && fds[0] == 0 && fds[1] == 1);
	REQUIRE(rd(fds[1], &c, 1) == -EBADF && wr(fds[0], "x", 1) == -EBADF);
	REQUIRE(rd(fds[0], 0, 0) == 0 && wr(fds[1], 0, 0) == 0);
	REQUIRE(wr(fds[1], 0, 1) == -EFAULT && wr(fds[1], "x", 1) == 1);
	REQUIRE(rd(fds[0], 0, 1) == -EFAULT);
	REQUIRE(rd(fds[0], &c, 1) == 1 && c == 'x');
	close_fd(fds[0]);
	REQUIRE(wr(fds[1], "x", 1) == -EPIPE);
	close_fd(fds[1]);

	for (int i = 0; i < FD_MAX - 1; i++)
		REQUIRE(call(__NR_openat, AT_FDCWD, (long)"/init", O_RDONLY, 0) == i);
	fds[0] = fds[1] = -1;
	REQUIRE(pipe2(fds, 0) == -EMFILE && fds[0] == -1 && fds[1] == -1);
	close_fd(0);
	REQUIRE(pipe2(fds, 0) == 0 && fds[0] == 0 && fds[1] == FD_MAX - 1);
	for (int i = 0; i < FD_MAX; i++)
		close_fd(i);
}

static unsigned char payload[2 * PIPE_BUF + 173];

static void test_dup3(void)
{
	int ends[2];
	char c;
	long file = call(__NR_openat, AT_FDCWD, (long)"/init", O_RDONLY, 0);
	REQUIRE(file == 0 && pipe2(ends, 0) == 0);
	int alias = ends[1];
	REQUIRE(call(__NR_dup3, file, file, 0, 0) == -EINVAL);
	REQUIRE(call(__NR_dup3, file, alias, 1, 0) == -EINVAL);
	REQUIRE(call(__NR_dup3, FD_MAX, alias, 0, 0) == -EBADF);
	REQUIRE(call(__NR_dup3, file, -1, 0, 0) == -EBADF);
	REQUIRE(call(__NR_dup3, file, FD_MAX, 0, 0) == -EBADF);
	REQUIRE(wr(alias, "x", 1) == 1 && rd(ends[0], &c, 1) == 1 && c == 'x');
	/* 替换最后一个写端使管道到达 EOF，复制后的文件共享读位置。 */
	REQUIRE(call(__NR_dup3, file, alias, 0, 0) == alias);
	REQUIRE(rd(ends[0], &c, 1) == 0);
	REQUIRE(rd(file, &c, 1) == 1 && c == 0x7f);
	REQUIRE(rd(alias, &c, 1) == 1 && c == 'E');
	REQUIRE(call(__NR_dup3, file, alias, 0, 0) == alias);
	close_fd(file);
	REQUIRE(rd(alias, &c, 1) == 1 && c == 'L');
	close_fd(ends[0]);
	close_fd(alias);
}

static void test_stream(void)
{
	unsigned char buf[317];
	int fds[2];

	for (unsigned int i = 0; i < sizeof(payload); i++)
		payload[i] = (unsigned char)(i * 31 + i / 256);
	REQUIRE(pipe2(fds, 0) == 0);
	long pid = fork_child();
	if (!pid) {
		close_fd(fds[0]);
		REQUIRE(wr(fds[1], payload, sizeof(payload)) == sizeof(payload));
		finish(0); /* 最后的写端由退出路径自动关闭。 */
	}
	close_fd(fds[1]);
	unsigned long offset = 0;
	for (;;) {
		long n = rd(fds[0], buf, sizeof(buf));
		REQUIRE(n >= 0 && (unsigned long)n <= sizeof(payload) - offset);
		if (!n)
			break;
		for (long i = 0; i < n; i++)
			REQUIRE(buf[i] == payload[offset + i]);
		offset += n;
	}
	REQUIRE(offset == sizeof(payload));
	close_fd(fds[0]);
	reap(pid);
}

/* 剩余空间只有一字节，两个字节的小写入需整体等待。随后跨环末读回。 */
static void test_small_write(void)
{
	int fds[2];
	REQUIRE(pipe2(fds, 0) == 0);
	REQUIRE(wr(fds[1], payload, PIPE_BUF - 1) == PIPE_BUF - 1);
	long pid = fork_child();
	if (!pid) {
		close_fd(fds[1]);
		REQUIRE(rd(fds[0], payload, PIPE_BUF) == PIPE_BUF - 1);
		REQUIRE(rd(fds[0], payload, 2) == 2 && payload[0] == 'x' && payload[1] == 'y');
		REQUIRE(rd(fds[0], payload, 1) == 0);
		finish(0);
	}
	close_fd(fds[0]);
	REQUIRE(wr(fds[1], "xy", 2) == 2);
	close_fd(fds[1]);
	reap(pid);
}

static void test_peer_close(void)
{
	int fds[2];
	char c;

	REQUIRE(pipe2(fds, 0) == 0);
	long pid = fork_child();
	if (!pid)
		finish(0);
	close_fd(fds[1]);
	REQUIRE(rd(fds[0], &c, 1) == 0);
	close_fd(fds[0]);
	reap(pid);

	REQUIRE(pipe2(fds, 0) == 0);
	REQUIRE(wr(fds[1], payload, PIPE_BUF) == PIPE_BUF);
	pid = fork_child();
	if (!pid)
		finish(0);
	close_fd(fds[0]);
	REQUIRE(wr(fds[1], "x", 1) == -EPIPE);
	close_fd(fds[1]);
	reap(pid);
}

void _start(void)
{
	test_errors();
	test_dup3();
	test_stream();
	test_small_write();
	test_peer_close();
	finish(0);
}
