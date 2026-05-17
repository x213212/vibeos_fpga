#include "virtio.h"
#include "os.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include "lwip/tcp.h"

#define R_NET(offset) ((volatile uint32_t *)(VIRTIO_NET_BASE + (offset)))
#define NET_QUEUE_SIZE 64
#define ETH_FRAME_SIZE 1536
#define NET_DEBUG 0

#if NET_DEBUG
#define NET_LOG(...) lib_printf(__VA_ARGS__)
#else
#define NET_LOG(...) do { } while (0)
#endif

typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[NET_QUEUE_SIZE];
} net_virtq_avail_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[NET_QUEUE_SIZE];
} net_virtq_used_t;

typedef struct {
    char pages[2 * PGSIZE] __attribute__((aligned(PGSIZE)));
    virtq_desc_t *desc; net_virtq_avail_t *avail; net_virtq_used_t *used; uint16_t used_idx;
    char tx_pages[2 * PGSIZE] __attribute__((aligned(PGSIZE)));
    virtq_desc_t *tx_desc; net_virtq_avail_t *tx_avail; net_virtq_used_t *tx_used; uint16_t tx_used_idx;
    uint8_t rx_buffers[NET_QUEUE_SIZE][ETH_FRAME_SIZE] __attribute__((aligned(16)));
    uint8_t tx_buffers[NET_QUEUE_SIZE][ETH_FRAME_SIZE] __attribute__((aligned(16)));
} virtio_net_t;

static virtio_net_t vnet;
static int net_ready = 0;
static volatile int net_irq_pending = 0;
static unsigned int net_irq_count = 0;
static uint16_t rx_queue_size = NET_QUEUE_SIZE;
static uint16_t tx_queue_size = NET_QUEUE_SIZE;
struct netif vnet_netif;
extern void network_task_notify(void);

static uint16_t tx_free_list[NET_QUEUE_SIZE];
static uint16_t tx_h = 0, tx_t = 0;

static uint16_t be16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void log_eth_tcp(const char *tag, const uint8_t *frame, uint32_t frame_len) {
    if (frame_len < 14) return;
    uint16_t eth_type = be16(frame + 12);
    if (eth_type != 0x0800) return;
    if (frame_len < 34) return;

    const uint8_t *ip = frame + 14;
    uint8_t ihl = (ip[0] & 0x0f) * 4;
    uint8_t proto = ip[9];
    uint16_t ip_len = be16(ip + 2);
    if (proto != 6 || frame_len < 14 + ihl + 20) return;

    const uint8_t *tcp = ip + ihl;
    uint8_t tcp_hl = ((tcp[12] >> 4) & 0x0f) * 4;
    uint16_t sport = be16(tcp + 0);
    uint16_t dport = be16(tcp + 2);
    uint32_t seq = be32(tcp + 4);
    uint32_t ack = be32(tcp + 8);
    uint8_t flags = tcp[13];
    uint16_t win = be16(tcp + 14);
    int tcp_payload = (int)ip_len - (int)ihl - (int)tcp_hl;
    if (tcp_payload < 0) tcp_payload = 0;

    NET_LOG("[net] %s tcp %u->%u seq=%u ack=%u win=%u fl=%x pay=%d frame=%u\n",
            tag, sport, dport, seq, ack, win, flags, tcp_payload, frame_len);
}

void tx_init() { for(int i=0; i<tx_queue_size; i++) tx_free_list[i]=i; tx_h=0; tx_t=tx_queue_size; }
void tx_recycle() {
    while(vnet.tx_used_idx != vnet.tx_used->idx) {
        uint16_t id = vnet.tx_used->ring[vnet.tx_used_idx % tx_queue_size].id;
        tx_free_list[tx_t % tx_queue_size] = id; tx_t++; vnet.tx_used_idx++;
    }
}
err_t net_linkout(struct netif *ni, struct pbuf *p) {
    if(tx_h == tx_t) {
        NET_LOG("[net] tx ERR_MEM len=%u h=%u t=%u\n", p->tot_len, tx_h, tx_t);
        return ERR_MEM;
    }
    uint16_t id = tx_free_list[tx_h % tx_queue_size]; tx_h++;
    NET_LOG("[net] tx send len=%u id=%u h=%u t=%u\n", p->tot_len, id, tx_h, tx_t);
    memset(vnet.tx_buffers[id], 0, 10); pbuf_copy_partial(p, vnet.tx_buffers[id]+10, p->tot_len, 0);
    log_eth_tcp("tx", vnet.tx_buffers[id] + 10, p->tot_len);
    vnet.tx_desc[id].addr = (uint64)(uint32)vnet.tx_buffers[id];
    vnet.tx_desc[id].len = p->tot_len+10; vnet.tx_desc[id].flags = 0;
    vnet.tx_avail->ring[vnet.tx_avail->idx % tx_queue_size] = id;
    __sync_synchronize(); vnet.tx_avail->idx++;
    NET_LOG("[net] tx notify avail->idx=%u\n", vnet.tx_avail->idx);
    *R_NET(VIRTIO_MMIO_QUEUE_NOTIFY) = 1; return ERR_OK;
}
err_t net_init_cb(struct netif *n) {
    volatile uint8_t *cfg = (volatile uint8_t *)(VIRTIO_NET_BASE + 0x100);
    for(int i=0; i<6; i++) n->hwaddr[i] = cfg[i];
    n->linkoutput = net_linkout; n->output = etharp_output; n->hwaddr_len = 6;
    n->mtu = 1500; n->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    return ERR_OK;
}

static struct tcp_pcb *http_listener;
static uint32_t http_accept_count;
static uint32_t http_recv_count;
static uint32_t http_state;

uint32_t http_server_debug_word(void)
{
    return 0x48000000UL |
           ((http_accept_count & 0xffu) << 16) |
           ((http_recv_count & 0xffu) << 8) |
           (http_state & 0xffu);
}

static void http_close_pcb(struct tcp_pcb *pcb)
{
    if (!pcb) return;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    if (tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb);
    }
}

static err_t http_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    (void)arg;
    (void)len;
    http_state = 0x05;
    http_close_pcb(pcb);
    return ERR_OK;
}

static err_t http_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void)arg;
    if (err != ERR_OK) {
        http_state = 0xe4;
        if (p) pbuf_free(p);
        http_close_pcb(pcb);
        return ERR_OK;
    }
    if (p == NULL) {
        http_state = 0x06;
        http_close_pcb(pcb);
        return ERR_OK;
    }

    http_recv_count++;
    http_state = 0x03;
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    const char *body =
        "<html><body><h1>VibeOS Ethernet OK</h1>"
        "<p>lwIP + Zynq GEM0 is running.</p>"
        "<p>IP: 192.168.0.154</p>"
        "</body></html>";
    char resp[256];
    int body_len = (int)strlen(body);
    int n = snprintf(resp, sizeof(resp),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Type: text/html\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n"
                     "\r\n"
                     "%s",
                     body_len, body);
    if (n <= 0 || n >= (int)sizeof(resp)) {
        http_state = 0xe5;
        http_close_pcb(pcb);
        return ERR_OK;
    }

    err_t wr = tcp_write(pcb, resp, (u16_t)n, TCP_WRITE_FLAG_COPY);
    if (wr != ERR_OK) {
        http_state = 0xe6;
        http_close_pcb(pcb);
        return ERR_OK;
    }
    tcp_sent(pcb, http_sent);
    tcp_output(pcb);
    http_state = 0x04;
    return ERR_OK;
}

static err_t http_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK || !pcb) {
        http_state = 0xe3;
        return ERR_VAL;
    }
    http_accept_count++;
    http_state = 0x02;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, http_recv);
    return ERR_OK;
}

void start_http(void)
{
    if (http_listener) return;

    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) {
        http_state = 0xe0;
        return;
    }

    err_t rc = tcp_bind(pcb, IP_ADDR_ANY, 80);
    if (rc != ERR_OK) {
        http_state = 0xe1;
        tcp_abort(pcb);
        return;
    }

    http_listener = tcp_listen(pcb);
    if (!http_listener) {
        http_state = 0xe2;
        tcp_abort(pcb);
        return;
    }
    tcp_accept(http_listener, http_accept);
    http_state = 0x01;
}

void virtio_net_rx_loop2() {
    if(!net_ready) return;
    NET_LOG("[net] rx_loop used_idx=%u used->idx=%u pending=%d\n", vnet.used_idx, vnet.used->idx, net_irq_pending);
    net_irq_pending = 0;
    tx_recycle();
    int recycled = 0;
    int budget = 4;
    while(vnet.used_idx != vnet.used->idx && budget-- > 0) {
        uint16_t id = vnet.used->ring[vnet.used_idx % rx_queue_size].id;
        uint32_t len = vnet.used->ring[vnet.used_idx % rx_queue_size].len;
        NET_LOG("[net] rx desc id=%u len=%u slot=%u\n", id, len, vnet.used_idx % rx_queue_size);
        if (len > 10) log_eth_tcp("rx", vnet.rx_buffers[id] + 10, len - 10);
        struct pbuf *p = pbuf_alloc(PBUF_RAW, len-10, PBUF_POOL);
        if(p){
            pbuf_take(p, vnet.rx_buffers[id]+10, len-10);
            err_t in_err = vnet_netif.input(p, &vnet_netif);
            NET_LOG("[net] input ret=%d len=%u\n", in_err, len - 10);
            if(in_err!=ERR_OK) {
                NET_LOG("[net] input drop len=%u\n", len - 10);
                pbuf_free(p);
            }
        } else {
            NET_LOG("[net] pbuf_alloc fail len=%u\n", len - 10);
        }
        vnet.avail->ring[vnet.avail->idx % rx_queue_size] = id;
        __sync_synchronize(); vnet.avail->idx++; vnet.used_idx++; recycled++;
    }
    if (recycled > 0) {
        NET_LOG("[net] recycled=%d avail->idx=%u notify\n", recycled, vnet.avail->idx);
        *R_NET(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;
    }
    if (vnet.used_idx != vnet.used->idx) {
        net_irq_pending = 1;
    }
}
void virtio_net_init() {
    uint32_t qmax;
    if (*R_NET(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976) return;
    *R_NET(VIRTIO_MMIO_STATUS) = 0; *R_NET(VIRTIO_MMIO_STATUS) = 3; *R_NET(VIRTIO_MMIO_DRIVER_FEATURES) = 0;
    *R_NET(VIRTIO_MMIO_GUEST_PAGE_SIZE) = PGSIZE;
    *R_NET(VIRTIO_MMIO_QUEUE_SEL) = 0;
    qmax = *R_NET(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (qmax == 0) return;
    if (qmax < rx_queue_size) rx_queue_size = (uint16_t)qmax;
    *R_NET(VIRTIO_MMIO_QUEUE_NUM) = rx_queue_size;
    memset(vnet.pages, 0, sizeof(vnet.pages));
    vnet.desc = (virtq_desc_t *)vnet.pages;
    vnet.avail = (net_virtq_avail_t *)(vnet.pages + rx_queue_size*sizeof(virtq_desc_t));
    vnet.used = (net_virtq_used_t *)(vnet.pages + PGSIZE);
    for(int i=0; i<rx_queue_size; i++) { vnet.desc[i].addr=(uint64)(uint32)&vnet.rx_buffers[i]; vnet.desc[i].len=ETH_FRAME_SIZE; vnet.desc[i].flags=2; vnet.avail->ring[i]=i; }
    vnet.avail->idx = rx_queue_size; *R_NET(VIRTIO_MMIO_QUEUE_PFN) = (uint32)vnet.pages/PGSIZE;
    *R_NET(VIRTIO_MMIO_QUEUE_NOTIFY) = 0;
    *R_NET(VIRTIO_MMIO_QUEUE_SEL) = 1;
    qmax = *R_NET(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (qmax == 0) return;
    if (qmax < tx_queue_size) tx_queue_size = (uint16_t)qmax;
    *R_NET(VIRTIO_MMIO_QUEUE_NUM) = tx_queue_size;
    memset(vnet.tx_pages, 0, sizeof(vnet.tx_pages));
    vnet.tx_desc = (virtq_desc_t *)vnet.tx_pages;
    vnet.tx_avail = (net_virtq_avail_t *)(vnet.tx_pages + tx_queue_size*sizeof(virtq_desc_t));
    vnet.tx_used = (net_virtq_used_t *)(vnet.tx_pages + PGSIZE);
    *R_NET(VIRTIO_MMIO_QUEUE_PFN) = (uint32)vnet.tx_pages/PGSIZE;
    *R_NET(VIRTIO_MMIO_STATUS) |= 4; lwip_init();
    ip4_addr_t ip, nm, gw; IP4_ADDR(&ip,192,168,123,1); IP4_ADDR(&nm,255,255,255,0); IP4_ADDR(&gw,192,168,123,1);
    netif_add(&vnet_netif, &ip, &nm, &gw, NULL, net_init_cb, ethernet_input);
    netif_set_default(&vnet_netif); netif_set_up(&vnet_netif);
    tx_init(); start_http(); net_ready = 1; lib_puts("Net Ready\n");
}
void virtio_net_interrupt_handler() {
    *R_NET(VIRTIO_MMIO_INTERRUPT_ACK) = *R_NET(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;
    net_irq_count++;
    NET_LOG("[net] irq #%u used_idx=%u used->idx=%u\n", net_irq_count, vnet.used_idx, vnet.used->idx);
    net_irq_pending = 1;
    network_task_notify();
}
int virtio_net_has_pending_irq() { return net_irq_pending; }
int virtio_net_has_rx_ready() {
    return vnet.used_idx != vnet.used->idx;
}
int virtio_net_rx_pending_count() {
    return (int)((uint16_t)(vnet.used->idx - vnet.used_idx));
}
