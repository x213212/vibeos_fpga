#include "console_api.h"

#define UART_THR ((volatile uint8_t *)0x10000000u)
#define UART_LSR ((volatile uint8_t *)0x10000005u)
#define UART_LSR_EMPTY_MASK 0x40u

void console_putc(char ch)
{
    while ((*UART_LSR & UART_LSR_EMPTY_MASK) == 0) {}
    *UART_THR = (uint8_t)ch;
}

void console_puts(const char *s)
{
    while (*s) console_putc(*s++);
}

void console_puthex(uint32_t v)
{
    static const char hex[] = "0123456789abcdef";
    console_puts("0x");
    for (int i = 7; i >= 0; i--) {
        console_putc(hex[(v >> (i * 4)) & 0xf]);
    }
}

void console_putdec(uint32_t v)
{
    char buf[11];
    int i = 0;
    if (v == 0) {
        console_putc('0');
        return;
    }
    while (v && i < 10) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i--) console_putc(buf[i]);
}
