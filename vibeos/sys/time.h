#ifndef _SYS_TIME_H
#define _SYS_TIME_H

#include <stdint.h>

typedef int64_t time_t;

struct timeval {
    time_t tv_sec;
    long tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

int gettimeofday(struct timeval *tv, void *tz);

#endif
