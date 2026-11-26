#include <fcntl.h>
#include <string.h>
#include <sys/wait.h>
#include "output.h"

#define LINE_SIZE 128
#define MAX_ARGS 32 /* 包含 argv[0]。 */

static int say(const char *text)
{
	return write_all(STDERR_FILENO, text, strlen(text));
}

/* 只读到本行结尾，下一行可能属于前台子进程。 */
static int read_command(char *line)
{
	size_t used = 0;
	const char *error = NULL;

	for (;;) {
		char ch;
		ssize_t n = read(STDIN_FILENO, &ch, 1);
		if (n < 0)
			return -1;
		if (!n && !used && !error)
			return 0;
		if (!n || ch == '\n')
			break;
		if (error)
			continue;
		if (used == LINE_SIZE - 1)
			error = "sh: command line too long\n";
		else if (!ch)
			error = "sh: NUL in command line\n";
		else
			line[used++] = ch;
	}
	if (error) {
		say(error);
		used = 0;
	}
	line[used] = '\0';
	return 1;
}

struct redirect {
	int target;
	char *path;
};

struct command {
	char *argv[MAX_ARGS + 1];
	int argc;
	struct redirect *redirects;
	int nredirects;
};

static int is_operator(char ch)
{
	return ch == '|' || ch == '<' || ch == '>';
}

/* 在原行中终止单词。保留刚被覆盖的分隔符，因此操作符两侧不要求空格。 */
static const char *parse_line(char *line, struct command cmds[2], struct redirect *next, int *count)
{
	struct command *cmd = &cmds[0];
	char pending = 0;

	for (int i = 0; i < 2; i++) {
		cmds[i].argc = 0;
		cmds[i].nredirects = 0;
	}
	*count = 1;
	cmd->redirects = next;
	for (;;) {
		char *word = line;
		while (*line && *line != ' ' && *line != '\t' && !is_operator(*line))
			line++;
		char separator = *line;
		if (separator)
			*line++ = '\0';
		if (*word && pending) {
			int target = pending == '<' ? STDIN_FILENO : STDOUT_FILENO;
			*next++ = (struct redirect){target, word};
			cmd->nredirects++;
			pending = 0;
		} else if (*word) {
			if (cmd->argc == MAX_ARGS)
				return "sh: too many arguments\n";
			cmd->argv[cmd->argc++] = word;
		}

		if (!separator)
			break;
		if (!is_operator(separator))
			continue;
		if (pending)
			return "sh: missing redirection path\n";
		if (separator == '|') {
			if (*count == 2 || !cmd->argc)
				return "sh: invalid pipeline\n";
			*count = 2;
			cmd = &cmds[1];
			cmd->redirects = next;
		} else {
			pending = separator;
		}
	}
	if (pending)
		return "sh: missing redirection path\n";
	if (*count == 1 && !cmd->argc && !cmd->nredirects) {
		*count = 0;
		return NULL;
	}
	for (int i = 0; i < *count; i++) {
		if (!cmds[i].argc)
			return "sh: missing command\n";
		cmds[i].argv[cmds[i].argc] = NULL;
		if (cmds[i].argv[0][0] != '/')
			return "sh: expected an absolute path\n";
	}
	return NULL;
}

/* fd 可能恰好就是目标编号，例如打开文件时标准输出已经关闭。 */
static void move_fd(int fd, int target)
{
	if (fd == target)
		return;
	if (dup3(fd, target, 0) < 0) {
		say("sh: cannot duplicate fd\n");
		_exit(1);
	}
	close(fd);
}

static _Noreturn void exec_command(const struct command *cmd, char **envp)
{
	for (int i = 0; i < cmd->nredirects; i++) {
		const struct redirect *r = &cmd->redirects[i];
		int flags = r->target == STDIN_FILENO ? O_RDONLY : O_WRONLY | O_CREAT | O_TRUNC;
		int fd = open(r->path, flags, 0666);
		if (fd < 0) {
			say("sh: cannot open ");
			say(r->path);
			say("\n");
			_exit(1);
		}
		move_fd(fd, r->target);
	}
	execve(cmd->argv[0], cmd->argv, envp);
	say("sh: cannot execute ");
	say(cmd->argv[0]);
	say("\n");
	_exit(127);
}

static void run_line(char *line, char **envp)
{
	struct command cmds[2];
	/* 每个重定向至少占用两个输入字节：操作符和单字节路径。 */
	struct redirect redirects[LINE_SIZE / 2];
	int count;
	const char *error = parse_line(line, cmds, redirects, &count);
	if (error) {
		say(error);
		return;
	}

	int ends[2];
	if (count == 2 && pipe(ends) < 0) {
		say("sh: pipe failed\n");
		return;
	}
	pid_t children[2];
	int started = 0;
	for (int i = 0; i < count; i++) {
		pid_t pid = fork();
		if (pid < 0) {
			say("sh: fork failed\n");
			break;
		}
		if (!pid) {
			if (count == 2) {
				if (i == 0) {
					close(ends[0]);
					move_fd(ends[1], STDOUT_FILENO);
				} else {
					close(ends[1]);
					move_fd(ends[0], STDIN_FILENO);
				}
			}
			exec_command(&cmds[i], envp);
		}
		children[started++] = pid;
	}
	/* 先关闭父进程的两端，再等待所有已启动的子进程，读端才能收到 EOF。 */
	if (count == 2) {
		close(ends[0]);
		close(ends[1]);
	}
	for (int i = 0; i < started; i++)
		if (waitpid(children[i], NULL, 0) < 0)
			say("sh: wait failed\n");
}

int main(int argc, char **argv, char **envp)
{
	char line[LINE_SIZE];

	(void)argc;
	(void)argv;
	for (;;) {
		if (say("minux$ ") < 0)
			return 1;
		int result = read_command(line);
		if (!result)
			return 0;
		if (result < 0) {
			say("sh: read failed\n");
			return 1;
		}
		run_line(line, envp);
	}
}
