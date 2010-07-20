
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <comm/sercomm_cons.h>

int errno;

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

