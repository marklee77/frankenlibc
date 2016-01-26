#ifndef _UNISTD_H_
#define _UNISTD_H_

#include <stdlib.h>
#include <sys/types.h>

#ifndef	_STDIO_H		/* <stdio.h> has the same definitions.  */
# define SEEK_SET	0	/* Seek from beginning of file.  */
# define SEEK_CUR	1	/* Seek from current position.  */
# define SEEK_END	2	/* Seek from end of file.  */
# ifdef __USE_GNU
#  define SEEK_DATA	3	/* Seek to next data.  */
#  define SEEK_HOLE	4	/* Seek to next hole.  */
# endif
#endif

#define fsync(f) __platform_fsync(f)
#define getpagesize __platform_getpagesize
#define lseek(a, b, c) __platform_lseek(a, b, c)
#define pread(a, b, c, d) __platform_pread(a, b, c, d)
#define pwrite(a, b, c, d) __platform_pwrite(a, b, c, d)
#define read(f, b, c) __platform_read(f, b, c)
#define write(f, b, c) __platform_write(f, b, c)

void _exit(int) __attribute__ ((noreturn));
int fsync(int);
int getpagesize(void);
off_t lseek(int, off_t, int);
ssize_t pread(int, void *, size_t, off_t);
ssize_t pwrite(int, const void *, size_t, off_t);
ssize_t read(int, void *, size_t);
ssize_t write(int, const void *, size_t);

#endif
