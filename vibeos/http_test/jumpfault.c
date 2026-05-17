#include "app.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    void (*fn)(void) = (void (*)(void))0x80000000u;

    app_puts("JUMPFAULT start\n");
    app_puts("about to jump kernel\n");
    fn();
    app_puts("jump survived\n");

    app_exit(0);
}
