#ifndef TASK_H
#define TASK_H

#include "types.h"
#include "riscv.h"

#define MAX_TASKS 16
#define MAX_TASKLEVELS 4
#define STACK_SIZE 65536

extern int taskTop;
void task_init(void);
int  task_create(void (*task)(void), int level, int priority);
void task_reset(int task_id, void (*task)(void), int level, int priority);
void task_go(int i);
void task_os();
void task_run(int task_id, int level, int priority);
int  task_next(void);
void task_sleep(int task_id);
void task_wake(int task_id);
int  task_current(void);
void task_sleep_current(void);

#endif
