#ifndef	_POLL_H
#define	_POLL_H

#ifdef __cplusplus
extern "C" {
#endif

#define POLLIN     0x001
#define POLLPRI    0x002
#define POLLOUT    0x004
#define POLLERR    0x008
#define POLLHUP    0x010
#define POLLNVAL   0x020
#define POLLRDNORM 0x040
#define POLLRDBAND 0x080
#ifndef POLLWRNORM
#define POLLWRNORM 0x100
#define POLLWRBAND 0x200
#endif
#ifndef POLLMSG
#define POLLMSG    0x400
#define POLLRDHUP  0x2000
#endif

typedef unsigned long nfds_t;

struct pollfd
{
	int fd;
	short events;
	short revents;
};

#define poll(a, b, c) __platform_poll(a, b, c)
int poll (struct pollfd *, nfds_t, int);

#ifdef __cplusplus
}
#endif

#endif