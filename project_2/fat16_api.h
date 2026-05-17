#ifndef FAT16_API_H
#define FAT16_API_H

#include <stdint.h>

typedef struct fat16_fs {
    uint32_t part_lba;
    uint32_t fat0_lba;
    uint32_t fat1_lba;
    uint32_t root_lba;
    uint32_t root_sectors;
    uint32_t first_data_lba;
    uint16_t root_entries;
    uint16_t fat_sectors;
    uint8_t sectors_per_cluster;
} fat16_fs_t;

int fat16_mount(fat16_fs_t *fs);
int fat16_list_root(const fat16_fs_t *fs);
int fat16_create_file_83(const fat16_fs_t *fs, const char name11[11], const void *data, uint32_t size);

#endif
