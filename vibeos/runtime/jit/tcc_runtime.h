#ifndef TCC_RUNTIME_H
#define TCC_RUNTIME_H

#include "riscv.h"
#include "stddef.h"
#include "types.h"

int os_jit_run(const char *source, int owner_win_id);
int os_jit_run_file(const char *path, int owner_win_id);
int os_jit_run_bg(const char *source, int owner_win_id, char *msg, size_t msg_size);
int os_jit_run_bg_file(const char *path, int owner_win_id, char *msg, size_t msg_size);
int os_jit_run_bg_debug_file(const char *path, int owner_win_id, char *msg, size_t msg_size);
void os_jit_ps(char *out, size_t out_size);
int os_jit_kill(int id, char *msg, size_t msg_size);
int os_jit_cancel_task(int task_id);
int os_jit_owner_active(int owner_win_id);
int os_jit_cancel_by_owner(int owner_win_id);
int os_jit_cancel_running_owner_from_trap(int owner_win_id);
void os_jit_shared_reset(void);
void os_jit_init(void);
void os_jit_cancel_trampoline(void);
void os_jit_debug_pause_trampoline(void);
void jit_uheap_info(uint32_t *base, uint32_t *size, uint32_t *used, uint32_t *free_bytes, uint32_t *blocks);

extern volatile reg_t jit_debug_resume_pc;

#endif
