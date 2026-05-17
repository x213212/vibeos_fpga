#include "sys/time.h"
#include "timer.h"

int gettimeofday(struct timeval *tv, void *tzp)
{
    (void)tzp;
    if (tv != 0) {
        unsigned int ms = get_millisecond_timer();
        tv->tv_sec = (time_t)(ms / 1000U);
        tv->tv_usec = (long)((ms % 1000U) * 1000U);
    }
    return 0;
}
