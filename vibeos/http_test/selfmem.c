#include "app.h"

static unsigned int bss_marker;
static unsigned int data_marker = 0x13572468u;

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    unsigned int stack_marker = 0x2468ace0u;
    unsigned int *heap = (unsigned int *)app_malloc(16);

    app_puts("SELFMEM start\n");

    app_puts("bss=");
    app_puthex((unsigned long)&bss_marker);
    app_putc('\n');
    app_puts("data=");
    app_puthex((unsigned long)&data_marker);
    app_putc('\n');
    app_puts("stack=");
    app_puthex((unsigned long)&stack_marker);
    app_putc('\n');

    app_puts("bss0=");
    app_puthex(bss_marker);
    app_putc('\n');
    app_puts("data0=");
    app_puthex(data_marker);
    app_putc('\n');
    app_puts("stack0=");
    app_puthex(stack_marker);
    app_putc('\n');

    bss_marker = 0xabcdef01u;
    data_marker = 0x10203040u;
    stack_marker = 0x55667788u;
    if (heap) {
        heap[0] = 0x11223344u;
        heap[1] = 0x99aabbccu;
    }

    app_puts("bss1=");
    app_puthex(bss_marker);
    app_putc('\n');
    app_puts("data1=");
    app_puthex(data_marker);
    app_putc('\n');
    app_puts("stack1=");
    app_puthex(stack_marker);
    app_putc('\n');
    app_puts("heap0=");
    app_puthex(heap ? heap[0] : 0);
    app_putc('\n');
    app_puts("heap1=");
    app_puthex(heap ? heap[1] : 0);
    app_putc('\n');

    if (heap) {
        app_free(heap);
    }

    app_puts("SELFMEM ok\n");
    app_exit(0);
}
