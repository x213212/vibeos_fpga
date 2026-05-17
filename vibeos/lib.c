#include "lib.h"

extern void basic_lock(void);
extern void basic_unlock(void);

#define LSR_RX_READY (1 << 0)
#define EOF 0
#define UART_INPUT_QUEUE_SIZE 1024
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
static volatile uint16_t uart_input_head = 0;
static volatile uint16_t uart_input_tail = 0;
static char uart_input_queue[UART_INPUT_QUEUE_SIZE];

static void uart_input_push(char c)
{
    uint16_t next = (uint16_t)((uart_input_tail + 1) % UART_INPUT_QUEUE_SIZE);
    if (next == uart_input_head) {
        uart_input_head = (uint16_t)((uart_input_head + 1) % UART_INPUT_QUEUE_SIZE);
    }
    uart_input_queue[uart_input_tail] = c;
    uart_input_tail = next;
}

int uart_input_pop(void)
{
    if (uart_input_head == uart_input_tail) return -1;
    char c = uart_input_queue[uart_input_head];
    uart_input_head = (uint16_t)((uart_input_head + 1) % UART_INPUT_QUEUE_SIZE);
    return (unsigned char)c;
}

void uart_init()
{
    /* disable interrupts */
    UART_REGW(UART_IER, 0x00);

    /* Baud rate setting */
    uint8_t lcr = UART_REGR(UART_LCR);
    UART_REGW(UART_LCR, lcr | (1 << 7));
    UART_REGW(UART_DLL, 0x03);
    UART_REGW(UART_DLM, 0x00);

    lcr = 0;
    UART_REGW(UART_LCR, lcr | (3 << 0));

    uint8_t ier = UART_REGR(UART_IER);
    UART_REGW(UART_IER, ier | (1 << 0));
}

char *lib_gets(char *s)
{
    int ch;
    char *p = s;

    while ((ch = lib_getc()) != '\n' && ch != EOF)
    {
        if (ch == -1)
        {
            continue;
        }
        *s = (char)ch;
        s++;
    }

    *s = '\0';
    return p;
}

int lib_getc(void)
{
    if (*UART_LSR & LSR_RX_READY)
    {
        return *UART_RHR == '\r' ? '\n' : *UART_RHR;
    }
    else
    {
        return -1;
    }
}

void lib_isr(void)
{
    for (;;)
    {
        int c = lib_getc();
        if (c == -1)
        {
            break;
        }
        else
        {
            uart_input_push((char)c);
        }
    }
}

void lib_delay(volatile int count)
{
    count *= 50000;
    while (count--)
        ;
}

int lib_putc(char ch)
{
#ifdef FPGA_MINIMAL
    for (int i = 0; i < 4096; i++) {
        if ((*UART_LSR & UART_LSR_EMPTY_MASK) != 0) return *UART_THR = ch;
    }
    return -1;
#else
    while ((*UART_LSR & UART_LSR_EMPTY_MASK) == 0)
        ;
    return *UART_THR = ch;
#endif
}

static void lib_puts_unlocked(char *s)
{
    while (*s) {
        if (lib_putc(*s++) < 0) break; // Stop if failed
    }
}

void lib_puts(char *s)
{
    basic_lock();
    lib_puts_unlocked(s);
    basic_unlock();
}

int puts(const char *s)
{
    lib_puts((char *)s);
    return 0;
}

// 64-bit unsigned modulo, return remainder
uint64_t __umoddi3(uint64_t n, uint64_t d) {
    uint64_t r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
        }
    }
    return r;
}

// 64-bit unsigned division software implementation for __udivdi3
uint64_t __udivdi3(uint64_t numerator, uint64_t denominator) {
    uint64_t quotient = 0, remainder = 0;
    int i;

    if (denominator == 0) {
        // Handle divide by zero if needed
        return 0xFFFFFFFFFFFFFFFFULL;
    }

    for (i = 63; i >= 0; i--) {
        remainder = (remainder << 1) | ((numerator >> i) & 1);
        if (remainder >= denominator) {
            remainder -= denominator;
            quotient |= (1ULL << i);
        }
    }
    return quotient;
}

// Signed division
int64_t __divdi3(int64_t n, int64_t d) {
    int sign = 1;
    if (n < 0) { n = -n; sign = -sign; }
    if (d < 0) { d = -d; sign = -sign; }
    uint64_t q = __udivdi3((uint64_t)n, (uint64_t)d);
    return sign * (int64_t)q;
}

// Signed modulo
int64_t __moddi3(int64_t n, int64_t d) {
    int sign = 1;
    if (n < 0) { n = -n; sign = -sign; }
    if (d < 0) { d = -d; }
    uint64_t r = __umoddi3((uint64_t)n, (uint64_t)d);
    return sign * (int64_t)r;
}
#define OUT_BUF_SIZE 1024

// UART output primitive (assume provided; keep your existing implementation)
extern int uart_putc(int c);

// Helper to append a character with bounds checking
static void put_char(char *out, size_t n, size_t *pos, char c) {
    if (out && *pos + 1 < n) {
        out[*pos] = c;
    }
    (*pos)++;
}

// Unsigned integer to string with base
static void print_unsigned(char *out, size_t n, size_t *pos, unsigned long long val, int base, int uppercase) {
    char buf[64];
    int idx = 0;
    if (val == 0) {
        buf[idx++] = '0';
    } else {
        while (val) {
            int d = val % base;
            if (d < 10)
                buf[idx++] = '0' + d;
            else
                buf[idx++] = (uppercase ? 'A' : 'a') + (d - 10);
            val /= base;
        }
    }
    for (int i = idx - 1; i >= 0; i--) {
        put_char(out, n, pos, buf[i]);
    }
}

// Signed integer
static void print_signed(char *out, size_t n, size_t *pos, long long val) {
    if (val < 0) {
        put_char(out, n, pos, '-');
        val = -val;
    }
    print_unsigned(out, n, pos, (unsigned long long)val, 10, 0);
}

// vsnprintf-like: if n == 0 or out == NULL, it only counts length.
int lib_vsnprintf(char *out, size_t n, const char *fmt, va_list ap) {
    size_t pos = 0;
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            put_char(out, n, &pos, *fmt);
            continue;
        }
        fmt++; // skip '%'
        if (*fmt == '%') {
            put_char(out, n, &pos, '%');
            continue;
        }

        int longlongarg = 0;
        if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') {
                longlongarg = 2;
                fmt++;
            } else {
                longlongarg = 1;
            }
        }

        switch (*fmt) {
        case 'd': {
            long long val;
            if (longlongarg == 2)
                val = va_arg(ap, long long);
            else if (longlongarg == 1)
                val = va_arg(ap, long);
            else
                val = va_arg(ap, int);
            print_signed(out, n, &pos, val);
            break;
        }
        case 'u': {
            unsigned long long val;
            if (longlongarg == 2)
                val = va_arg(ap, unsigned long long);
            else if (longlongarg == 1)
                val = va_arg(ap, unsigned long);
            else
                val = va_arg(ap, unsigned int);
            print_unsigned(out, n, &pos, val, 10, 0);
            break;
        }
        case 'x': {
            unsigned long long val;
            if (longlongarg == 2)
                val = va_arg(ap, unsigned long long);
            else if (longlongarg == 1)
                val = va_arg(ap, unsigned long);
            else
                val = va_arg(ap, unsigned int);
            print_unsigned(out, n, &pos, val, 16, 0);
            break;
        }
        case 'X': {
            unsigned long long val;
            if (longlongarg == 2)
                val = va_arg(ap, unsigned long long);
            else if (longlongarg == 1)
                val = va_arg(ap, unsigned long);
            else
                val = va_arg(ap, unsigned int);
            print_unsigned(out, n, &pos, val, 16, 1);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            put_char(out, n, &pos, c);
            break;
        }
        case 's': {
            const char *str = va_arg(ap, const char *);
            if (!str) str = "(null)";
            while (*str) {
                put_char(out, n, &pos, *str++);
            }
            break;
        }
        default:
            // unknown specifier, print literally
            put_char(out, n, &pos, '%');
            put_char(out, n, &pos, *fmt);
            break;
        }
    }

    // null terminate
    if (out) {
        if (pos < n)
            out[pos] = '\0';
        else if (n)
            out[n - 1] = '\0';
    }
    return (int)pos;
}

int lib_vprintf(const char *fmt, va_list ap) {
    static char buf[OUT_BUF_SIZE];
    va_list ap_copy;
    int len;

    basic_lock();
    va_copy(ap_copy, ap);
    len = lib_vsnprintf(NULL, 0, fmt, ap); // count required length
    if (len + 1 > (int)sizeof(buf)) {
        lib_puts_unlocked("error: lib_vprintf() output string size overflow\n");
        basic_unlock();
        while (1) { /* hang */ }
    }
    lib_vsnprintf(buf, len + 1, fmt, ap_copy);
    va_end(ap_copy);
    lib_puts_unlocked(buf);
    basic_unlock();
    return len;
}

int vsnprintf(char *out, size_t n, const char *fmt, va_list ap) {
    return lib_vsnprintf(out, n, fmt, ap);
}

int snprintf(char *out, size_t n, const char *fmt, ...) {
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = lib_vsnprintf(out, n, fmt, ap);
    va_end(ap);
    return ret;
}

int vsprintf(char *out, const char *fmt, va_list ap) {
    return lib_vsnprintf(out, (size_t)-1, fmt, ap);
}

int sprintf(char *out, const char *fmt, ...) {
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = lib_vsnprintf(out, (size_t)-1, fmt, ap);
    va_end(ap);
    return ret;
}

#define UART0 ((volatile uint32_t*)0x10000000)
int uart_putc(int c) {
#ifdef FPGA_MINIMAL
    for (int i = 0; i < 4096; i++) {
        if (((*UART0) & 0x80000000) == 0) {
            *UART0 = (uint32_t)c;
            return c;
        }
    }
    return -1;
#else
    while ((*UART0) & 0x80000000) ;
    *UART0 = (uint32_t)c;
    return c;
#endif
}
