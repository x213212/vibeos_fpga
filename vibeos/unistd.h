#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>

typedef long off_t;
// ssize_t is already defined in stddef.h as signed int

int open(const char *pathname, int flags, ...);
int close(int fd);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
off_t lseek(int fd, off_t offset, int whence);
int unlink(const char *pathname);
int getpagesize(void);

extern char **environ;

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#endif
