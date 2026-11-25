#include <unistd.h>

int main(int argc, char **argv, char **envp)
{
	const char expected_env[] = "MINUX=1";
	size_t len = 0;

	if (argc != 2 || !envp[0])
		return 1;
	for (size_t i = 0; i < sizeof(expected_env); i++)
		if (envp[0][i] != expected_env[i])
			return 1;
	while (argv[1][len])
		len++;
	if (write(STDOUT_FILENO, argv[1], len) != (ssize_t)len)
		return 2;
	return 37;
}
