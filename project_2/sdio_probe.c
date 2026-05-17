#include "console_api.h"
#include "sdio_api.h"
#include "fat16_api.h"

int os_main(void)
{
    fat16_fs_t fs;
    const char text[] = "hello from riscv sd card\r\n";
    const char name[11] = {'R','I','S','C','A','P','I',' ','T','X','T'};

    console_puts("SDIO API demo\n");
    if (sdio_init() != 0) {
        console_puts("sdio_init failed\n");
        while (1) {}
    }
    console_puts("sdio_init ok\n");

    if (fat16_mount(&fs) != 0) {
        console_puts("fat16_mount failed\n");
        while (1) {}
    }
    console_puts("fat16_mount ok\n");
    fat16_list_root(&fs);

    if (fat16_create_file_83(&fs, name, text, sizeof(text) - 1) == 0)
        console_puts("fat16_create_file_83 RISCAPI.TXT ok\n");
    else
        console_puts("fat16_create_file_83 RISCAPI.TXT failed\n");

    fat16_list_root(&fs);
    console_puts("SDIO API demo done\n");
    while (1) {}
    return 0;
}
