#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include "output.h"

static int say(const char *text)
{
	return write_all(STDERR_FILENO, text, strlen(text));
}

/* 只读到本行结尾，下一行可能属于前台子进程。 */
static int read_command(char **out)
{
	size_t cap = 128, used = 0;
	const char *error = NULL;
	char *line = malloc(cap);

	*out = NULL;
	if (!line)
		error = "sh: out of memory\n";
	for (;;) {
		char ch;
		ssize_t n = read(STDIN_FILENO, &ch, 1);
		if (n < 0) {
			free(line);
			return -1;
		}
		if (!n && !used && !error) {
			free(line);
			return 0;
		}
		if (!n || ch == '\n')
			break;
		if (error)
			continue;
		if (!ch) {
			error = "sh: NUL in command line\n";
			continue;
		}
		if (used + 1 >= cap) {
			if (cap > (size_t)-1 / 2) {
				error = "sh: out of memory\n";
				continue;
			}
			size_t next_cap = cap * 2;
			char *grown = realloc(line, next_cap);
			if (!grown) {
				error = "sh: out of memory\n";
				continue;
			}
			line = grown;
			cap = next_cap;
		}
		line[used++] = ch;
	}
	if (error) {
		say(error);
		free(line);
		return 2;
	}
	line[used] = '\0';
	*out = line;
	return 1;
}

struct redirect {
	int target;
	char *path;
};

struct command {
	char **argv;
	size_t argc;
	size_t argv_cap;
	struct redirect *redirects;
	size_t nredirects;
	size_t redirects_cap;
};

static void free_commands(struct command cmds[2])
{
	for (int i = 0; i < 2; i++) {
		free(cmds[i].argv);
		free(cmds[i].redirects);
	}
}

static int push_arg(struct command *cmd, char *word)
{
	if (cmd->argc + 2 > cmd->argv_cap) {
		size_t cap = cmd->argv_cap ? cmd->argv_cap * 2 : 8;
		if (cap < cmd->argc + 2 || cap > (size_t)-1 / sizeof(*cmd->argv))
			return -1;
		char **grown = realloc(cmd->argv, cap * sizeof(*cmd->argv));
		if (!grown)
			return -1;
		cmd->argv = grown;
		cmd->argv_cap = cap;
	}
	cmd->argv[cmd->argc++] = word;
	cmd->argv[cmd->argc] = NULL;
	return 0;
}

static int push_redirect(struct command *cmd, int target, char *path)
{
	if (cmd->nredirects == cmd->redirects_cap) {
		size_t cap = cmd->redirects_cap ? cmd->redirects_cap * 2 : 4;
		if (cap <= cmd->nredirects || cap > (size_t)-1 / sizeof(*cmd->redirects))
			return -1;
		struct redirect *grown = realloc(cmd->redirects, cap * sizeof(*cmd->redirects));
		if (!grown)
			return -1;
		cmd->redirects = grown;
		cmd->redirects_cap = cap;
	}
	cmd->redirects[cmd->nredirects++] = (struct redirect){ target, path };
	return 0;
}

static int is_operator(char ch)
{
	return ch == '|' || ch == '<' || ch == '>';
}

/* 在原行中终止单词。保留刚被覆盖的分隔符，因此操作符两侧不要求空格。 */
static const char *parse_line(char *line, struct command cmds[2], int *count)
{
	struct command *cmd = &cmds[0];
	char pending = 0;

	*count = 1;
	for (;;) {
		char *word = line;
		while (*line && *line != ' ' && *line != '\t' && !is_operator(*line))
			line++;
		char separator = *line;
		if (separator)
			*line++ = '\0';
		if (*word && pending) {
			int target = pending == '<' ? STDIN_FILENO : STDOUT_FILENO;
			if (push_redirect(cmd, target, word) < 0)
				return "sh: out of memory\n";
			pending = 0;
		} else if (*word) {
			if (push_arg(cmd, word) < 0)
				return "sh: out of memory\n";
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
	for (size_t i = 0; i < cmd->nredirects; i++) {
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
	if (errno == E2BIG) {
		say("sh: argument list too long\n");
		_exit(126);
	}
	say("sh: cannot execute ");
	say(cmd->argv[0]);
	say("\n");
	_exit(127);
}

static void run_line(char *line, char **envp)
{
	struct command cmds[2] = { 0 };
	int count = 0;
	const char *error = parse_line(line, cmds, &count);
	if (error) {
		say(error);
		goto out;
	}
	if (!count)
		goto out;

	int ends[2];
	if (count == 2 && pipe(ends) < 0) {
		say("sh: pipe failed\n");
		goto out;
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
out:
	free_commands(cmds);
}

int main(int argc, char **argv, char **envp)
{
	(void)argc;
	(void)argv;
	for (;;) {
		char *line = NULL;

		if (say("minux$ ") < 0)
			return 1;
		int result = read_command(&line);
		if (!result)
			return 0;
		if (result < 0) {
			say("sh: read failed\n");
			return 1;
		}
		if (result == 1) {
			run_line(line, envp);
			free(line);
		}
	}
}
