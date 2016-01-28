#include <stdint.h>

#include <errno.h>
#include <sys/uio.h>
#include <time.h>

#include <lkl_host.h>
#include <asm/syscalls.h>

#include "init.h"

#include "thread.h"

static int threads_are_go;
static struct franken_mtx *thrmtx;
static struct franken_cv *thrcv;

struct thrdesc {
	void (*f)(void *);
	void *arg;
	int canceled;
	void *thrid;
	struct timespec timeout;
	struct franken_mtx *mtx;
	struct franken_cv *cv;
};

static void *franken_timer_trampoline(void *arg)
{
	struct thrdesc *td = arg;
	void (*f)(void *);
	void *thrarg;
	int err;

	if (!threads_are_go) {
		mutex_enter_nowrap(thrmtx);
		while (!threads_are_go) {
			cv_wait_nowrap(thrcv, thrmtx);
		}
		mutex_exit(thrmtx);
	}

	f = td->f;
	thrarg = td->arg;
	if (td->timeout.tv_sec != 0 || td->timeout.tv_nsec != 0) {
		mutex_enter(td->mtx);
		err = cv_timedwait(td->cv, td->mtx,
			 	   	       td->timeout.tv_sec,
					       td->timeout.tv_nsec);
		if (td->canceled) {
			if (!td->thrid) {
				free(td);
			}
			goto end;
		}
		mutex_exit(td->mtx);
		if (err && err != 60)
			goto end;
	}

	free(td);
	f(thrarg);

	exit_thread();
end:
	return arg;
}

#define NSEC_PER_SEC 1000000000

static void print(const char *str, int len) {
	int ret __attribute__((unused));

	ret = write(1, str, len);
}

struct franken_sem {
	struct franken_mtx *lock;
	int count;
	struct franken_cv *cond;
};

static void *sem_alloc(int count)
{
	struct franken_sem *sem;

	sem = malloc(sizeof(*sem));
	if (!sem)
		return NULL;

	mutex_init(&sem->lock, MTX_SPIN);
	sem->count = count;
	cv_init(&sem->cond);

	return sem;
}

static void sem_free(void *_sem)
{
	struct franken_sem *sem = (struct franken_sem *)_sem;

	cv_destroy(sem->cond);
	mutex_destroy(sem->lock);
	free(sem);
}

static void sem_up(void *_sem)
{
	struct franken_sem *sem = (struct franken_sem *)_sem;

	mutex_enter(sem->lock);
	sem->count++;
	if (sem->count > 0)
		cv_signal(sem->cond);
	mutex_exit(sem->lock);
}

static void sem_down(void *_sem)
{
	struct franken_sem *sem = (struct franken_sem *)_sem;

	mutex_enter(sem->lock);
	while (sem->count <= 0)
		cv_wait(sem->cond, sem->lock);
	sem->count--;
	mutex_exit(sem->lock);
}

static int thread_create(void (*fn)(void *), void *arg)
{
    return create_thread("thread", NULL, fn, arg, NULL, 0, 0) ? 0 : EINVAL;
}

static void thread_exit(void)
{
    exit_thread();
}

static unsigned long long time(void) {
    int64_t sec;
    long nsec;

    struct timespec ts;
    
    if (-1 == clock_gettime(CLOCK_REALTIME, &ts)) return errno;

    return ((unsigned long long)ts.tv_sec * NSEC_PER_SEC) + ts.tv_nsec;
}

// FIXME: timer functions don't seem right, but fixing them causes segfault...
static void *timer_alloc(void (*fn)(void *), void *arg) {
    return fn;
}

static int timer_set_oneshot(void *timer, unsigned long ns)
{
	struct thrdesc *td;
	int ret;

	td = malloc(sizeof(*td));

	memset(td, 0, sizeof(*td));
	td->f = (void (*)(void *))timer;
	td->timeout = (struct timespec){ .tv_sec = ns / NSEC_PER_SEC,
					 .tv_nsec = ns % NSEC_PER_SEC};

	mutex_init(&td->mtx, MTX_SPIN);
	cv_init(&td->cv);

	ret = create_thread(franken_timer_trampoline, td, "timer", 1, 0, -1, &td->thrid);
	if (ret) {
		free(td);
		return -1;
	}

	return td ? 0 : -1;
} 

static void timer_free(void *timer) {
	struct thrdesc *td = timer;

	if (td->canceled)
		return;

	td->canceled = 1;
	mutex_enter(td->mtx);
	cv_signal(td->cv);
	mutex_exit(td->mtx);

	mutex_destroy(td->mtx);
	cv_destroy(td->cv);

	if (td->thrid)
		join_thread(td->thrid);

	free(td);
}

static void panic(void)
{
    abort();
}

static int fd_get_capacity(union lkl_disk disk, unsigned long long *res)
{
	off_t off;

	off = lseek(disk.fd, 0, SEEK_END);
	if (off < 0)
		return -1;

	*res = off;
	return 0;
}

static int blk_request(union lkl_disk disk, struct lkl_blk_req *req)
{
	int err = 0;
	struct iovec *iovec = (struct iovec *)req->buf;

	/* TODO: handle short reads/writes */
	switch (req->type) {
	case LKL_DEV_BLK_TYPE_READ:
		err = preadv(disk.fd, iovec, req->count, req->sector * 512);
		break;
	case LKL_DEV_BLK_TYPE_WRITE:
		err = pwritev(disk.fd, iovec, req->count, req->sector * 512);
		break;
	case LKL_DEV_BLK_TYPE_FLUSH:
	case LKL_DEV_BLK_TYPE_FLUSH_OUT:
		err = fsync(disk.fd);
		break;
	default:
		return LKL_DEV_BLK_STATUS_UNSUP;
	}

	if (err < 0)
		return LKL_DEV_BLK_STATUS_IOERR;

	return LKL_DEV_BLK_STATUS_OK;
}

struct lkl_dev_blk_ops lkl_dev_blk_ops = {
	.get_capacity = fd_get_capacity,
	.request = blk_request,
};

char **environ __attribute__((weak));

static char empty_string[] = "";
char *__progname = empty_string;

void _libc_init(void) __attribute__((weak));
void _libc_init() {}

void __init_libc(char **envp, char *pn);
void __libc_start_init(void);

int __franken_start_main(int (*)(int,char **,char **), int, char **, char **);

void _init(void) __attribute__ ((weak));
void _init() {}
void _fini(void) __attribute__ ((weak));
void _fini() {}

extern void (*const __init_array_start)() __attribute__((weak));
extern void (*const __init_array_end)() __attribute__((weak));
extern void (*const __fini_array_start)() __attribute__((weak));
extern void (*const __fini_array_end)() __attribute__((weak));

int atexit(void (*)(void));
void exit(int) __attribute__ ((noreturn));
static void finifn(void);

static void
finifn()
{
	uintptr_t a = (uintptr_t)&__fini_array_end;

	for (; a>(uintptr_t)&__fini_array_start; a -= sizeof(void(*)()))
		(*(void (**)())(a - sizeof(void(*)())))();
	_fini();
}

#define LKL_MEM_SIZE 100 * 1024 * 1024
static char *boot_cmdline = "";

static void
printk(const char *msg)
{
	int ret __attribute__((unused));

	ret = write(2, msg, strlen(msg));
}

// this is needed because getenv does not work until after __init_libc
static char *get_from_environ(const char *name) {
    int i;
    char *p;

    for (i = 0; environ[i]; i++) {
        if (!(p = strchr(environ[i], '='))) continue;
        if (!strncmp(environ[i], name, p - environ[i])) return p+1;
    }

    return NULL;
}

int
__franken_start_main(int(*main)(int,char **,char **), int argc, char **argv, char **envp)
{
	uintptr_t a;

	environ = envp;

	if (argv[0]) {
		char *c;
		__progname = argv[0];
		for (c = argv[0]; *c; ++c) {
			if (*c == '/')
				__progname = c + 1;
		}
	}

	__franken_fdinit();

    init_sched();

	mutex_init(&thrmtx, MTX_SPIN);
	cv_init(&thrcv);
	threads_are_go = 0;

    lkl_host_ops.panic = panic;
    lkl_host_ops.sem_alloc = sem_alloc;
    lkl_host_ops.sem_free = sem_free;
    lkl_host_ops.sem_up = sem_up;
    lkl_host_ops.sem_down = sem_down;
    lkl_host_ops.thread_create = thread_create;
    lkl_host_ops.thread_exit = thread_exit;
    lkl_host_ops.time = time;
	lkl_host_ops.timer_alloc = timer_alloc;
	lkl_host_ops.timer_set_oneshot = timer_set_oneshot;
    lkl_host_ops.timer_free = timer_free;

    lkl_host_ops.print = NULL;
    if (get_from_environ("FRANKEN_VERBOSE")) {
        lkl_host_ops.print = print;
    }

	lkl_start_kernel(&lkl_host_ops, LKL_MEM_SIZE, boot_cmdline);

	mutex_enter(thrmtx);
    threads_are_go = 1;
	cv_broadcast(thrcv);
	mutex_exit(thrmtx);

	__init_libc(envp, argv[0]);
	__libc_start_init();

	/* see if we have any devices to init */
	__franken_fdinit_create();

	int lkl_if_up(int ifindex);
	lkl_if_up(1);

	atexit(finifn);

    /* make sure stdout is working even when VERBOSE is off */
    lkl_host_ops.print = print;

	exit(main(argc, argv, envp));
	return 0;
}
