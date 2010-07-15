
#include <sys/types.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <unistd.h>

#include <comm/sercomm_cons.h>

int errno;

int ioctl(int fd, long int request, ...) {
    return 0;
}

ssize_t __libc_write(int fd, const void *buf, size_t count) {
	char c;
	int i;

	switch(fd) {
	default:
	case 0:
		errno = EBADF;
		return -1;
	case 1:
	case 2:
		return sercomm_write(buf, count);
	}
}

ssize_t write(int fd, const void *buf, size_t count) {
	return __libc_write(fd, buf, count);
}

off_t lseek(int fd, off_t offset, int whence) {
	return offset;
}

void _exit(int status) {
	puts("Program exit\n");
	while(1) { }
}
