#include "app.h"

extern int main(int argc, char **argv);

__attribute__((noinline, used)) void crt0_entry(unsigned long *frame) {
    int argc = (int)frame[0];
    char **argv = (char **)&frame[1];
    int rc = main(argc, argv);
    app_exit(rc);
}

__attribute__((naked, section(".text.start")))
void _start(void) {
    asm volatile(
        "mv a0, sp\n\t"
        "j crt0_entry\n\t"
    );
}
