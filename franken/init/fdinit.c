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

static int disk_id;

static int nd_id;
union lkl_netdev {
        int fd;
};
int lkl_netdev_add(union lkl_netdev nd, void *mac);
int lkl_if_up(int ifindex);
int lkl_netdev_get_ifindex(int id);

enum rump_etfs_type {
	RUMP_ETFS_REG,
	RUMP_ETFS_BLK,
	RUMP_ETFS_CHR,
	RUMP_ETFS_DIR,
	RUMP_ETFS_DIR_SUBDIRS
};

int rump_pub_etfs_register(const char *, const char *, enum rump_etfs_type);

struct __fdtable __franken_fd[MAXFD];

void
mkkey(char *k, char *n, const char *pre, int dev, int fd)
{
	int i, d;
	int len = strlen(pre);

	if (fd > 99 || dev > 99) abort();
	for (i = 0; i < len; i++)
		*k++ = pre[i];
	if (dev > 9) {
		d = (dev / 10) + '0';
		*k++ = d;
		dev /= 10;
	}
	d = dev + '0';
	*k++ = d;
	*k++ = 0;

	if (fd > 9) {
		d = (fd / 10) + '0';
		*n++ = d;
		fd /= 10;
	}
	d = fd + '0';
	*n++ = d;
	*n++ = 0;
}

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
			/* notify virtio-mmio dev id */
			union lkl_disk_backstore disk;
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
			/* notify virtio-mmio dev id */
			union lkl_netdev nd;
			nd.fd = fd;
			nd_id = lkl_netdev_add(nd, NULL);
			break;
		}
	}
}

int rump___sysimpl_open(const char *, int, ...);
int rump___sysimpl_dup2(int, int);
int rump___sysimpl_close(int);

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

static int
register_reg(int dev, int fd, int flags)
{
	char key[16], num[16];

	mkkey(key, num, "/dev/vfile", dev, fd);
	rump_pub_etfs_register(key, num, RUMP_ETFS_REG);
	return rump___sysimpl_open(key, flags);
}

static void
register_net(int fd)
{
	/* FIXME: hehe always fixme tagged.. */
	lkl_if_up(lkl_netdev_get_ifindex(nd_id));
	lkl_if_set_ipv4(lkl_netdev_get_ifindex(nd_id), 0x0200010a /* 10.1.0.2 */,
			24);
#if 0
	char key[16], num[16];
	int ret;
	int sock;
	char *addr, *mask, *gw;
	int af = AF_UNSPEC;
	struct ifcapreq cap;

	mkkey(key, num, "virt", fd, fd);
	ret = rump_pub_netconfig_ifcreate(key);
	if (ret == 0) {
		sock = rump___sysimpl_socket30(AF_INET6, SOCK_STREAM, 0);
		if (sock != -1) {
			rump_pub_netconfig_auto_ipv6(key);
			rump___sysimpl_close(sock);
			af = AF_INET6;
		}
		sock = rump___sysimpl_socket30(AF_INET, SOCK_STREAM, 0);
		if (sock != -1) {
			/* XXX move to autodetect later, but gateway complex */
			addr = getenv("FIXED_ADDRESS");
			mask = getenv("FIXED_MASK");
			gw = getenv("FIXED_GATEWAY");
			if (addr == NULL || mask == NULL || gw == NULL) {
				rump_pub_netconfig_dhcp_ipv4_oneshot(key);
			} else {
				rump_pub_netconfig_ifup(key);
				rump_pub_netconfig_ipv4_ifaddr_cidr(key, addr, atoi(mask));
				rump_pub_netconfig_ipv4_gw(gw);
			}
			rump___sysimpl_close(sock);
			af = AF_INET;
		}
		/* enable offload features */
		sock = rump___sysimpl_socket30(af, SOCK_STREAM, 0);
		if (sock != -1) {
			strncpy(&cap.ifcr_name, key, IFNAMSIZ);
			ret = rump___sysimpl_ioctl(sock, SIOCGIFCAP, &cap);
			if (ret != -1) {
				cap.ifcr_capenable = cap.ifcr_capabilities;
				rump___sysimpl_ioctl(sock, SIOCSIFCAP, &cap);
			}
			rump___sysimpl_close(sock);
		}
	}
#endif
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
	int n_reg = 0, n_block = 0;

	if (__franken_fd[0].valid) {
		fd = register_reg(n_reg++, 0, O_RDONLY);
		if (fd != -1) {
			rump___sysimpl_dup2(fd, 0);
			rump___sysimpl_close(fd);
		}
	}
	if (__franken_fd[1].valid) {
		fd = register_reg(n_reg++, 1, O_WRONLY);
		if (fd != -1) {
			rump___sysimpl_dup2(fd, 1);
			rump___sysimpl_close(fd);
		}
	}

	if (__franken_fd[2].valid) {
		fd = register_reg(n_reg++, 2, O_WRONLY);
		if (fd != -1) {
			rump___sysimpl_dup2(fd, 2);
			rump___sysimpl_close(fd);
		}
	}

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
			fd = register_reg(n_reg++, fd, __franken_fd[fd].flags & O_ACCMODE);
			break;
		case S_IFBLK:
			register_block(n_block++, fd, __franken_fd[fd].flags & O_ACCMODE, __franken_fd[fd].st.st_size, 0);
			break;
		case S_IFSOCK:
			register_net(fd);
			break;
		}
	}

	/* now some generic stuff */
	mount_tmpfs();
}
