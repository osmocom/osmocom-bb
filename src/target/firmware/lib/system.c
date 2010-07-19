
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <comm/sercomm_cons.h>

int errno;

int ioctl(int fd, long int request, ...) {
    return 0;
}

extern char _end;

const char *heaptop = &_end;

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
	if(addr || (fd != -1) || offset) {
		errno = ENOTSUP;
		return NULL;
	}
	if(flags != (MAP_PRIVATE|MAP_ANONYMOUS)) {
		errno = ENOTSUP;
		return NULL;
	}
	void *result = heaptop;
	heaptop += length;
	return result;
}

void *mremap(void *old_address, size_t old_size, size_t new_size, unsigned long flags) {
	if(new_size <= old_size) {
		return old_address;
	}
	if(!(flags & MREMAP_MAYMOVE)) {
		errno = ENOTSUP;
		return NULL;
	}

	return mmap(NULL, new_size, 0, (MAP_PRIVATE|MAP_ANONYMOUS), -1, 0);
}

int munmap(void *addr, size_t length) {
	return 0;
}

int fstat(int fd, struct stat *buf) {
	errno = ENOENT;
	return -1;
}

int __libc_open(const char *pathname, int flags) {
	errno = ENOENT;
	return -1;
}

int __libc_close(int fd) {
	return 0;
}

ssize_t __libc_read(int fd, void *buf, size_t count) {
	errno = ENOTSUP;
	return -1;
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

int __rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldsetm, long nr) {
	return 0;
}


/* XXX: very bad */

double floor(double x) {
	return 0.0;
}
double log(double x) {
	return 0.0;
}
double exp(double x) {
	return 0.0;
}

