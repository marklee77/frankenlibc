#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/syscalls.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/major.h>
#include <linux/uio.h>

#include <asm/syscalls.h>
#include <asm/host_ops.h>

ssize_t __platform_write(int fd, const void *buf, size_t count);

static ssize_t file_write(struct file *fp, const char __user *s,
			  size_t n, loff_t *off)
{
	return __platform_write(1, s, n);
}

ssize_t __platform_read(int fd, void *buf, size_t count);

static ssize_t file_read(struct file *file, char __user *buf, size_t size,
			 loff_t *ppos)
{
	return __platform_read(0, buf, size);
}

static struct file_operations lkl_stdio_fops = {
	.write =	file_write,
	.read =		file_read,
};

static int __init lkl_stdio_init(void)
{
	int err;

	/* prepare /dev/console */
	err = register_chrdev(TTYAUX_MAJOR, "console", &lkl_stdio_fops);
	if (err < 0) {
		printk(KERN_ERR "can't register lkl stdio console.\n");
		return err;
	}

	return 0;
}

/* should be _before_ default_rootfs creation (noinitramfs.c) */
fs_initcall(lkl_stdio_init);
