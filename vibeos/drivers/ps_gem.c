#include "os.h"
#include <stdint.h>
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "lwip/dns.h"
#include "netif/ethernet.h"
#include "lwip/timeouts.h"

#define GEM0_BASE 0xE000B000UL

#define GEM_NWCTRL   0x0000
#define GEM_NWCFG    0x0004
#define GEM_NWSR     0x0008
#define GEM_DMACR    0x0010
#define GEM_TXSR     0x0014
#define GEM_RXQBASE  0x0018
#define GEM_TXQBASE  0x001c
#define GEM_RXSR     0x0020
#define GEM_ISR      0x0024
#define GEM_IDR      0x002c
#define GEM_PHYMNTNC 0x0034
#define GEM_HASHL    0x0080
#define GEM_HASHH    0x0084
#define GEM_LADDR1L  0x0088
#define GEM_LADDR1H  0x008c

#define SLCR_UNLOCK        (*(volatile uint32_t *)0xF8000008UL)
#define SLCR_LOCK          (*(volatile uint32_t *)0xF8000004UL)
#define SLCR_APER_CLK_CTRL (*(volatile uint32_t *)0xF800012CUL)
#define SLCR_GEM0_CLK_CTRL (*(volatile uint32_t *)0xF8000140UL)

#define SLCR_UNLOCK_KEY 0x0000DF0DUL
#define SLCR_LOCK_KEY   0x0000767BUL
#define GEM0_CLK_MASK   0x03F03F71UL
#define GEM0_CLK_VALUE  0x00100141UL
#define APER_CLK_MASK   0x01FFCCCDUL
#define APER_CLK_VALUE  0x01DC044DUL

#define GEM_NWCTRL_MDEN      0x00000010UL
#define GEM_NWCTRL_TXEN      0x00000008UL
#define GEM_NWCTRL_RXEN      0x00000004UL
#define GEM_NWCTRL_LOOPEN    0x00000002UL
#define GEM_NWCTRL_HALTTX    0x00000400UL
#define GEM_NWCTRL_STARTTX   0x00000200UL
#define GEM_NWCTRL_STATCLR   0x00000020UL
#define GEM_NWCTRL_FLUSHRX   0x00040000UL
#define GEM_NWCFG_MDC_MASK   0x001c0000UL
#define GEM_NWCFG_MDC_SHIFT  18
#define GEM_NWCFG_FCSIGNORE  0x04000000UL
#define GEM_NWCFG_FCSREM     0x00020000UL
#define GEM_NWCFG_COPYALL    0x00000010UL
#define GEM_NWCFG_1536RXEN   0x00000100UL
#define GEM_NWCFG_FDEN       0x00000002UL
#define GEM_NWCFG_100        0x00000001UL
#define GEM_NWCFG_RESET      0x00080000UL
#define GEM_NWSR_MDIOIDLE    0x00000004UL

#define GEM_DMACR_RXBUF_SHIFT 16
#define GEM_DMACR_INCR16      0x00000010UL
#define GEM_DMACR_RXSIZE      0x00000300UL
#define GEM_DMACR_TXSIZE      0x00000400UL

#define GEM_TXSR_ALL          0x000001ffUL
#define GEM_RXSR_ALL          0x0000000fUL

#define GEM_TXBUF_USED        0x80000000UL
#define GEM_TXBUF_WRAP        0x40000000UL
#define GEM_TXBUF_LAST        0x00008000UL
#define GEM_TXBUF_LEN_MASK    0x00003fffUL

#define GEM_RXBUF_WRAP        0x00000002UL
#define GEM_RXBUF_NEW         0x00000001UL
#define GEM_RXBUF_ADDR_MASK   0xfffffffcUL
#define GEM_RXBUF_SOF         0x00004000UL
#define GEM_RXBUF_EOF         0x00008000UL
#define GEM_RXBUF_LEN_MASK    0x00001fffUL

#define GEM_PHY_OP_MASK      0x40020000UL
#define GEM_PHY_OP_READ      0x20000000UL
#define GEM_PHY_OP_WRITE     0x10000000UL
#define GEM_PHY_ADDR_SHIFT   23
#define GEM_PHY_REG_SHIFT    18

#define Z7LITE_PHY_ADDR 1
#define Z7LITE_PHY_ADDR_FALLBACK 0
#define ETH_DBG_BASE 0x10004020UL
#define GEM_RX_COUNT 32
#define GEM_TX_COUNT 8
#define GEM_FRAME_SIZE 1536

struct gem_bd {
    volatile uint32_t addr;
    volatile uint32_t stat;
} __attribute__((aligned(8)));

static struct gem_bd gem_rx_bd[GEM_RX_COUNT] __attribute__((aligned(64)));
static struct gem_bd gem_tx_bd[GEM_TX_COUNT] __attribute__((aligned(64)));
static uint8_t gem_rx_buf[GEM_RX_COUNT][GEM_FRAME_SIZE] __attribute__((aligned(64)));
static uint8_t gem_tx_buf[GEM_TX_COUNT][GEM_FRAME_SIZE] __attribute__((aligned(64)));

static const uint8_t gem_mac[6] = {0x02, 0x56, 0x49, 0x42, 0x45, 0x01};

static volatile uint32_t ps_gem_debug[8];
static struct netif ps_gem_netif;
static int ps_gem_ready;
static int ps_gem_lwip_ready;
static int ps_gem_netif_added;
static int ps_gem_http_started;
static int ps_gem_rx_idx;
static int ps_gem_tx_idx;
static uint32_t ps_gem_rx_packets;
static uint32_t ps_gem_tx_packets;
static uint32_t ps_gem_rx_drops;
static uint32_t ps_gem_tcp_trace;

extern void start_http(void);
extern uint32_t http_server_debug_word(void);
void ps_gem_probe(void);

static inline uint32_t gem_read(uint32_t off)
{
    return *(volatile uint32_t *)(GEM0_BASE + off);
}

static inline void gem_write(uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(GEM0_BASE + off) = v;
}

static uint32_t gem_phys(const void *p)
{
    uint32_t a = (uint32_t)p;
    if (a >= 0x80000000UL && a < 0xC0000000UL) return a - 0x80000000UL + 0x01000000UL;
    return a;
}

static inline void gem_dcache_writeback(uint32_t addr)
{
    asm volatile("csrw 0x3a1, %0" :: "r"(addr) : "memory");
}

static inline void gem_dcache_invalidate(uint32_t addr)
{
    asm volatile("csrw 0x3a2, %0" :: "r"(addr) : "memory");
}

static void gem_cache_writeback_range(const void *p, int len)
{
    uint32_t a = ((uint32_t)p) & ~31u;
    uint32_t e = ((uint32_t)p) + (uint32_t)len;
    while (a < e) {
        gem_dcache_writeback(a);
        gem_dcache_writeback(gem_phys((const void *)a));
        a += 32;
    }
    asm volatile("fence rw,rw" ::: "memory");
}

static void gem_cache_invalidate_range(const void *p, int len)
{
    uint32_t a = ((uint32_t)p) & ~31u;
    uint32_t e = ((uint32_t)p) + (uint32_t)len;
    while (a < e) {
        gem_dcache_invalidate(a);
        gem_dcache_invalidate(gem_phys((const void *)a));
        a += 32;
    }
    asm volatile("fence rw,rw" ::: "memory");
}

#ifdef FPGA_MINIMAL
static void eth_dbg_write(int idx, uint32_t value)
{
    if (idx < 0 || idx > 7) return;
    ps_gem_debug[idx] = value;
}

static void eth_dbg_snapshot(uint32_t status)
{
    eth_dbg_write(0, gem_read(GEM_NWCFG));
    eth_dbg_write(1, gem_read(GEM_NWCTRL));
    eth_dbg_write(2, gem_read(GEM_NWSR));
    eth_dbg_write(6, gem_read(GEM_PHYMNTNC));
    eth_dbg_write(7, status);
}
#endif

static uint16_t gem_be16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static void ps_gem_trace_tcp(const uint8_t *frame, uint32_t len, uint32_t dir)
{
    if (!frame || len < 54) return;
    if (gem_be16(frame + 12) != 0x0800u) return;
    const uint8_t *ip = frame + 14;
    uint32_t ihl = (uint32_t)(ip[0] & 0x0fu) * 4u;
    if ((ip[0] >> 4) != 4 || ihl < 20 || len < 14u + ihl + 20u) return;
    if (ip[9] != 6) return;
    const uint8_t *tcp = ip + ihl;
    uint16_t sport = gem_be16(tcp + 0);
    uint16_t dport = gem_be16(tcp + 2);
    uint8_t flags = tcp[13];
    ps_gem_tcp_trace = 0x50000000UL |
                       ((dir & 0x0fu) << 24) |
                       (((uint32_t)sport & 0xffu) << 16) |
                       (((uint32_t)dport & 0xffu) << 8) |
                       flags;
}

void ps_gem_get_debug(uint32_t out[8])
{
    if (!out) return;
    for (int i = 0; i < 8; i++) out[i] = ps_gem_debug[i];
}

static int gem_wait_mdio_idle(void)
{
    for (int i = 0; i < 200000; i++) {
        if (gem_read(GEM_NWSR) & GEM_NWSR_MDIOIDLE) return 0;
    }
    return -1;
}

static void gem_mdio_init(void)
{
    uint32_t cfg = gem_read(GEM_NWCFG);
    cfg &= ~GEM_NWCFG_MDC_MASK;
    cfg |= (2UL << GEM_NWCFG_MDC_SHIFT); /* MDC_DIV_32, matches Xilinx default. */
    cfg |= GEM_NWCFG_FCSREM | GEM_NWCFG_COPYALL |
           GEM_NWCFG_1536RXEN | GEM_NWCFG_FDEN | GEM_NWCFG_100;
    gem_write(GEM_NWCFG, cfg);
    gem_write(GEM_NWCTRL, gem_read(GEM_NWCTRL) | GEM_NWCTRL_MDEN);
}

static void ps_gem_reset_hw(void)
{
    uint32_t ctrl = gem_read(GEM_NWCTRL);

    ctrl &= ~(GEM_NWCTRL_TXEN | GEM_NWCTRL_RXEN |
              GEM_NWCTRL_HALTTX | GEM_NWCTRL_LOOPEN);
    ctrl |= GEM_NWCTRL_STATCLR | GEM_NWCTRL_FLUSHRX;
    gem_write(GEM_NWCTRL, ctrl);
    for (volatile int i = 0; i < 1024; i++) asm volatile("");

    gem_write(GEM_IDR, 0xffffffffUL);
    gem_write(GEM_ISR, 0xffffffffUL);
    gem_write(GEM_TXSR, GEM_TXSR_ALL);
    gem_write(GEM_RXSR, GEM_RXSR_ALL);
    gem_write(GEM_TXQBASE, 0);
    gem_write(GEM_RXQBASE, 0);
    gem_write(GEM_NWCFG, GEM_NWCFG_RESET);
    gem_write(GEM_HASHL, 0);
    gem_write(GEM_HASHH, 0);
}

static void gem0_enable_ps_clock(void)
{
    SLCR_UNLOCK = SLCR_UNLOCK_KEY;
    SLCR_GEM0_CLK_CTRL = (SLCR_GEM0_CLK_CTRL & ~GEM0_CLK_MASK) | GEM0_CLK_VALUE;
    SLCR_APER_CLK_CTRL = (SLCR_APER_CLK_CTRL & ~APER_CLK_MASK) | APER_CLK_VALUE;
    SLCR_LOCK = SLCR_LOCK_KEY;
}

static int gem_regs_look_disabled(void)
{
    return gem_read(GEM_NWCFG) == 0 && gem_read(GEM_NWCTRL) == 0 &&
           gem_read(GEM_NWSR) == 0;
}

static int gem_phy_id_valid(uint32_t raw_id1, uint32_t raw_id2)
{
    uint32_t id1 = raw_id1 & 0xffffu;
    uint32_t id2 = raw_id2 & 0xffffu;
    return !((id1 == 0xffffu && id2 == 0xffffu) ||
             (id1 == 0u && id2 == 0u));
}

static int gem_phy_read(int phy, int reg, uint16_t *out)
{
    uint32_t cmd;
    uint32_t raw;

    if (gem_wait_mdio_idle() != 0) return -1;
    cmd = GEM_PHY_OP_MASK | GEM_PHY_OP_READ |
          ((uint32_t)(phy & 31) << GEM_PHY_ADDR_SHIFT) |
          ((uint32_t)(reg & 31) << GEM_PHY_REG_SHIFT);
    gem_write(GEM_PHYMNTNC, cmd);
    if (gem_wait_mdio_idle() != 0) return -2;
    raw = gem_read(GEM_PHYMNTNC);
    for (volatile int i = 0; i < 64; i++) {
        asm volatile("");
    }
    raw = gem_read(GEM_PHYMNTNC);
    *out = (uint16_t)raw;
    return 0;
}

static int gem_phy_write(int phy, int reg, uint16_t val)
{
    uint32_t cmd;

    if (gem_wait_mdio_idle() != 0) return -1;
    cmd = GEM_PHY_OP_MASK | GEM_PHY_OP_WRITE |
          ((uint32_t)(phy & 31) << GEM_PHY_ADDR_SHIFT) |
          ((uint32_t)(reg & 31) << GEM_PHY_REG_SHIFT) |
          (uint32_t)val;
    gem_write(GEM_PHYMNTNC, cmd);
    if (gem_wait_mdio_idle() != 0) return -2;
    return 0;
}

static int ps_gem_link_is_up(void)
{
    uint16_t bmsr0 = 0;
    uint16_t bmsr1 = 0;
    if (gem_phy_read(Z7LITE_PHY_ADDR, 1, &bmsr0) != 0) return 0;
    if (gem_phy_read(Z7LITE_PHY_ADDR, 1, &bmsr1) != 0) return 0;
    return (bmsr1 & 0x0004u) != 0;
}

static void rtl8201f_force_mii_mode(void)
{
    uint16_t id1 = 0, id2 = 0, rmsr_before = 0, rmsr_after = 0;

    if (gem_phy_read(Z7LITE_PHY_ADDR, 2, &id1) != 0) return;
    if (gem_phy_read(Z7LITE_PHY_ADDR, 3, &id2) != 0) return;
    if (id1 != 0x001cu || id2 != 0xc816u) return;

    /* RTL8201F page 7 reg 16 bit 3: 0=MII, 1=RMII. The board straps it
       as RMII, but Zynq PS GEM EMIO is wired here as MII/GMII. */
    if (gem_phy_write(Z7LITE_PHY_ADDR, 31, 7) != 0) return;
    if (gem_phy_read(Z7LITE_PHY_ADDR, 16, &rmsr_before) == 0) {
        gem_phy_write(Z7LITE_PHY_ADDR, 16, (uint16_t)(rmsr_before & (uint16_t)~0x0008u));
        gem_phy_read(Z7LITE_PHY_ADDR, 16, &rmsr_after);
    }
    gem_phy_write(Z7LITE_PHY_ADDR, 31, 0);
#ifdef FPGA_MINIMAL
    eth_dbg_write(3, ((uint32_t)rmsr_before << 16) | rmsr_after);
#endif
    for (volatile int i = 0; i < 10000; i++) asm volatile("");
}

static void ps_gem_set_mac(void)
{
    uint32_t lo = (uint32_t)gem_mac[0] |
                  ((uint32_t)gem_mac[1] << 8) |
                  ((uint32_t)gem_mac[2] << 16) |
                  ((uint32_t)gem_mac[3] << 24);
    uint32_t hi = (uint32_t)gem_mac[4] |
                  ((uint32_t)gem_mac[5] << 8);
    gem_write(GEM_LADDR1L, lo);
    gem_write(GEM_LADDR1H, hi);
}

static void ps_gem_init_rings(void)
{
    memset(gem_rx_bd, 0, sizeof(gem_rx_bd));
    memset(gem_tx_bd, 0, sizeof(gem_tx_bd));
    memset(gem_rx_buf, 0, sizeof(gem_rx_buf));
    memset(gem_tx_buf, 0, sizeof(gem_tx_buf));

    for (int i = 0; i < GEM_RX_COUNT; i++) {
        uint32_t addr = gem_phys(gem_rx_buf[i]) & GEM_RXBUF_ADDR_MASK;
        if (i == GEM_RX_COUNT - 1) addr |= GEM_RXBUF_WRAP;
        gem_rx_bd[i].addr = addr;
        gem_rx_bd[i].stat = 0;
    }
    for (int i = 0; i < GEM_TX_COUNT; i++) {
        gem_tx_bd[i].addr = 0;
        gem_tx_bd[i].stat = GEM_TXBUF_USED | ((i == GEM_TX_COUNT - 1) ? GEM_TXBUF_WRAP : 0);
    }

    gem_cache_writeback_range(gem_rx_buf, sizeof(gem_rx_buf));
    gem_cache_writeback_range(gem_tx_buf, sizeof(gem_tx_buf));
    gem_cache_writeback_range(gem_rx_bd, sizeof(gem_rx_bd));
    gem_cache_writeback_range(gem_tx_bd, sizeof(gem_tx_bd));

    ps_gem_rx_idx = 0;
    ps_gem_tx_idx = 0;
}

static err_t ps_gem_linkoutput(struct netif *netif, struct pbuf *p)
{
    (void)netif;
    if (!ps_gem_ready || p->tot_len > GEM_FRAME_SIZE) return ERR_IF;

    gem_cache_invalidate_range(&gem_tx_bd[ps_gem_tx_idx], sizeof(gem_tx_bd[0]));
    if ((gem_tx_bd[ps_gem_tx_idx].stat & GEM_TXBUF_USED) == 0) {
        return ERR_MEM;
    }

    uint32_t len = pbuf_copy_partial(p, gem_tx_buf[ps_gem_tx_idx], p->tot_len, 0);
    if (len != p->tot_len) return ERR_IF;
    ps_gem_trace_tcp(gem_tx_buf[ps_gem_tx_idx], len, 2);

    gem_cache_writeback_range(gem_tx_buf[ps_gem_tx_idx], (int)len);
    gem_tx_bd[ps_gem_tx_idx].addr = gem_phys(gem_tx_buf[ps_gem_tx_idx]);
    gem_tx_bd[ps_gem_tx_idx].stat = (len & GEM_TXBUF_LEN_MASK) | GEM_TXBUF_LAST |
                                    ((ps_gem_tx_idx == GEM_TX_COUNT - 1) ? GEM_TXBUF_WRAP : 0);
    gem_cache_writeback_range(&gem_tx_bd[ps_gem_tx_idx], sizeof(gem_tx_bd[0]));

    gem_write(GEM_TXSR, GEM_TXSR_ALL);
    gem_write(GEM_NWCTRL, gem_read(GEM_NWCTRL) | GEM_NWCTRL_STARTTX);
    ps_gem_tx_idx = (ps_gem_tx_idx + 1) % GEM_TX_COUNT;
    ps_gem_tx_packets++;
    return ERR_OK;
}

static err_t ps_gem_netif_init_cb(struct netif *netif)
{
    netif->name[0] = 'e';
    netif->name[1] = '0';
    netif->hwaddr_len = 6;
    for (int i = 0; i < 6; i++) netif->hwaddr[i] = gem_mac[i];
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    netif->output = etharp_output;
    netif->linkoutput = ps_gem_linkoutput;
    return ERR_OK;
}

static int ps_gem_rx_one(void)
{
    gem_cache_invalidate_range(&gem_rx_bd[ps_gem_rx_idx], sizeof(gem_rx_bd[0]));
    uint32_t addr = gem_rx_bd[ps_gem_rx_idx].addr;
    if ((addr & GEM_RXBUF_NEW) == 0) return 0;

    uint32_t stat = gem_rx_bd[ps_gem_rx_idx].stat;
    uint32_t len = stat & GEM_RXBUF_LEN_MASK;
    if ((stat & (GEM_RXBUF_SOF | GEM_RXBUF_EOF)) == (GEM_RXBUF_SOF | GEM_RXBUF_EOF) &&
        len >= 14 && len <= GEM_FRAME_SIZE) {
        gem_cache_invalidate_range(gem_rx_buf[ps_gem_rx_idx], (int)len);
        ps_gem_trace_tcp(gem_rx_buf[ps_gem_rx_idx], len, 1);
        struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
        if (p) {
            pbuf_take(p, gem_rx_buf[ps_gem_rx_idx], (u16_t)len);
            if (ps_gem_netif.input(p, &ps_gem_netif) != ERR_OK) {
                pbuf_free(p);
                ps_gem_rx_drops++;
            } else {
                ps_gem_rx_packets++;
            }
        } else {
            ps_gem_rx_drops++;
        }
    } else {
        ps_gem_rx_drops++;
    }

    uint32_t new_addr = gem_phys(gem_rx_buf[ps_gem_rx_idx]) & GEM_RXBUF_ADDR_MASK;
    if (ps_gem_rx_idx == GEM_RX_COUNT - 1) new_addr |= GEM_RXBUF_WRAP;
    gem_rx_bd[ps_gem_rx_idx].addr = new_addr;
    gem_rx_bd[ps_gem_rx_idx].stat = 0;
    gem_cache_writeback_range(&gem_rx_bd[ps_gem_rx_idx], sizeof(gem_rx_bd[0]));
    ps_gem_rx_idx = (ps_gem_rx_idx + 1) % GEM_RX_COUNT;
    gem_write(GEM_RXSR, GEM_RXSR_ALL);
    return 1;
}

void ps_gem_poll(void)
{
#ifdef FPGA_MINIMAL
    if (!ps_gem_ready) return;
    int budget = 8;
    while (budget-- > 0 && ps_gem_rx_one()) {
    }
    netif_set_link_up(&ps_gem_netif);
    eth_dbg_write(0, gem_read(GEM_NWCFG));
    eth_dbg_write(1, gem_read(GEM_NWCTRL));
    eth_dbg_write(2, gem_read(GEM_NWSR));
    eth_dbg_write(4, ps_gem_tcp_trace ? ps_gem_tcp_trace : http_server_debug_word());
    eth_dbg_write(5, ps_gem_tx_packets);
    eth_dbg_write(6, ps_gem_rx_packets);
#endif
}

int ps_gem_has_pending_irq(void)
{
    return 0;
}

int ps_gem_has_rx_ready(void)
{
#ifdef FPGA_MINIMAL
    if (!ps_gem_ready) return 0;
    gem_cache_invalidate_range(&gem_rx_bd[ps_gem_rx_idx], sizeof(gem_rx_bd[0]));
    return (gem_rx_bd[ps_gem_rx_idx].addr & GEM_RXBUF_NEW) != 0;
#else
    return 0;
#endif
}

int ps_gem_rx_pending_count(void)
{
#ifdef FPGA_MINIMAL
    if (!ps_gem_ready) return 0;
    int count = 0;
    int idx = ps_gem_rx_idx;
    while (count < GEM_RX_COUNT) {
        gem_cache_invalidate_range(&gem_rx_bd[idx], sizeof(gem_rx_bd[0]));
        if ((gem_rx_bd[idx].addr & GEM_RXBUF_NEW) == 0) break;
        count++;
        idx = (idx + 1) % GEM_RX_COUNT;
    }
    return count;
#else
    return 0;
#endif
}

int ps_gem_is_ready(void)
{
    return ps_gem_ready;
}

void ps_gem_init(void)
{
#ifdef FPGA_MINIMAL
    ip4_addr_t ip;
    ip4_addr_t nm;
    ip4_addr_t gw;

    if (ps_gem_ready) return;
    lib_puts("[VIBE] eth0: starting GEM DMA driver\n");
    gem0_enable_ps_clock();
    ps_gem_reset_hw();
    gem_mdio_init();
    rtl8201f_force_mii_mode();
    eth_dbg_write(0, gem_read(GEM_NWCFG));
    eth_dbg_write(1, gem_read(GEM_NWCTRL));
    eth_dbg_write(2, gem_read(GEM_NWSR));
    eth_dbg_write(7, 0x45544820UL);
    if (gem_regs_look_disabled()) {
        eth_dbg_write(7, 0x455448e0UL);
        lib_puts("[VIBE] eth0: GEM0 registers are disabled\n");
        return;
    }

    if (!ps_gem_lwip_ready) {
        lwip_init();
        ps_gem_lwip_ready = 1;
    }

    gem_write(GEM_NWCTRL, gem_read(GEM_NWCTRL) & ~(GEM_NWCTRL_TXEN | GEM_NWCTRL_RXEN));
    gem_write(GEM_IDR, 0xffffffffUL);
    gem_write(GEM_ISR, 0xffffffffUL);
    gem_write(GEM_TXSR, GEM_TXSR_ALL);
    gem_write(GEM_RXSR, GEM_RXSR_ALL);
    ps_gem_set_mac();
    ps_gem_init_rings();
    gem_write(GEM_RXQBASE, gem_phys(gem_rx_bd));
    gem_write(GEM_TXQBASE, gem_phys(gem_tx_bd));
    gem_write(GEM_DMACR, (24UL << GEM_DMACR_RXBUF_SHIFT) |
                         GEM_DMACR_RXSIZE | GEM_DMACR_TXSIZE |
                         GEM_DMACR_INCR16);
    gem_write(GEM_NWCTRL, gem_read(GEM_NWCTRL) | GEM_NWCTRL_MDEN | GEM_NWCTRL_TXEN | GEM_NWCTRL_RXEN);

    IP4_ADDR(&ip, 192, 168, 0, 154);
    IP4_ADDR(&nm, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 168, 0, 1);
#if LWIP_DNS
    {
        ip_addr_t dns;
        IP_ADDR4(&dns, 192, 168, 0, 1);
        dns_setserver(0, &dns);
        IP_ADDR4(&dns, 8, 8, 8, 8);
        dns_setserver(1, &dns);
    }
#endif
    if (!ps_gem_netif_added) {
        netif_add(&ps_gem_netif, &ip, &nm, &gw, NULL, ps_gem_netif_init_cb, ethernet_input);
        netif_set_default(&ps_gem_netif);
        ps_gem_netif_added = 1;
    } else {
        netif_set_addr(&ps_gem_netif, &ip, &nm, &gw);
    }
    netif_set_up(&ps_gem_netif);
    netif_set_link_up(&ps_gem_netif);

    if (!ps_gem_http_started) {
        start_http();
        ps_gem_http_started = 1;
    }

    ps_gem_ready = 1;
    eth_dbg_write(7, 0x45544891UL);
    lib_puts("[VIBE] eth0: GEM DMA driver ready ip=192.168.0.154\n");
#else
    ps_gem_probe();
#endif
}

void ps_gem_probe(void)
{
#ifdef FPGA_MINIMAL
    lib_puts("[VIBE] eth0: GEM register snapshot\n");
    gem0_enable_ps_clock();
    gem_mdio_init();
    eth_dbg_write(0, gem_read(GEM_NWCFG));
    eth_dbg_write(1, gem_read(GEM_NWCTRL));
    eth_dbg_write(2, gem_read(GEM_NWSR));
    eth_dbg_write(3, 0);
    eth_dbg_write(4, 0);
    eth_dbg_write(5, ps_gem_tx_packets);
    eth_dbg_write(6, ps_gem_rx_packets);
    eth_dbg_write(7, ps_gem_ready ? 0x45544891UL : 0x45544820UL);
#else
    (void)gem_phy_read;
    (void)gem_phy_write;
#endif
}
