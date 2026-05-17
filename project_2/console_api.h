#ifndef CONSOLE_API_H
#define CONSOLE_API_H

#include <stdint.h>

void console_putc(char ch);
void console_puts(const char *s);
void console_puthex(uint32_t v);
void console_putdec(uint32_t v);

#endif
