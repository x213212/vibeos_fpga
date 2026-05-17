#ifndef APP_H
#define APP_H

#define APP_SYS_PUTC   1
#define APP_SYS_PUTS   2
#define APP_SYS_MALLOC 3
#define APP_SYS_FREE   4
#define APP_SYS_TIME   5
#define APP_SYS_YIELD  6
#define APP_SYS_OPEN   7
#define APP_SYS_READ   8
#define APP_SYS_WRITE  9
#define APP_SYS_EXIT   10
#define APP_SYS_CLOSE  11

#define APP_O_READ    0x01
#define APP_O_WRITE   0x02
#define APP_O_CREAT   0x100
#define APP_O_TRUNC   0x200

static inline __attribute__((noreturn)) void app_unreachable(void) {
#if defined(__GNUC__) && !defined(__TINYC__)
    __builtin_unreachable();
#else
    for (;;) {
    }
#endif
}

#ifdef __TINYC__
long app_ecall(long a0, long a1, long a2, long a7);
#else
static inline long app_ecall(long a0, long a1, long a2, long a7) {
    register long ra0 asm("a0") = a0;
    register long ra1 asm("a1") = a1;
    register long ra2 asm("a2") = a2;
    register long ra7 asm("a7") = a7;
    asm volatile("ecall"
                 : "+r"(ra0)
                 : "r"(ra1), "r"(ra2), "r"(ra7)
                 : "memory");
    return ra0;
}
#endif

static inline void app_putc(char c) {
    app_ecall((long)c, 0, 0, APP_SYS_PUTC);
}

static inline void app_puts(const char *s) {
    while (*s) {
        app_putc(*s++);
    }
}

static inline void app_puthex(unsigned long v) {
    static const char hex[] = "0123456789abcdef";
    int shift = (int)(sizeof(unsigned long) * 8) - 4;
    app_putc('0');
    app_putc('x');
    for (; shift >= 0; shift -= 4) {
        app_putc(hex[(v >> shift) & 0xF]);
    }
}

static inline void app_putptr(const void *p) {
    app_puthex((unsigned long)p);
}

static inline void *app_malloc(unsigned long size) {
    return (void *)app_ecall((long)size, 0, 0, APP_SYS_MALLOC);
}

static inline void app_free(void *p) {
    app_ecall((long)p, 0, 0, APP_SYS_FREE);
}

static inline int app_open(const char *path, int flags) {
    return (int)app_ecall((long)path, (long)flags, 0, APP_SYS_OPEN);
}

static inline int app_read(int fd, void *buf, unsigned long size) {
    return (int)app_ecall((long)fd, (long)buf, (long)size, APP_SYS_READ);
}

static inline int app_write(int fd, const void *buf, unsigned long size) {
    return (int)app_ecall((long)fd, (long)buf, (long)size, APP_SYS_WRITE);
}

static inline int app_close(int fd) {
    return (int)app_ecall((long)fd, 0, 0, APP_SYS_CLOSE);
}

static inline unsigned long app_time_ms(void) {
    return (unsigned long)app_ecall(0, 0, 0, APP_SYS_TIME);
}

static inline void app_yield(void) {
    app_ecall(0, 0, 0, APP_SYS_YIELD);
}

static inline __attribute__((noreturn)) void app_exit(int code) {
    app_ecall((long)code, 0, 0, APP_SYS_EXIT);
    app_unreachable();
}

#endif
