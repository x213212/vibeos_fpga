#ifndef SDIO_API_H
#define SDIO_API_H

#include <stdint.h>

#define SDIO_SECTOR_SIZE 512u

int sdio_init(void);
void sdio_reset_state(void);
int sdio_read_sector(uint32_t lba, void *buf512);
int sdio_write_sector(uint32_t lba, const void *buf512);
int sdio_is_high_capacity(void);

#endif
