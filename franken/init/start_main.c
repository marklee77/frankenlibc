#include <stdint.h>
#include <asm/syscalls.h>
#include <errno.h>

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

    ret = lkl_syscall(num, (long *)arg);
    if (ret < 0) {
        retval[0] = -ret;
        ret = -1;
    } 

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
 
int rump_init(void);

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

	rump_init();

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
