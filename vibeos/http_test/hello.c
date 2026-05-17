#include "app.h"

static void put_dec(int v) {
    char buf[16];
    int i = 0;
    unsigned int n;
    if (v < 0) {
        app_putc('-');
        n = (unsigned int)(-v);
    } else {
        n = (unsigned int)v;
    }
    if (n == 0) {
        app_putc('0');
        return;
    }
    while (n > 0 && i < (int)sizeof(buf)) {
        buf[i++] = (char)('0' + (n % 10U));
        n /= 10U;
    }
    while (i > 0) {
        app_putc(buf[--i]);
    }
}

int main(int argc, char **argv) {
    const char msg[] = "file io ok\n";

    app_puts("APPV5 start\n");
    app_puts("argc=");
    put_dec(argc);
    app_putc('\n');
    if (argc > 0 && argv && argv[0]) {
        app_puts("argv0=");
        app_puts(argv[0]);
        app_putc('\n');
    }
    if (argc > 1 && argv && argv[1]) {
        app_puts("argv1=");
        app_puts(argv[1]);
        app_putc('\n');
    }
    app_puts("B1\n");

    int fd = app_open("io.txt", APP_O_WRITE | APP_O_CREAT | APP_O_TRUNC);
    app_puts("A1\n");
    app_puts("ow=");
    put_dec(fd);
    app_putc('\n');
    if (fd >= 0) {
        app_puts("B2\n");
        int wr = app_write(fd, msg, sizeof(msg) - 1);
        app_puts("A2\n");
        app_puts("wr=");
        put_dec(wr);
        app_putc('\n');
        app_puts("B3\n");
        int cr = app_close(fd);
        app_puts("A3\n");
        app_puts("cw=");
        put_dec(cr);
        app_putc('\n');
    }

    app_puts("B4\n");
    app_puts("after write\n");
    app_puts("E1\n");
    app_exit(0);
}
