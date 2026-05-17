#include "app.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    volatile unsigned int *p = (unsigned int *)0x80000000u;

    app_puts("WRITEFAULT start\n");
    app_puts("about to write kernel\n");
    *p = 0x12345678u;
    app_puts("write survived\n");

    app_exit(0);
}
