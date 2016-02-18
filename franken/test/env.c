#include <stdlib.h>
#include <string.h>
#include <assert.h>

extern char **environ;

int
main(int argc, char **argv)
{

	assert(environ != NULL);
	return 0;
}
