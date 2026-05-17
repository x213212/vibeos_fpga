#include "sd_fat.h"
#include "sdio_api.h"
#include "stdio.h"
#include "string.h"

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void append_str(char *out, int out_max, const char *s)
{
    int len;
    if (!out || out_max <= 0 || !s) return;
    len = (int)strlen(out);
    while (*s && len < out_max - 1) out[len++] = *s++;
    out[len] = '\0';
}

static void format_83_name(const uint8_t *e, char *name, int name_max)
{
    int n = 0;
    int last = 7;
    if (!name || name_max <= 0) return;

    while (last >= 0 && e[last] == ' ') last--;
    for (int i = 0; i <= last && n < name_max - 1; i++) name[n++] = (char)e[i];

    last = 10;
    while (last >= 8 && e[last] == ' ') last--;
    if (last >= 8 && n < name_max - 1) name[n++] = '.';
    for (int i = 8; i <= last && n < name_max - 1; i++) name[n++] = (char)e[i];
    name[n] = '\0';
}

static int valid_boot_sector(const uint8_t *bs)
{
    if (bs[510] != 0x55 || bs[511] != 0xAA) return 0;
    if (le16(bs + 11) != 512) return 0;
    if (bs[13] == 0) return 0;
    if (bs[16] == 0) return 0;
    return 1;
}

static uint32_t cluster_lba(const sd_fat_fs_t *fs, uint32_t cluster)
{
    return fs->first_data_lba + (cluster - 2u) * fs->sectors_per_cluster;
}

static int mount_from_boot_sector(sd_fat_fs_t *fs, uint32_t part_lba, const uint8_t *bs)
{
    uint16_t reserved = le16(bs + 14);
    uint8_t fats = bs[16];
    uint16_t root_entries = le16(bs + 17);
    uint16_t total16 = le16(bs + 19);
    uint32_t total32 = le32(bs + 32);
    uint16_t fat16 = le16(bs + 22);
    uint32_t fat32 = le32(bs + 36);
    uint32_t total = total16 ? total16 : total32;
    uint32_t fat_sectors = fat16 ? fat16 : fat32;
    uint32_t root_sectors = ((uint32_t)root_entries * 32u + 511u) / 512u;
    uint32_t data_sectors;
    uint32_t clusters;

    if (!valid_boot_sector(bs)) return -1;
    if (total == 0 || fat_sectors == 0) return -1;
    data_sectors = total - reserved - ((uint32_t)fats * fat_sectors) - root_sectors;
    clusters = data_sectors / bs[13];

    memset(fs, 0, sizeof(*fs));
    fs->part_lba = part_lba;
    fs->fat_lba = part_lba + reserved;
    fs->fat_sectors = fat_sectors;
    fs->root_entries = root_entries;
    fs->root_sectors = root_sectors;
    fs->sectors_per_cluster = bs[13];
    fs->first_data_lba = part_lba + reserved + (uint32_t)fats * fat_sectors + root_sectors;

    if (clusters < 65525u && root_entries != 0 && fat16 != 0) {
        fs->type = SD_FAT_TYPE_FAT16;
        fs->root_lba = part_lba + reserved + (uint32_t)fats * fat_sectors;
        return 0;
    }

    if (root_entries == 0 && fat32 != 0) {
        fs->type = SD_FAT_TYPE_FAT32;
        fs->root_cluster = le32(bs + 44);
        if (fs->root_cluster < 2) fs->root_cluster = 2;
        fs->root_lba = cluster_lba(fs, fs->root_cluster);
        return 0;
    }

    return -1;
}

int sd_fat_mount(sd_fat_fs_t *fs)
{
    uint32_t sector[128];
    uint8_t *s = (uint8_t *)sector;
    uint32_t part_lba;

    if (!fs) return -1;
    if (sdio_init() != 0) return -1;
    if (sdio_read_sector(0, sector) != 0) return -1;

    if (mount_from_boot_sector(fs, 0, s) == 0) return 0;

    part_lba = le32(s + 446 + 8);
    if (part_lba == 0) return -1;
    if (sdio_read_sector(part_lba, sector) != 0) return -1;
    return mount_from_boot_sector(fs, part_lba, (uint8_t *)sector);
}

static int fat32_next_cluster(const sd_fat_fs_t *fs, uint32_t cluster, uint32_t *next)
{
    uint32_t sector[128];
    uint32_t fat_offset = cluster * 4u;
    uint32_t lba = fs->fat_lba + fat_offset / 512u;
    uint32_t off = fat_offset & 511u;
    if (sdio_read_sector(lba, sector) != 0) return -1;
    *next = le32(((uint8_t *)sector) + off) & 0x0FFFFFFFu;
    return 0;
}

static void append_dir_entry(const uint8_t *entry, char *out, int out_max, int all, int *printed)
{
    char name[16];
    char row[96];
    uint8_t attr = entry[11];
    uint32_t size = le32(entry + 28);

    if (entry[0] == 0x00 || entry[0] == 0xE5) return;
    if (attr == 0x0F || (attr & 0x08)) return;

    format_83_name(entry, name, sizeof(name));
    if (name[0] == '\0') return;

    if (all) {
        snprintf(row, sizeof(row), "%c%c%c%c %10u %s\n",
                 (attr & 0x10) ? 'd' : '-',
                 (attr & 0x01) ? 'r' : 'w',
                 (attr & 0x02) ? 'h' : '-',
                 (attr & 0x04) ? 's' : '-',
                 size, name);
    } else {
        snprintf(row, sizeof(row), "%c %s\n", (attr & 0x10) ? 'd' : 'f', name);
    }
    append_str(out, out_max, row);
    if (printed) *printed = 1;
}

static int list_dir_sector(uint32_t lba, char *out, int out_max, int all, int *printed, int *done)
{
    uint32_t sector[128];
    uint8_t *dir;

    if (sdio_read_sector(lba, sector) != 0) return -1;
    dir = (uint8_t *)sector;
    for (int off = 0; off < 512; off += 32) {
        if (dir[off] == 0x00) {
            *done = 1;
            return 0;
        }
        append_dir_entry(dir + off, out, out_max, all, printed);
    }
    return 0;
}

int sd_fat_list_root(char *out, int out_max, int all)
{
    sd_fat_fs_t fs;
    int printed = 0;
    int done = 0;

    if (!out || out_max <= 0) return -1;
    out[0] = '\0';
    if (sd_fat_mount(&fs) != 0) {
        append_str(out, out_max, "ERR: SD mount failed. Use FAT16/FAT32 card on PS SD0.\n");
        return -1;
    }

    if (fs.type == SD_FAT_TYPE_FAT16) {
        for (uint32_t i = 0; i < fs.root_sectors && !done; i++) {
            if (list_dir_sector(fs.root_lba + i, out, out_max, all, &printed, &done) != 0) return -1;
        }
    } else if (fs.type == SD_FAT_TYPE_FAT32) {
        uint32_t cluster = fs.root_cluster;
        for (int guard = 0; guard < 128 && cluster >= 2 && cluster < 0x0FFFFFF8u && !done; guard++) {
            for (uint32_t i = 0; i < fs.sectors_per_cluster && !done; i++) {
                if (list_dir_sector(cluster_lba(&fs, cluster) + i, out, out_max, all, &printed, &done) != 0) return -1;
            }
            if (!done && fat32_next_cluster(&fs, cluster, &cluster) != 0) return -1;
        }
    } else {
        append_str(out, out_max, "ERR: unsupported SD filesystem.\n");
        return -1;
    }

    if (!printed) append_str(out, out_max, "(empty)\n");
    return 0;
}

int sd_fat_status(char *out, int out_max)
{
    sd_fat_fs_t fs;
    if (!out || out_max <= 0) return -1;
    out[0] = '\0';
    if (sd_fat_mount(&fs) != 0) {
        append_str(out, out_max, "sd: mount failed\n");
        return -1;
    }
    snprintf(out, out_max,
             "sd: init ok, %s, %s, part_lba=%u fat_lba=%u root_lba=%u spc=%u\n",
             sdio_is_high_capacity() ? "SDHC/SDXC" : "SDSC",
             fs.type == SD_FAT_TYPE_FAT32 ? "FAT32" : "FAT16",
             fs.part_lba, fs.fat_lba, fs.root_lba, fs.sectors_per_cluster);
    return 0;
}
