#include "task.h"
#include "lib.h"

#define TASK_UNUSED 0
#define TASK_SLEEPING 1
#define TASK_RUNNING 2
#define LEVEL0_QUOTA 3

struct task_level {
	int tasks[MAX_TASKS];
	int running;
	int now;
};

struct task_meta {
	int flags;
	int level;
	int priority;
};

uint8_t task_stack[MAX_TASKS][STACK_SIZE];
struct context ctx_os;
struct context ctx_tasks[MAX_TASKS];
struct context *ctx_now;
static struct task_level task_levels[MAX_TASKLEVELS];
static struct task_meta task_meta[MAX_TASKS];
static int current_task_id = -1;
static int current_level = 0;
static int current_level_quota = LEVEL0_QUOTA;
int taskTop = 0; // total number of task

#ifdef FPGA_MINIMAL
static void task_zero_words(void *ptr, int bytes)
{
	uint32_t *p = (uint32_t *)ptr;
	int words = bytes / (int)sizeof(uint32_t);
	for (int i = 0; i < words; i++)
		p[i] = 0;
}
#endif

void task_init(void)
{
#ifdef FPGA_MINIMAL
	task_zero_words(&ctx_os, sizeof(ctx_os));
	task_zero_words(ctx_tasks, sizeof(ctx_tasks));
	task_zero_words(task_levels, sizeof(task_levels));
	task_zero_words(task_meta, sizeof(task_meta));
#else
	memset(&ctx_os, 0, sizeof(ctx_os));
	memset(ctx_tasks, 0, sizeof(ctx_tasks));
	memset(task_levels, 0, sizeof(task_levels));
	memset(task_meta, 0, sizeof(task_meta));
#endif
	ctx_now = &ctx_os;
	current_task_id = -1;
	current_level = 0;
	current_level_quota = LEVEL0_QUOTA;
	taskTop = 0;
}

static void task_add(int task_id)
{
	int level = task_meta[task_id].level;
	if (level < 0 || level >= MAX_TASKLEVELS)
		level = task_meta[task_id].level = 0;
	struct task_level *tl = &task_levels[level];
	if (tl->running < 0 || tl->running > MAX_TASKS) {
		tl->running = 0;
		tl->now = 0;
	}
	if (tl->running >= MAX_TASKS)
		panic("too many runnable tasks");
	tl->tasks[tl->running++] = task_id;
	task_meta[task_id].flags = TASK_RUNNING;
}

static void task_remove(int task_id)
{
	int level = task_meta[task_id].level;
	if (level < 0 || level >= MAX_TASKLEVELS) {
		task_meta[task_id].level = 0;
		task_meta[task_id].flags = TASK_SLEEPING;
		return;
	}
	struct task_level *tl = &task_levels[level];
	if (tl->running < 0 || tl->running > MAX_TASKS) {
		tl->running = 0;
		tl->now = 0;
		task_meta[task_id].flags = TASK_SLEEPING;
		return;
	}
	int i;
	for (i = 0; i < tl->running; i++) {
		if (tl->tasks[i] == task_id)
			break;
	}
	if (i == tl->running)
		return;
	tl->running--;
	if (i < tl->now)
		tl->now--;
	if (tl->now >= tl->running)
		tl->now = 0;
	for (; i < tl->running; i++) {
		int next = tl->tasks[i + 1];
		if (next < 0 || next >= taskTop)
			next = 0;
		tl->tasks[i] = next;
	}
	task_meta[task_id].flags = TASK_SLEEPING;
}

static void task_switchsub(void)
{
	for (int i = 0; i < MAX_TASKLEVELS; i++) {
		if (task_levels[i].running > 0) {
			current_level = i;
			current_level_quota = (i == 0) ? LEVEL0_QUOTA : 1;
			return;
		}
	}
	current_level = 0;
	current_level_quota = LEVEL0_QUOTA;
}

// create a new task
int task_create(void (*task)(void), int level, int priority)
{
	if (taskTop >= MAX_TASKS)
		panic("too many tasks");
	int i = taskTop++;
	memset(&ctx_tasks[i], 0, sizeof(ctx_tasks[i]));
	ctx_tasks[i].ra = (reg_t)task;
	ctx_tasks[i].sp = ((reg_t)&task_stack[i][STACK_SIZE]) & ~(reg_t)0xF;
	task_meta[i].flags = TASK_SLEEPING;
	task_meta[i].level = 0;
	task_meta[i].priority = 1;
	task_run(i, level, priority);
	return i;
}

void task_reset(int task_id, void (*task)(void), int level, int priority)
{
	if (task_id < 0 || task_id >= taskTop)
		return;
	if (task_meta[task_id].flags == TASK_RUNNING) {
		task_remove(task_id);
	}
	memset(&ctx_tasks[task_id], 0, sizeof(ctx_tasks[task_id]));
	ctx_tasks[task_id].ra = (reg_t)task;
	ctx_tasks[task_id].sp = ((reg_t)&task_stack[task_id][STACK_SIZE]) & ~(reg_t)0xF;
	task_meta[task_id].flags = TASK_SLEEPING;
	task_meta[task_id].level = 0;
	task_meta[task_id].priority = 1;
	task_run(task_id, level, priority);
}

void task_run(int task_id, int level, int priority)
{
	if (task_id < 0 || task_id >= taskTop)
		return;
	if (level < 0)
		level = task_meta[task_id].level;
	if (level >= MAX_TASKLEVELS)
		level = MAX_TASKLEVELS - 1;
	if (priority <= 0)
		priority = 1;
	if (task_meta[task_id].flags == TASK_RUNNING && task_meta[task_id].level != level)
		task_remove(task_id);
	task_meta[task_id].level = level;
	task_meta[task_id].priority = priority;
	if (task_meta[task_id].flags != TASK_RUNNING)
		task_add(task_id);
	task_switchsub();
}

void task_sleep(int task_id)
{
	if (task_id < 0 || task_id >= taskTop)
		return;
	if (task_meta[task_id].flags != TASK_RUNNING)
		return;
	int self = (task_id == current_task_id);
	task_remove(task_id);
	task_switchsub();
	if (self)
		task_os();
}

void task_wake(int task_id)
{
	if (task_id < 0 || task_id >= taskTop)
		return;
	if (task_meta[task_id].flags == TASK_RUNNING)
		return;
	task_add(task_id);
	task_switchsub();
}

int task_current(void)
{
	return current_task_id;
}

void task_sleep_current(void)
{
	if (current_task_id >= 0)
		task_sleep(current_task_id);
}

int task_next(void)
{
	int chosen_level = -1;
	if (current_level >= 0 && current_level < MAX_TASKLEVELS &&
	    task_levels[current_level].running > 0 && current_level_quota > 0) {
		chosen_level = current_level;
		current_level_quota--;
	} else {
		for (int step = 1; step <= MAX_TASKLEVELS; step++) {
			int level = (current_level + step) % MAX_TASKLEVELS;
			if (task_levels[level].running > 0) {
				chosen_level = level;
				current_level = level;
				current_level_quota = (level == 0) ? (LEVEL0_QUOTA - 1) : 0;
				break;
			}
		}
		if (chosen_level < 0) {
			for (int i = 0; i < MAX_TASKLEVELS; i++) {
				if (task_levels[i].running > 0) {
					chosen_level = i;
					current_level = i;
					current_level_quota = (i == 0) ? (LEVEL0_QUOTA - 1) : 0;
					break;
				}
			}
		}
	}
	if (chosen_level < 0)
		return -1;

	struct task_level *tl = &task_levels[chosen_level];
	if (tl->running <= 0)
		return -1;
	if (tl->now >= tl->running)
		tl->now = 0;
	int task_id = tl->tasks[tl->now];
	tl->now++;
	if (tl->now >= tl->running)
		tl->now = 0;
	if (task_id < 0 || task_id >= taskTop)
		return -1;
	if (task_meta[task_id].flags != TASK_RUNNING)
		return -1;
	if (ctx_tasks[task_id].ra == 0 || ctx_tasks[task_id].sp == 0) {
		task_remove(task_id);
		return -1;
	}
	return task_id;
}

// switch to task[i]
void task_go(int i)
{
	if (i < 0 || i >= taskTop)
		return;
	if (task_meta[i].flags != TASK_RUNNING)
		return;
	if (ctx_tasks[i].ra == 0 || ctx_tasks[i].sp == 0) {
		task_remove(i);
		return;
	}
	current_task_id = i;
	ctx_now = &ctx_tasks[i];
	sys_switch(&ctx_os, &ctx_tasks[i]);
}

// switch back to os
void task_os()
{
	if (current_task_id < 0 || current_task_id >= taskTop)
		return;
	struct context *ctx = &ctx_tasks[current_task_id];
	current_task_id = -1;
	ctx_now = &ctx_os;
	sys_switch(ctx, &ctx_os);
}
