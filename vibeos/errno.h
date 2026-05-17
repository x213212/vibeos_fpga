#ifndef ERRNO_H
#define ERRNO_H
extern int errno;
#define EAGAIN 11
#define EWOULDBLOCK EAGAIN
#define EIO 5
#define EPIPE 32
#define ENOENT 2
#define EPERM 1
#define EACCES 13
#define EINTR 4
#define ETIMEDOUT 110
#define ENOMEM 12
#define EINVAL 22
#define E2BIG  7
#define EILSEQ 84
#endif
