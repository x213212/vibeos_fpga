#ifndef __TIMER_H__
#define __TIMER_H__

#include "riscv.h"
#include "sys.h"
#include "lib.h"
#include "task.h"

extern void timer_handler();
extern void timer_init();
extern unsigned int get_millisecond_timer(void);
extern unsigned int sys_now(void);
extern unsigned int get_wall_clock_seconds(void);
extern void set_wall_clock_unix_seconds(unsigned int unix_seconds);
extern int wall_clock_is_synced(void);
extern void vibe_sntp_set_system_time(unsigned int unix_seconds);

#endif
