#include "fat16_api.h"
#include "sdio_api.h"
#include "console_api.h"

static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | ((uint16_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void put16le(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32le(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void memzero(uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) p[i] = 0;
}

static void memcpy8(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

static void print_83_name(const uint8_t *e)
{
    for (int i = 0; i < 8; i++) {
        if (e[i] != ' ') console_putc((char)e[i]);
    }
    if (e[8] != ' ') {
        console_putc('.');
        for (int i = 8; i < 11; i++) {
            if (e[i] != ' ') console_putc((char)e[i]);
        }
    }
}

int fat16_mount(fat16_fs_t *fs)
{
    uint32_t sector[128];
    if (sdio_read_sector(0, sector) != 0) return -1;

    uint8_t *mbr = (uint8_t *)sector;
    uint32_t part_lba = le32(mbr + 446 + 8);
    uint8_t part_type = mbr[446 + 4];
    if (part_lba == 0 && part_type == 0) part_lba = 0;

    if (sdio_read_sector(part_lba, sector) != 0) return -1;
    uint8_t *bs = (uint8_t *)sector;
    uint16_t bps = le16(bs + 11);
    uint8_t spc = bs[13];
    uint16_t reserved = le16(bs + 14);
    uint8_t fats = bs[16];
    uint16_t root_entries = le16(bs + 17);
    uint16_t fat_sz = le16(bs + 22);

    if (bps != 512 || spc == 0 || fats < 2 || fat_sz == 0 || root_entries == 0) return -1;

    fs->part_lba = part_lba;
    fs->fat0_lba = part_lba + reserved;
    fs->fat1_lba = fs->fat0_lba + fat_sz;
    fs->root_lba = part_lba + reserved + (uint32_t)fats * fat_sz;
    fs->root_sectors = ((uint32_t)root_entries * 32u + 511u) / 512u;
    fs->first_data_lba = fs->root_lba + fs->root_sectors;
    fs->root_entries = root_entries;
    fs->fat_sectors = fat_sz;
    fs->sectors_per_cluster = spc;
    return 0;
}

int fat16_list_root(const fat16_fs_t *fs)
{
    uint32_t sector[128];
    console_puts("ROOT DIR\n");
    for (uint32_t rs = 0; rs < fs->root_sectors; rs++) {
        if (sdio_read_sector(fs->root_lba + rs, sector) != 0) return -1;
        uint8_t *dir = (uint8_t *)sector;
        for (int off = 0; off < 512; off += 32) {
            uint8_t first = dir[off];
            uint8_t attr = dir[off + 11];
            if (first == 0x00) return 0;
            if (first == 0xE5 || attr == 0x0F) continue;
            print_83_name(dir + off);
            console_puts(" attr=");
            console_puthex(attr);
            console_puts(" cluster=");
            console_puthex(le16(dir + off + 26));
            console_puts(" size=");
            console_puthex(le32(dir + off + 28));
            console_putc('\n');
        }
    }
    return 0;
}

int fat16_create_file_83(const fat16_fs_t *fs, const char name11[11], const void *data, uint32_t size)
{
    uint32_t sector[128];
    uint32_t data_sector[128];
    if (size > 512) return -1;

    uint16_t free_cluster = 0;
    uint32_t free_fat_sector = 0;
    uint32_t free_fat_offset = 0;
    for (uint32_t fat_sector = 0; fat_sector < fs->fat_sectors && free_cluster == 0; fat_sector++) {
        if (sdio_read_sector(fs->fat0_lba + fat_sector, sector) != 0) return -1;
        uint8_t *fat = (uint8_t *)sector;
        for (uint32_t off = 0; off < 512; off += 2) {
            uint32_t cluster = fat_sector * 256u + off / 2u;
            if (cluster < 2) continue;
            if (le16(fat + off) == 0) {
                free_cluster = (uint16_t)cluster;
                free_fat_sector = fat_sector;
                free_fat_offset = off;
                break;
            }
        }
    }
    if (free_cluster == 0) return -1;

    int entry_sector = -1;
    int entry_offset = -1;
    for (uint32_t rs = 0; rs < fs->root_sectors && entry_sector < 0; rs++) {
        if (sdio_read_sector(fs->root_lba + rs, sector) != 0) return -1;
        uint8_t *root = (uint8_t *)sector;
        for (int off = 0; off < 512; off += 32) {
            if (root[off] == 0x00 || root[off] == 0xE5) {
                entry_sector = (int)rs;
                entry_offset = off;
                break;
            }
        }
    }
    if (entry_sector < 0) return -1;

    memzero((uint8_t *)data_sector, 512);
    memcpy8((uint8_t *)data_sector, (const uint8_t *)data, size);
    uint32_t data_lba = fs->first_data_lba + ((uint32_t)free_cluster - 2u) * fs->sectors_per_cluster;
    if (sdio_write_sector(data_lba, data_sector) != 0) return -1;

    if (sdio_read_sector(fs->fat0_lba + free_fat_sector, sector) != 0) return -1;
    put16le((uint8_t *)sector + free_fat_offset, 0xFFFFu);
    if (sdio_write_sector(fs->fat0_lba + free_fat_sector, sector) != 0) return -1;
    if (sdio_write_sector(fs->fat1_lba + free_fat_sector, sector) != 0) return -1;

    if (sdio_read_sector(fs->root_lba + (uint32_t)entry_sector, sector) != 0) return -1;
    uint8_t *entry = (uint8_t *)sector + entry_offset;
    memzero(entry, 32);
    memcpy8(entry, (const uint8_t *)name11, 11);
    entry[11] = 0x20;
    put16le(entry + 26, free_cluster);
    put32le(entry + 28, size);
    if (sdio_write_sector(fs->root_lba + (uint32_t)entry_sector, sector) != 0) return -1;
    return 0;
}
