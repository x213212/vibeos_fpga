#ifndef _FCNTL_H
#define _FCNTL_H

#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_CREAT   0x0100
#define O_EXCL    0x0200
#define O_NOCTTY  0x0400
#define O_TRUNC   0x0800
#define O_APPEND  0x1000
#define O_NONBLOCK 0x2000
#define O_SYNC    0x4000
#define O_ASYNC   0x8000
#define O_BINARY  0x0000 // RISC-V 上通常忽略這個，但 TCC 需要

int open(const char *pathname, int flags, ...);
int close(int fd);

#endif
