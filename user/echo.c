#include <string.h>
#include "output.h"

int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		if (i > 1 && write_all(STDOUT_FILENO, " ", 1))
			return 1;
		if (write_all(STDOUT_FILENO, argv[i], strlen(argv[i])))
			return 1;
	}
	return write_all(STDOUT_FILENO, "\n", 1) ? 1 : 0;
}
