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

static int split_args(char *line, char **args)
{
	int argc = 0;

	while (*line) {
		if (*line == ' ' || *line == '\t') {
			line++;
			continue;
		}
		if (argc == MAX_ARGS)
			return -1;
		args[argc++] = line;
		while (*line && *line != ' ' && *line != '\t')
			line++;
		if (*line)
			*line++ = '\0';
	}
	args[argc] = NULL;
	return argc;
}

int main(int argc, char **argv, char **envp)
{
	char line[LINE_SIZE];
	char *args[MAX_ARGS + 1];

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
		int count = split_args(line, args);
		if (!count)
			continue;
		if (count < 0) {
			say("sh: too many arguments\n");
			continue;
		}
		if (args[0][0] != '/') {
			say("sh: expected an absolute path\n");
			continue;
		}

		pid_t pid = fork();
		if (pid < 0) {
			say("sh: fork failed\n");
			continue;
		}
		if (!pid) {
			execve(args[0], args, envp);
			say("sh: cannot execute ");
			say(args[0]);
			say("\n");
			_exit(127);
		}
		if (waitpid(pid, NULL, 0) < 0)
			say("sh: wait failed\n");
	}
}
