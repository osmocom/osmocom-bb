
#include <fcntl.h>
#include <errno.h>
#include <string.h>

/* max number of file descriptors (would-be per-process) */
#define MAXFDS 8
/* max number of open files */
#define MAXFILES 8

/* forward declarations */
struct file;
struct file_operations;

/* function pointer types for operations */
typedef int (*fop_sync_t) (struct file * fd);
typedef off_t(*fop_seek_t) (struct file * fd, off_t offset, int whence);
typedef ssize_t(*fop_read_t) (struct file * fd, void *buf, size_t size);
typedef ssize_t(*fop_write_t) (struct file * fd, const void *buf, size_t size);
typedef int (*fop_close_t) (struct file * fd);

/* structure describing operations for file */
struct file_operations {
	fop_sync_t fop_sync;
	fop_seek_t fop_seek;
	fop_read_t fop_read;
	fop_write_t fop_write;
	fop_close_t fop_close;
};

/* structure describing the state behind a file descriptor */
struct file {
	/* refcount, -1 used on allocation */
	int8_t f_used;
	/* file flags (O_RDONLY and so forth) */
	int f_flags;
	/* object-specific operation structure */
	struct file_operations *f_ops;
	/* object-specific file structure */
	void *f_cookie;
};

/* "table" of all file descriptors */
struct file *open_fds[MAXFDS];

/* pre-allocated descriptor structures */
struct file open_files[MAXFILES];

ssize_t scc_write(struct file *f, const void *buf, size_t size)
{
	return sercomm_write(buf, size);
}

struct file_operations sercomm_console_operations = {
	.fop_write = &scc_write
};

__attribute__ ((constructor))
void file_table_init(void)
{
	/* clear all structures */
	memset(open_files, 0, sizeof(open_files));
	memset(open_fds, 0, sizeof(open_fds));

	/* stdin */
	open_files[0].f_used = 1;
	open_files[0].f_flags = O_RDONLY;
	open_files[0].f_ops = &sercomm_console_operations;
	open_fds[0] = &open_files[0];

	/* stdout */
	open_files[1].f_used = 1;
	open_files[1].f_flags = O_WRONLY;
	open_files[1].f_ops = &sercomm_console_operations;
	open_fds[1] = &open_files[1];

	/* stderr */
	open_files[2].f_used = 1;
	open_files[2].f_flags = O_WRONLY;
	open_files[2].f_ops = &sercomm_console_operations;
	open_fds[2] = &open_files[2];
}

struct file *file_alloc()
{
	int i;
	for (i = 0; i < MAXFILES; i++) {
		if (!open_files[i].f_used) {
			open_files[i].f_used = -1;
			return &open_files[i];
		}
	}

	errno = ENFILE;
	return NULL;
}

int file_ref(struct file *fd)
{
	if (fd->f_used == -1) {
		fd->f_used = 1;
	} else {
		fd->f_used++;
	}
	return fd->f_used;
}

int file_unref(struct file *fd)
{
	if (fd->f_used <= 0) {
		fd->f_used = 0;
	} else {
		fd->f_used--;
	}
	return fd->f_used;
}

int file_sync(struct file *fd)
{
	/* pass through to the implementation, if present. else fail. */
	if (fd->f_ops->fop_sync) {
		return fd->f_ops->fop_sync(fd);
	} else {
		errno = EOPNOTSUPP;
		return -1;
	}
}

off_t file_seek(struct file * fd, off_t offset, int whence)
{
	/* pass through to the implementation, if present. else fail. */
	if (fd->f_ops->fop_seek) {
		return fd->f_ops->fop_seek(fd, offset, whence);
	} else {
		errno = EOPNOTSUPP;
		return -1;
	}
}

size_t file_read(struct file * fd, void *buf, size_t count)
{
	/* pass through to the implementation, if present. else fail. */
	if (fd->f_ops->fop_read) {
		return fd->f_ops->fop_read(fd, buf, count);
	} else {
		errno = EOPNOTSUPP;
		return -1;
	}
}

size_t file_write(struct file * fd, const void *buf, size_t count)
{
	/* pass through to the implementation, if present. else fail. */
	if (fd->f_ops->fop_write) {
		return fd->f_ops->fop_write(fd, buf, count);
	} else {
		errno = EOPNOTSUPP;
		return -1;
	}
}

int file_close(struct file *fd)
{
	/* unref and really close if at last ref */
	if (!file_unref(fd)) {
		return 0;
	}

	/* call object implementation once */
	if (fd->f_ops->fop_close) {
		return fd->f_ops->fop_close(fd);
	}

	/* clear structure */
	memset(fd, 0, sizeof(struct file));

	/* done */
	return 0;
}

int fd_alloc(struct file *file)
{
	int i;
	for (i = 0; i < MAXFDS; i++) {
		if (!open_fds[i]) {
			file_ref(file);
			open_fds[i] = file;
			return i;
		}
	}

	errno = EMFILE;
	return -1;
}

struct file *file_for_fd(int fd)
{
	if (fd >= MAXFDS) {
		errno = EBADF;
		return NULL;
	}

	if (!open_fds[fd]) {
		errno = EBADF;
		return NULL;
	}

	return open_fds[fd];
}

int __libc_open(const char *pathname, int flags, ...)
{
	errno = EOPNOTSUPP;
	return -1;
}

int open(const char *pathname, int flags, ...)
    __attribute__ ((weak, alias("__libc_open")));

int __libc_close(int fd)
{
	struct file *file = file_for_fd(fd);
	if (!file) {
		errno = EBADF;
		return -1;
	}

	int res = file_close(file);
	if (res < 0) {
		return res;
	}

	open_fds[fd] = NULL;

	return 0;
}

int close(int fd)
    __attribute__ ((weak, alias("__libc_close")));

ssize_t __libc_read(int fd, void *buf, size_t count)
{
	struct file *f = file_for_fd(fd);
	if (!f) {
		errno = EBADF;
		return -1;
	}

	return file_read(f, buf, count);
}

ssize_t read(int fd, void *buf, size_t count)
    __attribute__ ((weak, alias("__libc_read")));

ssize_t __libc_write(int fd, const void *buf, size_t count)
{
	struct file *f = file_for_fd(fd);
	if (!f) {
		errno = EBADF;
		return -1;
	}

	return file_write(f, buf, count);
}

ssize_t write(int fd, const void *buf, size_t count)
    __attribute__ ((weak, alias("__libc_write")));

off_t __libc_lseek(int fd, off_t offset, int whence)
{
	struct file *f = file_for_fd(fd);
	if (!f) {
		errno = EBADF;
		return -1;
	}

	return file_seek(f, offset, whence);
}

off_t lseek(int fd, off_t offset, int whence)
    __attribute__ ((weak, alias("__libc_lseek")));

int __libc_fstat(int fd, struct stat *buf)
{
	errno = EOPNOTSUPP;
	return -1;
}

int fstat(int fd, struct stat *buf)
    __attribute__ ((weak, alias("__libc_fstat")));

int __libc_ioctl(int fd, long int request, ...)
{
	errno = EOPNOTSUPP;
	return -1;
}

int ioctl(int fd, long int request, ...)
    __attribute__ ((weak, alias("__libc_ioctl")));
