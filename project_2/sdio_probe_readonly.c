#include "console_api.h"
#include "sdio_api.h"
#include "fat16_api.h"

static void dump_words(const char *label, const uint32_t *words, int count)
{
    console_puts(label);
    console_putc('\n');
    for (int i = 0; i < count; i++) {
        console_puthex(words[i]);
        console_putc((i == count - 1) ? '\n' : ' ');
    }
}

int os_main(void)
{
    fat16_fs_t fs;
    uint32_t sector[128];

    console_puts("SDIO read-only probe\n");
    if (sdio_init() != 0) {
        console_puts("sdio_init failed\n");
        while (1) {}
    }
    console_puts("sdio_init ok\n");
    console_puts("capacity=");
    console_puts(sdio_is_high_capacity() ? "SDHC/SDXC\n" : "SDSC\n");

    if (sdio_read_sector(0, sector) != 0) {
        console_puts("read sector0 failed\n");
        while (1) {}
    }
    console_puts("read sector0 ok\n");
    dump_words("sector0 first 8 words:", sector, 8);
    console_puts("sector0 sig=");
    console_puthex(((uint8_t *)sector)[510] | ((uint32_t)((uint8_t *)sector)[511] << 8));
    console_putc('\n');

    if (fat16_mount(&fs) != 0) {
        console_puts("fat16_mount failed\n");
        console_puts("This card may be FAT32/exFAT, or the SDIO read is not stable yet.\n");
        while (1) {}
    }
    console_puts("fat16_mount ok\n");
    fat16_list_root(&fs);
    console_puts("SDIO read-only probe done\n");
    while (1) {}
    return 0;
}
