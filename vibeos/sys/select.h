#ifndef SYS_SELECT_H
#define SYS_SELECT_H

#include "time.h"

typedef unsigned long fd_mask;

#define FD_SETSIZE 32

typedef struct {
    fd_mask fds_bits[(FD_SETSIZE + (8 * sizeof(fd_mask)) - 1) / (8 * sizeof(fd_mask))];
} fd_set;

#define FD_ZERO(set) do { \
    for (unsigned int _i = 0; _i < (unsigned int)(sizeof((set)->fds_bits)/sizeof((set)->fds_bits[0])); _i++) { \
        (set)->fds_bits[_i] = 0; \
    } \
} while (0)

#define FD_SET(fd, set) ((set)->fds_bits[(fd) / (8 * sizeof(fd_mask))] |= (1UL << ((fd) % (8 * sizeof(fd_mask)))))
#define FD_ISSET(fd, set) (((set)->fds_bits[(fd) / (8 * sizeof(fd_mask))] & (1UL << ((fd) % (8 * sizeof(fd_mask))))) != 0)

struct timeval;

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);

#endif
