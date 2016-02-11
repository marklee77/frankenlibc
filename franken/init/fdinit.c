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

// FIXME: this typedef shouldn't be necessary
typedef unsigned socklen_t;
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "init.h"

#include <lkl.h>
#include <lkl/asm/syscalls.h>

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

		switch (st.st_mode & S_IFMT) {
		case S_IFREG:
			__franken_fd[fd].seek = 1;
			break;
		case S_IFBLK:
			__franken_fd[fd].seek = 1;
			union lkl_disk disk;
			disk.fd = fd;
			__franken_fd[fd].device_id = lkl_disk_add(disk);
			break;
		case S_IFCHR:
		case S_IFIFO:
			__franken_fd[fd].seek = 0;
			break;
		case S_IFSOCK:
			__franken_fd[fd].seek = 0;
			union lkl_netdev nd;
			nd.fd = fd;
			__franken_fd[fd].device_id = lkl_netdev_add(nd, NULL);
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

// FIXME: change env vars to FIXED_%FD_ADDR to support multiple devices
static void
register_net(int fd)
{
	int ifindex, err;
	char *addr, *mask, *gw;

	ifindex = lkl_netdev_get_ifindex(__franken_fd[fd].device_id);
	if ((err = lkl_if_up(ifindex)))
		;

	addr = getenv("FIXED_ADDRESS");
	mask = getenv("FIXED_MASK");
	gw = getenv("FIXED_GATEWAY");

	// FIXME: check for error
	if (addr && mask && gw) {
		if ((err = lkl_if_set_ipv4(ifindex, inet_addr(addr), atoi(mask)))) 
			;
		if ((err = lkl_set_ipv4_gateway(inet_addr(gw)))) 
			;
	} else {
		lkl_if_set_ipv4(ifindex, inet_addr("10.1.0.2"), 24);
	}
}

static int
register_block(int fd, int flags, off_t size, int root)
{
	int ret;
	char mnt_point[32];

	ret = lkl_mount_dev(__franken_fd[fd].device_id, "ext4", 0, NULL, 
			    mnt_point, sizeof(mnt_point));
	if (ret < 0) {
		;
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

	for (fd = 3; fd < MAXFD; fd++) {
		if (__franken_fd[fd].valid == 0)
			break;
		switch (__franken_fd[fd].st.st_mode & S_IFMT) {
		case S_IFREG:
			break;
		case S_IFBLK:
			register_block(fd, __franken_fd[fd].flags & O_ACCMODE,
				       __franken_fd[fd].st.st_size, !n_block++);
			break;
		case S_IFSOCK:
			register_net(fd);
			break;
		}
	}

	/* FIXME: put all generic setup here? */
	mount_tmpfs();
}
