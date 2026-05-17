#include "app.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    volatile unsigned int *p = (unsigned int *)0x80000000u;
    void (*fn)(void) = (void (*)(void))0x80000000u;

    app_puts("FAULT TEST start\n");

    app_puts("about to read kernel\n");
    (void)*p;
    app_puts("read survived\n");

    app_puts("about to write kernel\n");
    *p = 0x12345678u;
    app_puts("write survived\n");

    app_puts("about to jump kernel\n");
    fn();
    app_puts("jump survived\n");

    app_exit(0);
}
