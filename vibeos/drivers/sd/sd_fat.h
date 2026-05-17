#ifndef SD_FAT_H
#define SD_FAT_H

#include <stdint.h>

#define SD_FAT_TYPE_NONE 0
#define SD_FAT_TYPE_FAT16 16
#define SD_FAT_TYPE_FAT32 32

typedef struct sd_fat_fs {
    uint32_t part_lba;
    uint32_t fat_lba;
    uint32_t root_lba;
    uint32_t root_cluster;
    uint32_t root_sectors;
    uint32_t first_data_lba;
    uint32_t fat_sectors;
    uint16_t root_entries;
    uint8_t sectors_per_cluster;
    uint8_t type;
} sd_fat_fs_t;

int sd_fat_mount(sd_fat_fs_t *fs);
int sd_fat_status(char *out, int out_max);
int sd_fat_list_root(char *out, int out_max, int all);

#endif
