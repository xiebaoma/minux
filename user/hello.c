#include <string.h>
#include <unistd.h>

int main(int argc, char **argv, char **envp)
{
	if (argc != 2 || !envp[0] || strcmp(envp[0], "MINUX=1"))
		return 1;
	size_t len = strlen(argv[1]);
	if (write(STDOUT_FILENO, argv[1], len) != (ssize_t)len)
		return 2;
	return 37;
}
