#include <stdint.h>

#include <errno.h>

#include <lkl_host.h>
#include <asm/syscalls.h>

#include "init.h"

#include "thread.h"
#include "rump/rumpuser.h"

static void hyp_schedule(void) {}
static void hyp_unschedule(void) {}

static void hyp_backend_schedule(int nlocks, void *interlock) {}
static void hyp_backend_unschedule(int nlocks, int *countp, void *interlock) {}

static void hyp_lwproc_switch(struct lwp *newlwp) {}
static void hyp_lwproc_release(void) {}
static int hyp_lwproc_newlwp(pid_t pid) { return 0; }
static struct lwp *hyp_lwproc_curlwp(void) { return NULL; }
static int hyp_lwproc_rfork(void *priv, int flags, const char *comm) { return 0; }
static void hyp_lwpexit(void) {}

static pid_t hyp_getpid(void) { return 0; }
static int hyp_syscall(int num, void *arg, long *retval) {
    int ret = 0;

#if 0
    ret = lkl_syscall(num, (long *)arg);
    if (ret < 0) {
        retval[0] = -ret;
        ret = -1;
    } 
#endif

    return ret;
}
static void hyp_execnotify(const char *comm) {}

static const struct rumpuser_hyperup hyp = {
    .hyp_schedule = hyp_schedule,
    .hyp_unschedule = hyp_unschedule,

    .hyp_backend_schedule = hyp_backend_schedule,
    .hyp_backend_unschedule = hyp_backend_unschedule,

    .hyp_lwproc_switch = hyp_lwproc_switch,
    .hyp_lwproc_release = hyp_lwproc_release,
    .hyp_lwproc_newlwp = hyp_lwproc_newlwp,
    .hyp_lwproc_curlwp = hyp_lwproc_curlwp,
    .hyp_lwproc_rfork = hyp_lwproc_rfork,
    .hyp_lwpexit = hyp_lwpexit,    

    .hyp_getpid = hyp_getpid,
    .hyp_syscall = hyp_syscall,
    .hyp_execnotify = hyp_execnotify,
};

static int threads_are_go;
static struct rumpuser_mtx *thrmtx;
static struct rumpuser_cv *thrcv;

struct thrdesc {
	void (*f)(void *);
	void *arg;
	int canceled;
	void *thrid;
	struct timespec timeout;
	struct rumpuser_mtx *mtx;
	struct rumpuser_cv *cv;
};

static void *rump_timer_trampoline(void *arg)
{
	struct thrdesc *td = arg;
	void (*f)(void *);
	void *thrarg;
	int err;

	/* from src-netbsd/sys/rump/librump/rumpkern/thread.c */
	/* don't allow threads to run before all CPUs have fully attached */
	if (!threads_are_go) {
		rumpuser_mutex_enter_nowrap(thrmtx);
		while (!threads_are_go) {
			rumpuser_cv_wait_nowrap(thrcv, thrmtx);
		}
		rumpuser_mutex_exit(thrmtx);
	}

	f = td->f;
	thrarg = td->arg;
	if (td->timeout.tv_sec != 0 || td->timeout.tv_nsec != 0) {
		rumpuser_mutex_enter(td->mtx);
		err = rumpuser_cv_timedwait(td->cv, td->mtx,
					    td->timeout.tv_sec,
					    td->timeout.tv_nsec);
		if (td->canceled) {
			if (!td->thrid) {
				rumpuser_free(td, 0);
			}
			goto end;
		}
		rumpuser_mutex_exit(td->mtx);
		/* FIXME: we should not use rumpuser__errtrans here */
		/* FIXME: 60=>ETIMEDOUT(netbsd) rumpuser__errtrans(ETIMEDOUT)) */
		if (err && err != 60)
			goto end;
	}

	rumpuser_free(td, 0);
	f(thrarg);

	rumpuser_thread_exit();
end:
	return arg;
}

#define NSEC_PER_SEC 1000000000

static void *rump_add_timer(__u64 ns, void (*func) (void *arg), void *arg)
{
	int ret;
	struct thrdesc *td;

	rumpuser_malloc(sizeof(*td), 0, (void **)&td);

	memset(td, 0, sizeof(*td));
	td->f = func;
	td->arg = arg;
	td->timeout = (struct timespec){ .tv_sec = ns / NSEC_PER_SEC,
					 .tv_nsec = ns % NSEC_PER_SEC};

	rumpuser_mutex_init(&td->mtx, RUMPUSER_MTX_SPIN);
	rumpuser_cv_init(&td->cv);

	ret = rumpuser_thread_create(rump_timer_trampoline, td, "timer",
				     1, 0, -1, &td->thrid);
	if (ret) {
		rumpuser_free(td, 0);
		return NULL;
	}

	return td;
}

static void *timer_alloc(void (*fn)(void *), void *arg) {
    return fn;
}

static int timer_set_oneshot(void *timer_fn, unsigned long ns)
{
    return rump_add_timer(ns, (void (*)(void *))timer_fn, NULL) ? 0 : -1;
} 
 
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
static char *boot_cmdline = "";    /* FIXME: maybe we have rump_set_boot_cmdline? */

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

    if (rumpuser_init(RUMPUSER_VERSION, &hyp) != 0) {
        return EINVAL;
    }

	rumpuser_mutex_init(&thrmtx, RUMPUSER_MTX_SPIN);
	rumpuser_cv_init(&thrcv);
	threads_are_go = 0;

	lkl_host_ops.timer_alloc = timer_alloc;
	lkl_host_ops.timer_set_oneshot = timer_set_oneshot;
	lkl_start_kernel(&lkl_host_ops, LKL_MEM_SIZE, boot_cmdline);

	rumpuser_mutex_enter(thrmtx);
    threads_are_go = 1;
	rumpuser_cv_broadcast(thrcv);
	rumpuser_mutex_exit(thrmtx);

	__init_libc(envp, argv[0]);
	__libc_start_init();

	/* see if we have any devices to init */
	__franken_fdinit_create();

	/* XXX may need to have a rump kernel specific hook */
	int lkl_if_up(int ifindex);
	lkl_if_up(1);

	atexit(finifn);

	exit(main(argc, argv, envp));
	return 0;
}
