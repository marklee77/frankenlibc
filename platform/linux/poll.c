#include <poll.h>
#include <time.h>
#include <signal.h>
#include "syscall.h"

int poll(struct pollfd *fds, nfds_t n, int timeout)
{
	return syscall(SYS_poll, fds, n, timeout);
}
