#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>

#include "init.h"

#include <lkl.h>
#include <lkl/asm/syscalls.h>

// FIXME: a static int only lets us add one disk and one dev
static int disk_id;
static int nd_id;

struct __fdtable __franken_fd[MAXFD];

void
__franken_fdinit()
{
	int fd;
	struct stat st;

	/* iterate over numbered descriptors, stopping when one does not exist */
	for (fd = 0; fd < MAXFD; fd++) {
		memset(&st, 0, sizeof(struct stat));
		if (fstat(fd, &st) == -1) {
			__franken_fd[fd].valid = 0;
			break;
		}
		__franken_fd[fd].valid = 1;
		__franken_fd[fd].flags = fcntl(fd, F_GETFL, 0);
		memcpy(&__franken_fd[fd].st, &st, sizeof(struct stat));
		/* XXX move this to platform code */
		switch (st.st_mode & S_IFMT) {
		case S_IFREG:
			__franken_fd[fd].seek = 1;
			break;
		case S_IFBLK:
			__franken_fd[fd].seek = 1;
			union lkl_disk disk;
			disk.fd = fd;
			disk_id = lkl_disk_add(disk);
			break;
		case S_IFCHR:
			/* XXX Linux presents stdin as char device see notes to clean up */
			__franken_fd[fd].seek = 0;
			break;
		case S_IFIFO:
			__franken_fd[fd].seek = 0;
			break;
		case S_IFSOCK:
			__franken_fd[fd].seek = 0;
			union lkl_netdev nd;
			nd.fd = fd;
			nd_id = lkl_netdev_add(nd, NULL);
			break;
		}
	}
}

static void
mount_tmpfs(void)
{
    lkl_sys_mkdir("/tmp", 0777);
    lkl_sys_mount("tmpfs", "/tmp", "tmpfs", 0, "mode=0777");
}

static void
unmount_atexit(void)
{
	int ret __attribute__((__unused__));
	ret = lkl_sys_umount("/etc", 0);
}

static void
register_net(int fd)
{
	/* FIXME: can we dynamically grab the real device address? */
    int ifindex;
    ifindex = lkl_netdev_get_ifindex(nd_id);
	lkl_if_up(ifindex);
	lkl_if_set_ipv4(ifindex, 0x0200010a /* 10.1.0.2 */, 24);
}

static int
register_block(int dev, int fd, int flags, off_t size, int root)
{
	int ret;
	char mnt_point[32];

	ret = lkl_mount_dev(disk_id, "ext4", 0, NULL, mnt_point,
			    sizeof(mnt_point));
	if (ret < 0) {
		printf("can't mount disk (%d) at %s. err=%d\n",
			disk_id, mnt_point, ret);
    } else if (root) {
        lkl_sys_chroot(mnt_point);
        lkl_sys_mkdir("/dev", 0755);
        lkl_sys_mknod("/dev/null", 0644, LKL_MKDEV(1, 3));
    }

	atexit(unmount_atexit);
	return ret;
}

void
__franken_fdinit_create()
{
	int fd;
	int n_block = 0;

	/* XXX would be nicer to be able to detect a file system,
	   but this also allows us not to mount a block device.
	   Pros and cons, may change if this is not convenient */

	/* only fd 3 will be mounted as root file system */
	if (__franken_fd[3].valid) {
		fd = 3;
		switch (__franken_fd[fd].st.st_mode & S_IFMT) {
		case S_IFREG:
		case S_IFBLK:
			if (register_block(n_block++, fd,
			    __franken_fd[fd].flags & O_ACCMODE,
			    __franken_fd[fd].st.st_size, 1) == 0) {
				__franken_fd[fd].mounted = 1;
			}
			break;
		case S_IFSOCK:
			register_net(fd);
			break;
		}
	}

	for (fd = 4; fd < MAXFD; fd++) {
		if (__franken_fd[fd].valid == 0)
			break;
		switch (__franken_fd[fd].st.st_mode & S_IFMT) {
		case S_IFREG:
            // FIXME: register regular file? when does this happen?
			break;
		case S_IFBLK:
			register_block(n_block++, fd, __franken_fd[fd].flags & O_ACCMODE,
                           __franken_fd[fd].st.st_size, 0);
			break;
		case S_IFSOCK:
			register_net(fd);
			break;
		}
	}

	/* now some generic stuff */
	mount_tmpfs();
}
