#define __KERNEL__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#if 0
static ssize_t file_write(struct file *fp, const char __user *s,
			  size_t n, loff_t *off)
{
	lkl_ops->print(s, n);
	return n;
}

static ssize_t file_read(struct file *file, char __user *buf, size_t size,
			 loff_t *ppos)
{
	struct iovec iov;

	iov.iov_base = buf;
	iov.iov_len = size;

	return -preadv(0, &iov, 1, 0);
}

static struct file_operations lkl_stdio_fops = {
	//.owner		= THIS_MODULE,
	.write =	file_write,
	.read =		file_read,
};

#endif
static int __init lkl_stdio_init(void)
{
	int err = -1;

	/* prepare /dev/console */
	//err = register_chrdev(TTYAUX_MAJOR, "console", &lkl_stdio_fops);
	if (err < 0) {
		printk(KERN_ERR "can't register lkl stdio console.\n");
		return err;
	}

	return 0;
}
/* should be _before_ default_rootfs creation (noinitramfs.c) */
fs_initcall(lkl_stdio_init);
