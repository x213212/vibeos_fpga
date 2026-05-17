#ifndef __OS_H__
#define __OS_H__

#include "riscv.h"
#include "lib.h"
#include "task.h"
#include "timer.h"
#include "string.h"

extern void panic(const char *);
extern void user_init();
extern void os_kernel();
extern int os_main(void);

// PLIC
extern void plic_init();
extern int plic_claim();
extern void plic_complete(int);

// lock
extern void basic_lock();
extern void basic_unlock();

extern int atomic_swap(lock_t *);

extern void lock_init(lock_t *lock);

extern void lock_acquire(lock_t *lock);

extern void lock_free(lock_t *lock);

extern void page_init(void);
extern void page_test(void);
extern void mem_usage_info(uint32_t *total_pages, uint32_t *used_pages, uint32_t *free_pages, uint32_t *m_calls, uint32_t *f_calls);
extern void kernel_heap_range_info(uint32_t *start, uint32_t *end);
extern void *malloc(size_t size);
extern void free(void *p);
extern int appfs_open(const char *path, int flags);
extern int appfs_read(int fd, void *buf, size_t size);
extern int appfs_write(int fd, const void *buf, size_t size);
extern int appfs_seek(int fd, int offset, int whence);
extern int appfs_tell(int fd);
extern int appfs_unlink(const char *path);
extern int appfs_close(int fd);
extern int appfs_close_all(void);
extern void appfs_set_cwd(uint32_t cwd_bno, const char *cwd);

#endif
