#include <stdint.h>
#include <lkl_host.h>

#include "init.h"

#include "thread.h"

static void
printk(const char *msg)
{
	int ret __attribute__((unused));

	ret = write(2, msg, strlen(msg));
}

int threads_are_go;
struct franken_mtx *thrmtx;
struct franken_cv *thrcv;

static void print(const char *str, int len) {
	int ret __attribute__((unused));

	ret = write(1, str, len);
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
static char *boot_cmdline = "";

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

    lkl_host_ops.print = NULL;
    if (!get_from_environ("FRANKEN_VERBOSE")) {
        lkl_host_ops.print = print;
    }

	lkl_start_kernel(&lkl_host_ops, LKL_MEM_SIZE, boot_cmdline);

	mutex_enter(thrmtx);
    threads_are_go = 1;
	cv_broadcast(thrcv);
	mutex_exit(thrmtx);

	__init_libc(envp, argv[0]);
	__libc_start_init();

    /* crate base device */
    lkl_sys_mknod("/dev/null", 0644, LKL_MKDEV(1, 3));

    /* lo up */
	lkl_if_up(1);

	/* see if we have any devices to init */
	__franken_fdinit_create();

	atexit(finifn);

    /* make sure stdout is working even when VERBOSE is off */
    lkl_host_ops.print = print;

	exit(main(argc, argv, envp));
	return 0;
}
