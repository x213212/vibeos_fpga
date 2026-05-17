#include "sdio_api.h"

#define SD0 0xE0100000u
#define SDMA_ADDR   (SD0 + 0x00u)
#define BLK_SIZECNT (SD0 + 0x04u)
#define ARGUMENT    (SD0 + 0x08u)
#define XFER_CMD    (SD0 + 0x0Cu)
#define RESP0       (SD0 + 0x10u)
#define RESP1       (SD0 + 0x14u)
#define RESP2       (SD0 + 0x18u)
#define RESP3       (SD0 + 0x1Cu)
#define BUF_DATA    (SD0 + 0x20u)
#define PRESENT     (SD0 + 0x24u)
#define HOST_PWR    (SD0 + 0x28u)
#define CLK_RESET   (SD0 + 0x2Cu)
#define INT_STAT    (SD0 + 0x30u)
#define INT_EN      (SD0 + 0x34u)
#define SIG_EN      (SD0 + 0x38u)

#define INT_CMD_COMPLETE  0x00000001u
#define INT_XFER_COMPLETE 0x00000002u
#define INT_BUF_WRITE_READY 0x00000010u
#define INT_BUF_READ_READY  0x00000020u
#define INT_ALL          0x117f01ffu
#define INT_ERR_MASK     0xffff0000u

static int high_capacity;
static int initialized;

static uint32_t read32(uint32_t addr) { return *(volatile uint32_t *)addr; }
static void write32(uint32_t addr, uint32_t value) { *(volatile uint32_t *)addr = value; }
static uint16_t read16(uint32_t addr) { return *(volatile uint16_t *)addr; }
static void write16(uint32_t addr, uint16_t value) { *(volatile uint16_t *)addr = value; }

void sdio_reset_state(void)
{
    initialized = 0;
    high_capacity = 0;
}

static int wait_clear(uint32_t addr, uint32_t mask, uint32_t limit)
{
    while (limit--) {
        if ((read32(addr) & mask) == 0) return 0;
    }
    return -1;
}

static int wait_set(uint32_t addr, uint32_t mask, uint32_t limit)
{
    while (limit--) {
        if ((read32(addr) & mask) == mask) return 0;
    }
    return -1;
}

static int wait_int(uint32_t mask)
{
    for (uint32_t i = 0; i < 5000000u; i++) {
        uint32_t s = ((uint32_t)read16(INT_STAT + 2u) << 16) | read16(INT_STAT);
        if (s & INT_ERR_MASK) {
            write16(INT_STAT, (uint16_t)s);
            write16(INT_STAT + 2u, (uint16_t)(s >> 16));
            return -1;
        }
        if ((s & mask) == mask) {
            write16(INT_STAT, (uint16_t)mask);
            return 0;
        }
    }
    return -1;
}

static int cmd(uint32_t idx, uint32_t arg, uint32_t flags, int data_present)
{
    if (data_present) flags |= 0x20u;
    if (wait_clear(PRESENT, data_present ? 0x3u : 0x1u, 5000000u) != 0) return -1;
    write32(INT_STAT, INT_ALL);
    write32(ARGUMENT, arg);
    write32(XFER_CMD, (idx << 24) | (flags << 16));
    return wait_int(INT_CMD_COMPLETE);
}

static int app_cmd(uint32_t rca, uint32_t idx, uint32_t arg)
{
    if (cmd(55, rca << 16, 0x1Au, 0) != 0) return -1;
    return cmd(idx, arg, 0x02u, 0);
}

int sdio_init(void)
{
    uint32_t ocr = 0;
    uint32_t rca;

    if (initialized) return 0;

    write32(INT_STAT, INT_ALL);
    write32(CLK_RESET, 0x01000000u);
    if (wait_clear(CLK_RESET, 0x01000000u, 1000000u) != 0) return -1;

    write32(HOST_PWR, 0x00000F00u);
    write32(CLK_RESET, 0x00008001u);
    if (wait_set(CLK_RESET, 0x00000002u, 1000000u) != 0) return -1;
    write32(CLK_RESET, read32(CLK_RESET) | 0x00000004u | 0x000E0000u);
    write16(INT_EN, 0x01ffu);
    write16(INT_EN + 2u, 0x117fu);
    write32(SIG_EN, 0);
    write32(INT_STAT, INT_ALL);

    (void)cmd(0, 0, 0, 0);
    (void)cmd(8, 0x000001AAu, 0x1Au, 0);

    for (int i = 0; i < 200; i++) {
        if (app_cmd(0, 41, 0x40FF8000u) != 0) return -1;
        ocr = read32(RESP0);
        if (ocr & 0x80000000u) break;
    }
    if ((ocr & 0x80000000u) == 0) return -1;
    high_capacity = ((ocr & 0x40000000u) != 0);

    if (cmd(2, 0, 0x09u, 0) != 0) return -1;
    (void)read32(RESP0);
    (void)read32(RESP1);
    (void)read32(RESP2);
    (void)read32(RESP3);

    if (cmd(3, 0, 0x1Au, 0) != 0) return -1;
    rca = read32(RESP0) >> 16;
    if (cmd(7, rca << 16, 0x1Bu, 0) != 0) return -1;
    if (cmd(16, SDIO_SECTOR_SIZE, 0x1Au, 0) != 0) return -1;

    initialized = 1;
    return 0;
}

int sdio_read_sector(uint32_t lba, void *buf512)
{
    uint32_t *buf = (uint32_t *)buf512;
    if (sdio_init() != 0) return -1;

    write32(BLK_SIZECNT, (1u << 16) | SDIO_SECTOR_SIZE);
    write32(SDMA_ADDR, 0);
    write32(INT_STAT, INT_ALL);
    write32(ARGUMENT, high_capacity ? lba : (lba << 9));
    write32(XFER_CMD, (17u << 24) | ((0x2u | 0x18u | 0x20u) << 16) | 0x10u);
    if (wait_int(INT_CMD_COMPLETE) != 0) return -1;
    if (wait_int(INT_BUF_READ_READY) != 0) return -1;
    for (int i = 0; i < 128; i++) buf[i] = read32(BUF_DATA);
    if (wait_int(INT_XFER_COMPLETE) != 0) return -1;
    return 0;
}

int sdio_write_sector(uint32_t lba, const void *buf512)
{
    const uint32_t *buf = (const uint32_t *)buf512;
    if (sdio_init() != 0) return -1;

    write32(BLK_SIZECNT, (1u << 16) | SDIO_SECTOR_SIZE);
    write32(SDMA_ADDR, 0);
    write32(INT_STAT, INT_ALL);
    write32(ARGUMENT, high_capacity ? lba : (lba << 9));
    write32(XFER_CMD, (24u << 24) | ((0x1Au | 0x20u) << 16));
    if (wait_int(INT_CMD_COMPLETE) != 0) return -1;
    if (wait_int(INT_BUF_WRITE_READY) != 0) return -1;
    for (int i = 0; i < 128; i++) write32(BUF_DATA, buf[i]);
    if (wait_int(INT_XFER_COMPLETE) != 0) return -1;
    return 0;
}

int sdio_is_high_capacity(void)
{
    return high_capacity;
}
