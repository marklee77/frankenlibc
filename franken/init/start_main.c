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

static void print(const char *str, int len) {
	int ret __attribute__((unused));

	ret = write(1, str, len);
}

struct rumpuser_sem {
	struct rumpuser_mtx *lock;
	int count;
	struct rumpuser_cv *cond;
};

static void *sem_alloc(int count)
{
	struct rumpuser_sem *sem;

	rumpuser_malloc(sizeof(*sem), 0, (void **)&sem);
	if (!sem)
		return NULL;

	rumpuser_mutex_init(&sem->lock, RUMPUSER_MTX_SPIN);
	sem->count = count;
	rumpuser_cv_init(&sem->cond);

	return sem;
}

static void sem_free(void *_sem)
{
	struct rumpuser_sem *sem = (struct rumpuser_sem *)_sem;

	rumpuser_cv_destroy(sem->cond);
	rumpuser_mutex_destroy(sem->lock);
	rumpuser_free(sem, 0);
}

static void sem_up(void *_sem)
{
	struct rumpuser_sem *sem = (struct rumpuser_sem *)_sem;

	rumpuser_mutex_enter(sem->lock);
	sem->count++;
	if (sem->count > 0)
		rumpuser_cv_signal(sem->cond);
	rumpuser_mutex_exit(sem->lock);
}

static void sem_down(void *_sem)
{
	struct rumpuser_sem *sem = (struct rumpuser_sem *)_sem;

	rumpuser_mutex_enter(sem->lock);
	while (sem->count <= 0)
		rumpuser_cv_wait(sem->cond, sem->lock);
	sem->count--;
	rumpuser_mutex_exit(sem->lock);
}

static int thread_create(void (*fn)(void *), void *arg)
{
    void *thr;
    return rumpuser_thread_create(fn, arg, "thread", 0, 1, -1, &thr);
}

static void thread_exit(void)
{
    rumpuser_thread_exit();
}

static unsigned long long time(void) {
    int64_t sec;
    long nsec;
    
    rumpuser_clock_gettime(RUMPUSER_CLOCK_RELWALL, &sec, &nsec);

    return ((unsigned long long)sec * NSEC_PER_SEC) + nsec;
}

static void *timer_alloc(void (*fn)(void *), void *arg) {
    return fn;
}

static int timer_set_oneshot(void *timer, unsigned long ns)
{
	int ret;
	struct thrdesc *td;

	rumpuser_malloc(sizeof(*td), 0, (void **)&td);

	memset(td, 0, sizeof(*td));
	td->f = (void (*)(void))timer;
	td->arg = NULL;
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

	return td ? 0 : -1;
} 

static void timer_free(void *timer) {
	struct thrdesc *td = timer;

	if (td->canceled)
		return;

	td->canceled = 1;
	rumpuser_mutex_enter(td->mtx);
	rumpuser_cv_signal(td->cv);
	rumpuser_mutex_exit(td->mtx);

	rumpuser_mutex_destroy(td->mtx);
	rumpuser_cv_destroy(td->cv);

	if (td->thrid)
		rumpuser_thread_join(td->thrid);

	rumpuser_free(td, 0);
}

static void panic(void)
{
    rumpuser_exit(RUMPUSER_PANIC);
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

    lkl_host_ops.panic = panic;
    lkl_host_ops.print = print;
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
