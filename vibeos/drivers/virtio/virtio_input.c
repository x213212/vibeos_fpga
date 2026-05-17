#include "virtio.h"
#include "os.h"
#include "vga.h"

#define CTRLDBG_PRINTF(...) do { } while (0)

#define INPUT_QUEUE_SIZE 64
#define INPUT_EVENT_QUEUE_SIZE 512

int gui_mx=WIDTH/2, gui_my=HEIGHT/2, gui_clicked=0, gui_click_pending=0, gui_wheel=0;
int gui_right_clicked=0, gui_right_click_pending=0;
int gui_shortcut_new_task = 0;
int gui_shortcut_close_task = 0;
int gui_shortcut_switch_task = 0; 
int gui_ctrl_pressed = 0;
char gui_key=0;
int gui_prev_regs_pressed = 0;
int gbemu_btn_a = 0;
int gbemu_btn_b = 0;
int gbemu_btn_start = 0;
int gbemu_btn_select = 0;
int gbemu_btn_up = 0;
int gbemu_btn_down = 0;
int gbemu_btn_left = 0;
int gbemu_btn_right = 0;
extern int os_debug;
extern volatile int gui_redraw_needed;

static void enqueue_input_event(uint16 device, uint16 type, uint16 code, uint32 value);
void virtio_input_poll(void);

#define GUI_KEY_QUEUE_SIZE 64
static unsigned char gui_key_queue[GUI_KEY_QUEUE_SIZE];
static volatile int gui_key_head = 0;
static volatile int gui_key_tail = 0;

static void gui_key_submit(char key)
{
    if (key == 0) return;
    if (key == 3) {
        gui_key = key;
        gui_key_head = gui_key_tail;
        return;
    }
    if (gui_key == 0 && gui_key_head == gui_key_tail) {
        gui_key = key;
        return;
    }
    int next = (gui_key_tail + 1) % GUI_KEY_QUEUE_SIZE;
    if (next == gui_key_head) {
        gui_key_head = (gui_key_head + 1) % GUI_KEY_QUEUE_SIZE;
    }
    gui_key_queue[gui_key_tail] = (unsigned char)key;
    gui_key_tail = next;
}

int gui_key_pull_next(void)
{
    if (gui_key != 0) return 1;
    if (gui_key_head == gui_key_tail) return 0;
    gui_key = (char)gui_key_queue[gui_key_head];
    gui_key_head = (gui_key_head + 1) % GUI_KEY_QUEUE_SIZE;
    return gui_key != 0;
}

int gui_key_queued(void)
{
    return gui_key != 0 || gui_key_head != gui_key_tail;
}

#ifdef FPGA_MINIMAL
#define FPGA_MOUSE_BASE 0x10004000UL
#define INPUT_VISIBLE_HEIGHT 300

#define USB0_USBCMD       (*(volatile uint32 *)0xE0002140UL)
#define USB0_USBSTS       (*(volatile uint32 *)0xE0002144UL)
#define USB0_FRINDEX      (*(volatile uint32 *)0xE000214CUL)
#define USB0_CTRLDSSEG    (*(volatile uint32 *)0xE0002150UL)
#define USB0_PERIODICLIST (*(volatile uint32 *)0xE0002154UL)
#define USB0_ASYNCLIST    (*(volatile uint32 *)0xE0002158UL)
#define USB0_TTCTRL       (*(volatile uint32 *)0xE000215CUL)
#define USB0_BURSTSIZE    (*(volatile uint32 *)0xE0002160UL)
#define USB0_ULPI_VIEW    (*(volatile uint32 *)0xE0002170UL)
#define USB0_CONFIGFLAG   (*(volatile uint32 *)0xE0002180UL)
#define USB0_PORTSC1      (*(volatile uint32 *)0xE0002184UL)
#define USB0_OTGSC        (*(volatile uint32 *)0xE00021A4UL)
#define USB0_USBMODE      (*(volatile uint32 *)0xE00021A8UL)
#define USB0_SBUSCFG      (*(volatile uint32 *)0xE0002090UL)
#define GPIO_DATA1        (*(volatile uint32 *)0xE000A044UL)
#define GPIO_DIRM1        (*(volatile uint32 *)0xE000A244UL)
#define GPIO_OEN1         (*(volatile uint32 *)0xE000A248UL)

#define USB_ENUM_START_DELAY_POLLS 60u
#define USB_ENUM_RETRY_POLLS       600u
#define USB_ENUM_RETRY_MS          500u
#define USB_ENUM_POWER_SETTLE_MS   250u
#define USB_ENUM_STABLE_POLLS      12u
#define USB_CONTROL_TIMEOUT_UFRAMES 500u
#define USB_NO_CONNECT_STABLE_POLLS 150u
#define USB_PHY_RECOVER_RETRY_MS    3000u
#define USBMODE_HOST               0x00000003u

#define EHCI_LINK_TERM    1u
#define EHCI_LINK_QH      2u
#define EHCI_QTD_ACTIVE   0x00000080u
#define EHCI_QTD_HALTED   0x00000040u
#define EHCI_PID_OUT      0u
#define EHCI_PID_IN       1u
#define EHCI_PID_SETUP    2u

#define USB_PORTSC_CCS    0x00000001u
#define USB_PORTSC_CSC    0x00000002u
#define USB_PORTSC_PE     0x00000004u
#define USB_PORTSC_PEC    0x00000008u
#define USB_PORTSC_OCC    0x00000020u
#define USB_PORTSC_PR     0x00000100u
#define USB_PORTSC_PP     0x00001000u
#define USB_PORTSC_PFSC   0x01000000u
#define USB_PORTSC_W1C    (USB_PORTSC_CSC | USB_PORTSC_PEC | USB_PORTSC_OCC)
#define USB_INTR_ACTIVE_TIMEOUT_POLLS 250000
#define USB_KBD_ACTIVE_TIMEOUT_POLLS 0
#define USB_PHY_RESET_MIO46_BIT 0x00004000u

struct ehci_qtd {
    uint32 next;
    uint32 alt_next;
    uint32 token;
    uint32 buf[5];
    uint32 buf_hi[5];
} __attribute__((packed, aligned(32)));

struct ehci_qh {
    uint32 horiz;
    uint32 ep_char;
    uint32 ep_cap;
    uint32 cur_qtd;
    struct ehci_qtd overlay;
} __attribute__((packed, aligned(64)));

struct usb_setup_pkt {
    uint8 bmRequestType;
    uint8 bRequest;
    uint16 wValue;
    uint16 wIndex;
    uint16 wLength;
} __attribute__((packed));

struct usb_mouse_state {
    struct ehci_qh async_head;
    struct ehci_qh ctrl_qh;
    struct ehci_qh intr_qh;
    struct ehci_qtd qtd[4];
    struct ehci_qh kbd_qh;
    struct ehci_qtd kbd_qtd;
    uint32 periodic[1024] __attribute__((aligned(4096)));
    struct usb_setup_pkt setup;
    uint8 data[256] __attribute__((aligned(32)));
    uint8 report[8] __attribute__((aligned(32)));
    uint8 kbd_report[8] __attribute__((aligned(32)));
    uint8 kbd_prev_report[8] __attribute__((aligned(32)));
    int ready;
    int mouse_ready;
    int kbd_ready;
    int addr;
    int iface;
    int kbd_iface;
    int intr_ep;
    int intr_mps;
    int kbd_ep;
    int kbd_mps;
    int ep0_mps;
    int speed;
    int tt_hub;
    int tt_port;
    int direct_async;
    int speed_try;
    int last_fail;
    int poll_div;
    int retry_div;
    int retry_after;
    int intr_pending;
    int intr_wait_ticks;
    int intr_dt;
    int intr_halt_count;
    int kbd_pending;
    int kbd_wait_ticks;
    int kbd_dt;
    int kbd_halt_count;
    uint32 last_buttons;
    uint32 last_modifiers;
} __attribute__((aligned(4096)));

#define USB_MOUSE_DMA_CPU_BASE  0x851da000UL
#define USB_MOUSE_DMA_CPU_SIZE  0x00010000UL
#define USB_MOUSE_DMA_PHYS_BASE 0x061da000UL
#define usb_mouse (*(struct usb_mouse_state *)USB_MOUSE_DMA_CPU_BASE)

static int usb_mouse_state_cleared;
static int usb_host_power_kicked;
static int usb_host_controller_initialized;
static uint32 usb_enum_poll_count;
static uint32 usb_enum_next_poll;
static uint32 usb_enum_retry_after_ms;
static uint32 usb_last_port_connected;
static uint32 usb_connect_stable_polls;
static uint32 usb_no_connect_polls;
static uint32 usb_phy_recover_after_ms;
static volatile int usb_poll_busy;

static void usb_ulpi_configure_host_phy(void);

static void usb_mouse_ensure_state(void)
{
    if (!usb_mouse_state_cleared) {
        memset(&usb_mouse, 0, sizeof(usb_mouse));
        usb_mouse_state_cleared = 1;
    }
}

static void usb_mouse_mark(int code)
{
    volatile uint32 *m = (volatile uint32 *)FPGA_MOUSE_BASE;
    m[15] = (uint32)code;
}

static uint32 usb_phys(const void *p)
{
    uint32 a = (uint32)p;
    if (a >= 0x80000000UL && a < 0xC0000000UL) return a - 0x80000000UL + 0x01000000UL;
    return a;
}

static inline void usb_dcache_writeback(uint32 addr)
{
    asm volatile("csrw 0x3a1, %0" :: "r"(addr) : "memory");
}

static inline void usb_dcache_invalidate(uint32 addr)
{
    asm volatile("csrw 0x3a2, %0" :: "r"(addr) : "memory");
}

static void usb_cache_writeback_range(const void *p, int len)
{
    uint32 a = ((uint32)p) & ~31u;
    uint32 e = ((uint32)p) + (uint32)len;
    while (a < e) {
        usb_dcache_writeback(a);
        usb_dcache_writeback(usb_phys((const void *)a));
        a += 32;
    }
    asm volatile("fence rw,rw" ::: "memory");
}

static void usb_cache_invalidate_range(const void *p, int len)
{
    uint32 a = ((uint32)p) & ~31u;
    uint32 e = ((uint32)p) + (uint32)len;
    while (a < e) {
        usb_dcache_invalidate(a);
        usb_dcache_invalidate(usb_phys((const void *)a));
        a += 32;
    }
    asm volatile("fence rw,rw" ::: "memory");
}

static void usb_delay(int n)
{
    for (volatile int i = 0; i < n; i++) asm volatile("");
}

static void usb_wait_uframes(uint32 uframes)
{
    uint32 last = USB0_FRINDEX & 0x3fffu;
    uint32 seen = 0;
    uint32 guard = uframes * 20000u + 200000u;
    if (guard > 5000000u) guard = 5000000u;
    while (seen < uframes && guard-- > 0) {
        uint32 cur = USB0_FRINDEX & 0x3fffu;
        uint32 delta = (cur - last) & 0x3fffu;
        if (delta) {
            seen += delta;
            last = cur;
        }
    }
}

static void usb_wait_ms(uint32 ms)
{
    usb_wait_uframes(ms * 8u);
}

static void usb_mouse_force_recover(int mark)
{
    usb_mouse_mark(mark);
    USB0_USBCMD = USB0_USBCMD & ~0x00000030u;
    USB0_ASYNCLIST = 0;
    USB0_PERIODICLIST = 0;
    memset(&usb_mouse, 0, sizeof(usb_mouse));
    usb_mouse_state_cleared = 1;
    usb_host_power_kicked = 0;
    usb_host_controller_initialized = 0;
    USB0_USBSTS = 0x0000003fu;
    USB0_USBCMD = 0x00080001u;
}

static void usb_async_stop(void)
{
    USB0_USBCMD = USB0_USBCMD & ~0x00000020u;
    for (int i = 0; i < 200000; i++) {
        if ((USB0_USBSTS & 0x00008000u) == 0) break;
    }
    USB0_ASYNCLIST = 0;
    USB0_USBSTS = 0x0000003fu;
}

static void usb_async_restart(struct ehci_qh *head)
{
    USB0_USBCMD = USB0_USBCMD & ~0x00000020u;
    for (int i = 0; i < 200000; i++) {
        if ((USB0_USBSTS & 0x00008000u) == 0) break;
    }
    USB0_USBSTS = 0x0000003fu;
    USB0_ASYNCLIST = usb_phys(head);
    asm volatile("fence rw,rw" ::: "memory");
    USB0_USBCMD = USB0_USBCMD | 0x00000021u;
}

static uint32 usb_portsc_preserve(uint32 p)
{
    /*
     * PORTSC contains write-one-to-clear/status bits.  Keep PE when doing
     * read-modify-write updates; on EHCI, writing PE=0 can disable a live
     * port.  Only mask change bits and PR unless the caller sets them.
     */
    return p & ~(USB_PORTSC_W1C | USB_PORTSC_PR);
}

static void usb_port_power_on(void)
{
    USB0_PORTSC1 = usb_portsc_preserve(USB0_PORTSC1) | USB_PORTSC_PP;
}

static int usb_port_reset(void)
{
    volatile uint32 *m = (volatile uint32 *)FPGA_MOUSE_BASE;

    USB0_USBCMD = USB0_USBCMD & ~0x00000030u;
    for (int i = 0; i < 300000; i++) {
        if ((USB0_USBSTS & 0x0000c000u) == 0) break;
    }
    USB0_ASYNCLIST = 0;
    USB0_PERIODICLIST = 0;

    if ((USB0_PORTSC1 & USB_PORTSC_CCS) == 0) return -1;

    USB0_PORTSC1 = usb_portsc_preserve(USB0_PORTSC1) | USB_PORTSC_PP | USB_PORTSC_PR;
    usb_wait_ms(60);
    USB0_PORTSC1 = usb_portsc_preserve(USB0_PORTSC1) | USB_PORTSC_PP;

    for (int i = 0; i < 3000000; i++) {
        uint32 p = USB0_PORTSC1;
        if ((p & USB_PORTSC_PR) == 0 && (p & USB_PORTSC_PE) != 0) break;
    }
    usb_wait_ms(300);

    m[8] = USB0_PORTSC1;
    m[9] = USB0_USBCMD;
    m[10] = USB0_USBSTS;
    m[11] = USB0_USBMODE;
    return ((USB0_PORTSC1 & USB_PORTSC_PE) != 0) ? 0 : -2;
}

static void usb_clear_port_changes(void)
{
    uint32 p = USB0_PORTSC1;
    if (p & (USB_PORTSC_CSC | USB_PORTSC_PEC | USB_PORTSC_OCC)) {
        USB0_PORTSC1 = (usb_portsc_preserve(p) | USB_PORTSC_PP |
                        (p & USB_PORTSC_W1C)) & ~USB_PORTSC_PR;
        usb_delay(20000);
    }
}

static int usb_controller_reset(void)
{
    USB0_USBCMD = 0;
    usb_delay(200000);
    USB0_USBCMD = 0x00000002u;
    for (int i = 0; i < 3000000; i++) {
        if ((USB0_USBCMD & 0x00000002u) == 0) break;
    }
    if (USB0_USBCMD & 0x00000002u) return -1;

    USB0_USBMODE = USBMODE_HOST;
    USB0_CONFIGFLAG = 0x00000001u;
    USB0_SBUSCFG = 0x00000007u;
    USB0_CTRLDSSEG = 0x00000000u;
    USB0_TTCTRL = 0x00000000u;
    USB0_BURSTSIZE = 0x00001010u;
    USB0_ASYNCLIST = 0;
    USB0_PERIODICLIST = 0;
    USB0_OTGSC = (USB0_OTGSC & ~0x007f0001u) | 0x00000026u;
    usb_port_power_on();
    USB0_USBSTS = 0x0000003fu;
    USB0_USBCMD = 0x00080001u;
    usb_delay(800000);
    return 0;
}

static int usb_host_prepare(void)
{
    volatile uint32 *m = (volatile uint32 *)FPGA_MOUSE_BASE;

    /*
     * The loader already recovers the external ULPI PHY and leaves PORTSC1 at
     * a connected/enabled state.  Reprogramming ULPI or resetting the port here
     * can drop a working keyboard back to no-connect, so OS-side prepare only
     * verifies host-mode registers and keeps the physical port untouched.
     */
    if ((USB0_USBMODE & 0x03u) != USBMODE_HOST) USB0_USBMODE = USBMODE_HOST;
    USB0_CONFIGFLAG = 0x00000001u;
    USB0_SBUSCFG = 0x00000007u;
    USB0_CTRLDSSEG = 0x00000000u;
    USB0_TTCTRL = 0x00000000u;
    USB0_BURSTSIZE = 0x00001010u;
    USB0_USBSTS = 0x0000003fu;
    USB0_USBCMD = (USB0_USBCMD | 0x00080001u);

    for (int i = 0; i < 3000000; i++) {
        uint32 p = USB0_PORTSC1;
        if (p & USB_PORTSC_CCS) break;
    }

    usb_clear_port_changes();
    USB0_USBSTS = 0x0000003fu;

    m[8] = USB0_PORTSC1;
    m[9] = USB0_USBCMD;
    m[10] = USB0_USBSTS;
    m[11] = USB0_USBMODE;
    /*
     * PE is set by usb_port_reset().  A connected-but-not-enabled port must
     * not fail prepare, otherwise the retry loop never reaches bus reset.
     */
    return (USB0_PORTSC1 & USB_PORTSC_CCS) ? 0 : -1;
}

static void usb_periodic_start(struct ehci_qh *qh)
{
    uint32 link = usb_phys(qh) | EHCI_LINK_QH;

    USB0_USBCMD = USB0_USBCMD & ~0x00000010u;
    for (int i = 0; i < 200000; i++) {
        if ((USB0_USBSTS & 0x00004000u) == 0) break;
    }

    for (int i = 0; i < 1024; i++) usb_mouse.periodic[i] = link;
    qh->horiz = EHCI_LINK_TERM;

    usb_cache_writeback_range(usb_mouse.periodic, sizeof(usb_mouse.periodic));
    usb_cache_writeback_range(qh, sizeof(*qh));
    USB0_PERIODICLIST = usb_phys(usb_mouse.periodic);
    asm volatile("fence rw,rw" ::: "memory");
    USB0_USBCMD = USB0_USBCMD | 0x00000011u;
}

static void usb_periodic_keepalive(struct ehci_qh *qh)
{
    uint32 plist = usb_phys(usb_mouse.periodic);
    uint32 link = usb_phys(qh) | EHCI_LINK_QH;

    if (USB0_PERIODICLIST != plist) {
        for (int i = 0; i < 1024; i++) usb_mouse.periodic[i] = link;
        usb_cache_writeback_range(usb_mouse.periodic, sizeof(usb_mouse.periodic));
        USB0_PERIODICLIST = plist;
    }
    if ((USB0_USBCMD & 0x00000010u) == 0) {
        usb_cache_writeback_range(qh, sizeof(*qh));
        asm volatile("fence rw,rw" ::: "memory");
        USB0_USBCMD = USB0_USBCMD | 0x00000011u;
    }
}

static void usb_intr_endpoint_recover(struct ehci_qh *qh)
{
    uint32 plist = usb_phys(usb_mouse.periodic);
    uint32 link = usb_phys(qh) | EHCI_LINK_QH;

    USB0_USBCMD = USB0_USBCMD & ~0x00000010u;
    for (int i = 0; i < 200000; i++) {
        if ((USB0_USBSTS & 0x00004000u) == 0) break;
    }

    qh->cur_qtd = 0;
    qh->overlay.next = EHCI_LINK_TERM;
    qh->overlay.alt_next = EHCI_LINK_TERM;
    qh->overlay.token = 0;
    usb_cache_writeback_range(qh, sizeof(*qh));

    for (int i = 0; i < 1024; i++) usb_mouse.periodic[i] = link;
    usb_cache_writeback_range(usb_mouse.periodic, sizeof(usb_mouse.periodic));
    asm volatile("fence rw,rw" ::: "memory");

    USB0_PERIODICLIST = plist;
    USB0_USBSTS = 0x0000003fu;
    USB0_USBCMD = USB0_USBCMD | 0x00000011u;
    if (qh == &usb_mouse.kbd_qh) {
        usb_mouse.kbd_pending = 0;
        usb_mouse.kbd_wait_ticks = 0;
        usb_mouse.kbd_halt_count = 0;
    } else {
        usb_mouse.intr_pending = 0;
        usb_mouse.intr_wait_ticks = 0;
        usb_mouse.intr_halt_count = 0;
    }
}

static void usb_qtd_init(struct ehci_qtd *q, const void *buf, int len, int pid, int dt, int ioc)
{
    memset(q, 0, sizeof(*q));
    q->next = EHCI_LINK_TERM;
    q->alt_next = EHCI_LINK_TERM;
    q->token = EHCI_QTD_ACTIVE | (3u << 10) | ((uint32)pid << 8) |
               ((uint32)len << 16) | (dt ? 0x80000000u : 0) | (ioc ? 0x00008000u : 0);
    if (buf && len > 0) q->buf[0] = usb_phys(buf);
}

static void usb_qh_init(struct ehci_qh *qh, int addr, int ep, int mps, int speed, int control)
{
    uint32 rl = 4u;
    memset(qh, 0, sizeof(*qh));
    qh->horiz = usb_phys(qh) | EHCI_LINK_QH;
    qh->ep_char = (rl << 28) | (uint32)addr | ((uint32)ep << 8) | ((uint32)speed << 12) |
                  ((ep == 0) ? (1u << 14) : 0) | ((uint32)mps << 16) |
                  (control ? (1u << 27) : 0);
    qh->ep_cap = (1u << 30);
    if (speed != 2) qh->ep_cap |= (1u << 23);
    qh->overlay.next = EHCI_LINK_TERM;
    qh->overlay.alt_next = EHCI_LINK_TERM;
    qh->overlay.token = 0;
}

static void usb_intr_qh_init(struct ehci_qh *qh, int addr, int ep, int mps, int speed)
{
    usb_qh_init(qh, addr, ep, mps, speed, 0);
    qh->horiz = EHCI_LINK_TERM;
    qh->ep_char &= ~(0xfu << 28);
    qh->ep_cap = (1u << 30) | 0x00000001u; /* mult=1, S-mask: poll in microframe 0 */
    if (speed != 2) {
        qh->ep_cap |= (1u << 23) | 0x00001c00u; /* root-port TT + split completion mask */
    }
}

static void usb_async_head_init(struct ehci_qh *head, struct ehci_qh *next)
{
    memset(head, 0, sizeof(*head));
    head->horiz = usb_phys(next) | EHCI_LINK_QH;
    head->ep_char = (4u << 28) | (64u << 16) | (1u << 15) | (1u << 14) | (2u << 12);
    head->ep_cap = (1u << 30);
    head->overlay.next = EHCI_LINK_TERM;
    head->overlay.alt_next = EHCI_LINK_TERM;
    head->overlay.token = EHCI_QTD_HALTED;
}

static int usb_wait_qtd(struct ehci_qtd *q)
{
    uint32 last = USB0_FRINDEX & 0x3fffu;
    uint32 seen = 0;
    uint32 guard = 20000000u;
    while (seen < USB_CONTROL_TIMEOUT_UFRAMES && guard-- > 0) {
        usb_cache_invalidate_range(q, sizeof(*q));
        uint32 t = q->token;
        if ((t & EHCI_QTD_ACTIVE) == 0) return (t & EHCI_QTD_HALTED) ? -2 : 0;
        uint32 cur = USB0_FRINDEX & 0x3fffu;
        uint32 delta = (cur - last) & 0x3fffu;
        if (delta) {
            seen += delta;
            last = cur;
        }
    }
    usb_cache_invalidate_range(q, sizeof(*q));
    uint32 t = q->token;
    if ((t & EHCI_QTD_ACTIVE) == 0) return (t & EHCI_QTD_HALTED) ? -2 : 0;
    return -1;
}

static int usb_control(int addr, uint8 rt, uint8 req, uint16 val, uint16 idx, void *data, int len)
{
    struct ehci_qtd *setup = &usb_mouse.qtd[0];
    struct ehci_qtd *dataq = &usb_mouse.qtd[1];
    struct ehci_qtd *status = &usb_mouse.qtd[2];
    struct ehci_qh *ctrl = &usb_mouse.ctrl_qh;
    int has_data = len > 0;
    int data_in = (rt & 0x80) != 0;

    usb_async_stop();

    usb_mouse.setup.bmRequestType = rt;
    usb_mouse.setup.bRequest = req;
    usb_mouse.setup.wValue = val;
    usb_mouse.setup.wIndex = idx;
    usb_mouse.setup.wLength = len;

    if (data && len > 0 && data_in) {
        memset(data, 0, len);
        usb_cache_writeback_range(data, len);
        usb_cache_invalidate_range(data, len);
    }

    usb_qtd_init(setup, &usb_mouse.setup, 8, EHCI_PID_SETUP, 0, 0);
    if (has_data) {
        usb_qtd_init(dataq, data, len, data_in ? EHCI_PID_IN : EHCI_PID_OUT, 1, 0);
        setup->next = usb_phys(dataq);
        usb_qtd_init(status, 0, 0, data_in ? EHCI_PID_OUT : EHCI_PID_IN, 1, 1);
        dataq->next = usb_phys(status);
    } else {
        usb_qtd_init(status, 0, 0, EHCI_PID_IN, 1, 1);
        setup->next = usb_phys(status);
    }

    usb_qh_init(ctrl, addr, 0, usb_mouse.ep0_mps ? usb_mouse.ep0_mps : 8,
                usb_mouse.speed, (usb_mouse.speed != 2) && !usb_mouse.direct_async);
    USB0_TTCTRL = 0x00000000u;
    usb_async_head_init(&usb_mouse.async_head, ctrl);
    ctrl->horiz = usb_phys(&usb_mouse.async_head) | EHCI_LINK_QH;
    ctrl->cur_qtd = 0;
    ctrl->overlay.next = usb_phys(setup);
    ctrl->overlay.alt_next = EHCI_LINK_TERM;
    ctrl->overlay.token = 0;
    usb_cache_writeback_range(&usb_mouse.setup, sizeof(usb_mouse.setup));
    usb_cache_writeback_range(setup, sizeof(*setup));
    if (has_data) usb_cache_writeback_range(dataq, sizeof(*dataq));
    usb_cache_writeback_range(status, sizeof(*status));
    if (data && len > 0 && !data_in) usb_cache_writeback_range(data, len);
    usb_cache_writeback_range(ctrl, sizeof(*ctrl));
    usb_cache_writeback_range(&usb_mouse.async_head, sizeof(usb_mouse.async_head));
    usb_async_restart(&usb_mouse.async_head);

    int wait_rc = usb_wait_qtd(status);
    usb_cache_invalidate_range(setup, sizeof(*setup));
    if (has_data) usb_cache_invalidate_range(dataq, sizeof(*dataq));
    usb_cache_invalidate_range(status, sizeof(*status));
    if (wait_rc != 0) {
        volatile uint32 *m = (volatile uint32 *)FPGA_MOUSE_BASE;
        usb_cache_invalidate_range(ctrl, sizeof(*ctrl));
        m[8] = setup->token;
        m[9] = has_data ? dataq->token : 0;
        m[10] = status->token;
        m[11] = USB0_ASYNCLIST;
        m[12] = USB0_USBSTS;
        m[13] = USB0_PORTSC1;
        m[14] = ctrl->ep_char;
        m[15] = ((uint32)(addr & 0xff) << 24) | ((uint32)rt << 16) |
                ((uint32)req << 8) | (uint32)(len & 0xff);
        usb_async_stop();
        return -1;
    }
    usb_async_stop();
    if (data && len > 0 && data_in) usb_cache_invalidate_range(data, len);
    return 0;
}

static int usb_try_dev_desc8(int speed, int tt_hub, int tt_port, int direct, int mark)
{
    usb_async_stop();
    if (mark != 123) usb_wait_ms(5);
    usb_wait_ms(20);
    usb_mouse.speed = speed;
    usb_mouse.tt_hub = tt_hub;
    usb_mouse.tt_port = tt_port;
    usb_mouse.direct_async = direct;
    usb_clear_port_changes();
    usb_mouse_mark(mark);
    return usb_control(0, 0x80, 6, 0x0100, 0, usb_mouse.data, 8);
}

static int usb_hid_parse_config(uint8 *d, int n)
{
    int iface = -1;
    int hid_iface = -1;
    int hid_proto = 0;
    usb_mouse.intr_ep = 0;
    usb_mouse.intr_mps = 4;
    usb_mouse.kbd_ep = 0;
    usb_mouse.kbd_mps = 8;
    usb_mouse.iface = -1;
    usb_mouse.kbd_iface = -1;
    for (int i = 0; i + 2 <= n && d[i] >= 2; i += d[i]) {
        if (i + d[i] > n) break;
        if (d[i + 1] == 4 && d[i] >= 9) {
            iface = d[i + 2];
            if (d[i + 5] == 3) {
                hid_iface = iface;
                hid_proto = d[i + 7];
            } else {
                hid_iface = -1;
                hid_proto = 0;
            }
        } else if (d[i + 1] == 5 && d[i] >= 7 && hid_iface >= 0) {
            if ((d[i + 3] & 3) == 3 && (d[i + 2] & 0x80)) {
                int ep = d[i + 2] & 0x0f;
                int mps = d[i + 4] | (d[i + 5] << 8);
                if ((hid_proto == 1 || mps >= 7) && usb_mouse.kbd_ep == 0) {
                    usb_mouse.kbd_iface = hid_iface;
                    usb_mouse.kbd_ep = ep;
                    usb_mouse.kbd_mps = mps;
                    if (usb_mouse.kbd_mps > (int)sizeof(usb_mouse.kbd_report))
                        usb_mouse.kbd_mps = sizeof(usb_mouse.kbd_report);
                } else if (usb_mouse.intr_ep == 0) {
                    usb_mouse.iface = hid_iface;
                    usb_mouse.intr_ep = ep;
                    usb_mouse.intr_mps = mps;
                    if (usb_mouse.intr_mps > (int)sizeof(usb_mouse.report))
                        usb_mouse.intr_mps = sizeof(usb_mouse.report);
                }
            }
        }
    }
    return (usb_mouse.intr_ep || usb_mouse.kbd_ep) ? 0 : -1;
}

static int usb_hid_key_to_linux(uint8 usage)
{
    static const uint8 map[128] = {
        [0x04]=30, [0x05]=48, [0x06]=46, [0x07]=32, [0x08]=18, [0x09]=33,
        [0x0a]=34, [0x0b]=35, [0x0c]=23, [0x0d]=36, [0x0e]=37, [0x0f]=38,
        [0x10]=50, [0x11]=49, [0x12]=24, [0x13]=25, [0x14]=16, [0x15]=19,
        [0x16]=31, [0x17]=20, [0x18]=22, [0x19]=47, [0x1a]=17, [0x1b]=45,
        [0x1c]=21, [0x1d]=44,
        [0x1e]=2, [0x1f]=3, [0x20]=4, [0x21]=5, [0x22]=6, [0x23]=7,
        [0x24]=8, [0x25]=9, [0x26]=10, [0x27]=11,
        [0x28]=28, [0x29]=1, [0x2a]=14, [0x2b]=15, [0x2c]=57,
        [0x2d]=12, [0x2e]=13, [0x2f]=26, [0x30]=27, [0x31]=43,
        [0x33]=39, [0x34]=40, [0x35]=41, [0x36]=51, [0x37]=52, [0x38]=53,
        [0x39]=58,
        [0x4f]=106, [0x50]=105, [0x51]=108, [0x52]=103,
        [0x49]=110, [0x4a]=102, [0x4b]=104, [0x4c]=111, [0x4d]=107, [0x4e]=109,
        [0x54]=98, [0x55]=55, [0x56]=74, [0x57]=78, [0x58]=96,
        [0x59]=79, [0x5a]=80, [0x5b]=81, [0x5c]=75, [0x5d]=76,
        [0x5e]=77, [0x5f]=71, [0x60]=72, [0x61]=73, [0x62]=82,
        [0x63]=83,
    };
    return (usage < 128) ? map[usage] : 0;
}

static int usb_key_in_report(uint8 key, uint8 *report)
{
    for (int i = 2; i < 8; i++) {
        if (report[i] == key) return 1;
    }
    return 0;
}

static int usb_keyboard_usage_is_pointer(uint8 usage)
{
    switch (usage) {
    case 0x54: case 0x55: case 0x56: case 0x57: case 0x58:
    case 0x59: case 0x5a: case 0x5b: case 0x5c:
    case 0x5d: case 0x5e: case 0x5f: case 0x60:
    case 0x61: case 0x62: case 0x63:
        return 1;
    default:
        return 0;
    }
}

#define USB_KBD_POINTER_RELEASE_GRACE_MS 160u
#define USB_KBD_POINTER_RAMP_MS 850u
#define USB_KBD_POINTER_BASE_SPEED 340
#define USB_KBD_POINTER_TOP_SPEED 1000

static uint8 usb_kbd_pointer_held[128];
static uint32 usb_kbd_pointer_release_at[128];

static int usb_time_reached(uint32 now, uint32 deadline)
{
    return deadline != 0 && ((uint32)(now - deadline) < 0x80000000u);
}

static void usb_keyboard_pointer_clear(void)
{
    memset(usb_kbd_pointer_held, 0, sizeof(usb_kbd_pointer_held));
    memset(usb_kbd_pointer_release_at, 0, sizeof(usb_kbd_pointer_release_at));
}

static void usb_keyboard_pointer_update_holds(uint32 now)
{
    static const uint8 pointer_usages[] = {
        0x54, 0x55, 0x56, 0x57, 0x58,
        0x59, 0x5a, 0x5b, 0x5c, 0x5d,
        0x5e, 0x5f, 0x60, 0x61, 0x62,
        0x63
    };

    for (int i = 0; i < (int)(sizeof(pointer_usages) / sizeof(pointer_usages[0])); i++) {
        uint8 usage = pointer_usages[i];
        if (usb_key_in_report(usage, usb_mouse.kbd_report)) {
            usb_kbd_pointer_held[usage] = 1;
            usb_kbd_pointer_release_at[usage] = 0;
        } else if (usb_kbd_pointer_held[usage] && usb_kbd_pointer_release_at[usage] == 0) {
            usb_kbd_pointer_release_at[usage] = now + USB_KBD_POINTER_RELEASE_GRACE_MS;
        }
    }
}

static int usb_keyboard_pointer_down(uint8 usage, uint32 now)
{
    if (usage >= 128) return 0;
    if (usb_time_reached(now, usb_kbd_pointer_release_at[usage])) {
        usb_kbd_pointer_held[usage] = 0;
        usb_kbd_pointer_release_at[usage] = 0;
    }
    return usb_kbd_pointer_held[usage] != 0;
}

static uint8 current_repeat_key = 0;
static uint32 next_repeat_ms = 0;
static int repeat_phase = 0;

static void usb_keyboard_apply_report(uint32 token, volatile uint32 *dbg)
{
    static const uint8 mod_code[8] = {29, 42, 56, 125, 97, 54, 100, 126};

    usb_cache_invalidate_range(usb_mouse.kbd_report, sizeof(usb_mouse.kbd_report));
    uint32 report0 = ((uint32)usb_mouse.kbd_report[3] << 24) | ((uint32)usb_mouse.kbd_report[2] << 16) |
                     ((uint32)usb_mouse.kbd_report[1] << 8) | usb_mouse.kbd_report[0];
    uint32 report1 = ((uint32)usb_mouse.kbd_report[7] << 24) | ((uint32)usb_mouse.kbd_report[6] << 16) |
                     ((uint32)usb_mouse.kbd_report[5] << 8) | usb_mouse.kbd_report[4];
    dbg[8] = token;
    dbg[9] = report0;
    dbg[10] = report1;
    dbg[11] = ((uint32)(usb_mouse.kbd_ep & 0xff) << 24) |
              ((uint32)(usb_mouse.kbd_mps & 0xff) << 16) |
              (uint32)(usb_mouse.poll_div & 0xff);
    dbg[12] = USB0_USBSTS;
    dbg[13] = USB0_PORTSC1;

    if (token & EHCI_QTD_HALTED) return;

    uint32 now = get_millisecond_timer();
    usb_keyboard_pointer_update_holds(now);

    uint8 mods = usb_mouse.kbd_report[0];
    uint8 prev_mods = (uint8)usb_mouse.last_modifiers;
    int changed = 0;
    for (int i = 0; i < 8; i++) {
        uint8 mask = (uint8)(1u << i);
        if ((mods & mask) != (prev_mods & mask)) {
            enqueue_input_event(0, 1, mod_code[i], (mods & mask) ? 1 : 0);
            changed = 1;
        }
    }
    usb_mouse.last_modifiers = mods;

    for (int i = 2; i < 8; i++) {
        uint8 key = usb_mouse.kbd_prev_report[i];
        if (key >= 4 && !usb_key_in_report(key, usb_mouse.kbd_report)) {
            int code = usb_hid_key_to_linux(key);
            if (code && !usb_keyboard_usage_is_pointer(key)) {
                enqueue_input_event(0, 1, (uint16)code, 0);
                changed = 1;
            }
        }
    }
    for (int i = 2; i < 8; i++) {
        uint8 key = usb_mouse.kbd_report[i];
        if (key >= 4 && !usb_key_in_report(key, usb_mouse.kbd_prev_report)) {
            int code = usb_hid_key_to_linux(key);
            if (code && !usb_keyboard_usage_is_pointer(key)) {
                enqueue_input_event(0, 1, (uint16)code, 1);
                changed = 1;
            }
            if (!usb_keyboard_usage_is_pointer(key)) {
                current_repeat_key = key;
                next_repeat_ms = get_millisecond_timer() + 400;
                repeat_phase = 1;
            }
        }
    }
    
    if (current_repeat_key && !usb_key_in_report(current_repeat_key, usb_mouse.kbd_report)) {
        current_repeat_key = 0;
        repeat_phase = 0;
    }
    
    memcpy(usb_mouse.kbd_prev_report, usb_mouse.kbd_report, sizeof(usb_mouse.kbd_prev_report));
    if (changed) gui_redraw_needed = 1;
}

static void usb_mouse_apply_report(uint32 token, volatile uint32 *dbg)
{
    usb_cache_invalidate_range(usb_mouse.report, sizeof(usb_mouse.report));
    uint32 report0 = ((uint32)usb_mouse.report[3] << 24) | ((uint32)usb_mouse.report[2] << 16) |
                     ((uint32)usb_mouse.report[1] << 8) | usb_mouse.report[0];
    uint32 report1 = ((uint32)usb_mouse.report[7] << 24) | ((uint32)usb_mouse.report[6] << 16) |
                     ((uint32)usb_mouse.report[5] << 8) | usb_mouse.report[4];
    dbg[8] = token;
    dbg[9] = report0;
    dbg[10] = report1;
    dbg[11] = ((uint32)(usb_mouse.intr_ep & 0xff) << 24) |
              ((uint32)(usb_mouse.intr_mps & 0xff) << 16) |
              (uint32)(usb_mouse.poll_div & 0xff);
    dbg[12] = USB0_USBSTS;
    dbg[13] = USB0_PORTSC1;

    if ((token & EHCI_QTD_HALTED) == 0) {
        int buttons = usb_mouse.report[0];
        int dx = (int)(signed char)usb_mouse.report[1];
        int dy = (int)(signed char)usb_mouse.report[2];
        int wheel = (int)(signed char)usb_mouse.report[3];
        if (usb_mouse.intr_mps >= 5 && usb_mouse.report[0] != 0 &&
            (usb_mouse.report[2] || usb_mouse.report[3] || usb_mouse.report[4] ||
             (usb_mouse.report[1] & 7))) {
            buttons = usb_mouse.report[1];
            dx = (int)(signed char)usb_mouse.report[2];
            dy = (int)(signed char)usb_mouse.report[3];
            wheel = (int)(signed char)usb_mouse.report[4];
            dbg[15] = 0x70100000u | (uint32)(token & 0xffffu);
        }
        if (dx || dy || wheel || (uint32)buttons != usb_mouse.last_buttons) {
            int old_left = gui_clicked;
            int old_right = gui_right_clicked;
            int buttons_changed = ((uint32)buttons != usb_mouse.last_buttons);
            gui_mx += dx;
            gui_my += dy;
            if (gui_mx < 0) gui_mx = 0;
            if (gui_my < 0) gui_my = 0;
            if (gui_mx >= WIDTH) gui_mx = WIDTH - 1;
            if (gui_my >= INPUT_VISIBLE_HEIGHT) gui_my = INPUT_VISIBLE_HEIGHT - 1;
            gui_clicked = (buttons & 1) ? 1 : 0;
            gui_right_clicked = (buttons & 2) ? 1 : 0;
            if (gui_clicked && !old_left) gui_click_pending = 1;
            if (gui_right_clicked && !old_right) gui_right_click_pending = 1;
            if (wheel) gui_wheel += wheel;
            if (buttons_changed || wheel) gui_redraw_needed = 1;
            usb_mouse.last_buttons = buttons;

            dbg[1] = (uint32)gui_mx;
            dbg[2] = (uint32)gui_my;
            dbg[3] = (uint32)buttons;
            dbg[4] = (uint32)wheel;
        }
    }

    dbg[14] = ((uint32)(gui_my & 0xffff) << 16) | (uint32)(gui_mx & 0xffff);
}

static void usb_keyboard_poll(void)
{
    struct ehci_qtd *q = &usb_mouse.kbd_qtd;
    volatile uint32 *dbg = (volatile uint32 *)FPGA_MOUSE_BASE;

    if (usb_mouse.kbd_pending) {
        usb_cache_invalidate_range(q, sizeof(*q));
        uint32 token = q->token;
        if (token & EHCI_QTD_ACTIVE) {
            usb_periodic_keepalive(&usb_mouse.kbd_qh);
            usb_mouse.kbd_wait_ticks++;
            dbg[8] = token;
            dbg[11] = ((uint32)(usb_mouse.kbd_ep & 0xff) << 24) |
                      ((uint32)(usb_mouse.kbd_mps & 0xff) << 16) |
                      (uint32)(usb_mouse.poll_div & 0xff);
            dbg[10] = (uint32)usb_mouse.kbd_wait_ticks;
            dbg[12] = USB0_USBSTS;
            dbg[13] = USB0_PORTSC1;
            if (USB_KBD_ACTIVE_TIMEOUT_POLLS &&
                usb_mouse.kbd_wait_ticks > USB_KBD_ACTIVE_TIMEOUT_POLLS) {
                dbg[15] = 0x76000000u | (uint32)(token & 0xffffu);
                usb_intr_endpoint_recover(&usb_mouse.kbd_qh);
                return;
            }
            dbg[15] = 0x75000000u | (uint32)(token & 0xffffu);
            return;
        }

        usb_mouse.kbd_pending = 0;
        usb_mouse.kbd_wait_ticks = 0;
        if (token & EHCI_QTD_HALTED) {
            if (++usb_mouse.kbd_halt_count >= 4) {
                dbg[15] = 0x77000000u | (uint32)(token & 0xffffu);
                usb_intr_endpoint_recover(&usb_mouse.kbd_qh);
                return;
            }
        } else {
            usb_mouse.kbd_halt_count = 0;
            usb_mouse.kbd_dt ^= 1;
        }
        usb_keyboard_apply_report(token, dbg);
        dbg[15] = 0x78000000u | (uint32)(token & 0xffffu);
    }

    memset(usb_mouse.kbd_report, 0, sizeof(usb_mouse.kbd_report));
    usb_cache_writeback_range(usb_mouse.kbd_report, sizeof(usb_mouse.kbd_report));
    usb_cache_invalidate_range(usb_mouse.kbd_report, sizeof(usb_mouse.kbd_report));
    usb_qtd_init(q, usb_mouse.kbd_report, usb_mouse.kbd_mps, EHCI_PID_IN, 0, 1);
    usb_mouse.kbd_qh.overlay.next = usb_phys(q);
    usb_mouse.kbd_qh.overlay.token = usb_mouse.kbd_dt ? 0x80000000u : 0;
    usb_mouse.kbd_qh.cur_qtd = 0;
    usb_cache_writeback_range(q, sizeof(*q));
    usb_cache_writeback_range(&usb_mouse.kbd_qh, sizeof(usb_mouse.kbd_qh));
    usb_cache_writeback_range(usb_mouse.periodic, sizeof(usb_mouse.periodic));
    USB0_USBCMD = USB0_USBCMD | 0x00000011u;
    usb_mouse.kbd_pending = 1;
    usb_mouse.kbd_wait_ticks = 0;
    dbg[8] = q->token;
    dbg[11] = ((uint32)(usb_mouse.kbd_ep & 0xff) << 24) |
              ((uint32)(usb_mouse.kbd_mps & 0xff) << 16) |
              (uint32)(usb_mouse.poll_div & 0xff);
    dbg[12] = USB0_USBSTS;
    dbg[13] = USB0_PORTSC1;
    dbg[15] = 0x79000000u | (uint32)(q->token & 0xffffu);
}

static void usb_mouse_init_hw(void)
{
    usb_mouse_ensure_state();
    if (usb_mouse.ready) return;
    if (usb_mouse.last_fail < 0) return;
    if (usb_mouse.last_fail > 0) {
        uint32 now = USB0_FRINDEX & 0x3fffu;
        if (((now - (uint32)usb_mouse.retry_after) & 0x3fffu) < 4000u) return;
        usb_mouse.last_fail = 0;
    }
    if (usb_mouse.last_fail == 1) {
        if ((USB0_PORTSC1 & 1) == 0) {
            if (++usb_mouse.retry_div < 64) return;
            usb_mouse.retry_div = 0;
        } else {
            usb_mouse.last_fail = 0;
        }
    }

    int retry_div = usb_mouse.retry_div;
    int retry_after = usb_mouse.retry_after;
    memset(&usb_mouse, 0, sizeof(usb_mouse));
    usb_keyboard_pointer_clear();
    usb_mouse.retry_div = retry_div;
    usb_mouse.retry_after = retry_after;
    usb_mouse.ep0_mps = 8;
    usb_mouse_mark(10);

    /*
     * The loader releases the external USB3320 PHY through Z7-LITE MIO46
     * (OTG_nRST). VibeOS still owns the USB bus state after boot: every fresh
     * HID enumeration must put the device back at address 0 before SET_ADDRESS.
     */
    if (!usb_host_controller_initialized) {
        if (usb_host_prepare() != 0) {
            lib_puts("[VIBE] usb hid: host prepare failed\n");
            usb_mouse.last_fail = 2;
            usb_mouse.retry_after = USB0_FRINDEX & 0x3fffu;
            return;
        }
        usb_host_controller_initialized = 1;
    } else if (usb_host_prepare() != 0) {
        lib_puts("[VIBE] usb mouse: host not ready\n");
        usb_mouse.last_fail = 2;
        usb_mouse.retry_after = USB0_FRINDEX & 0x3fffu;
        return;
    }

    if ((USB0_PORTSC1 & 1) == 0) {
        volatile uint32 *m = (volatile uint32 *)FPGA_MOUSE_BASE;
        m[8] = USB0_PORTSC1;
        m[9] = USB0_USBCMD;
        m[10] = USB0_USBSTS;
        m[11] = USB0_USBMODE;
        if (usb_mouse.last_fail != 1) lib_printf("[VIBE] usb mouse: no connect port=%x\n", USB0_PORTSC1);
        usb_mouse.last_fail = 1;
        usb_mouse.retry_after = USB0_FRINDEX & 0x3fffu;
        usb_mouse_mark(120);
        return;
    }
    usb_mouse_mark(20);
    usb_port_power_on();
    usb_mouse_mark(21);
    int did_port_reset = 0;
    if ((USB0_PORTSC1 & USB_PORTSC_PE) == 0) {
        if (usb_port_reset() != 0) {
            lib_printf("[VIBE] usb hid: port reset failed port=%x sts=%x\n", USB0_PORTSC1, USB0_USBSTS);
            usb_mouse.last_fail = 2;
            usb_mouse.retry_after = USB0_FRINDEX & 0x3fffu;
            usb_mouse_mark(121);
            return;
        }
        did_port_reset = 1;
    } else {
        volatile uint32 *m = (volatile uint32 *)FPGA_MOUSE_BASE;
        m[8] = USB0_PORTSC1;
        m[9] = USB0_USBCMD;
        m[10] = USB0_USBSTS;
        m[11] = USB0_USBMODE;
    }
    USB0_ASYNCLIST = 0;
    USB0_PERIODICLIST = 0;
    USB0_USBSTS = 0x0000003fu;
    usb_delay(20000);
    usb_clear_port_changes();
    usb_mouse_mark(22);
    /*
     * QH.EPS encoding matches the controller's PORTSC1.PSPD value on this
     * Zynq/ChipIdea host: 0=FS, 1=LS, 2=HS. Start with the real port speed
     * so interrupt endpoints use the same speed that enumeration used.
     */
    usb_mouse.speed = (USB0_PORTSC1 >> 26) & 3u;
    if (usb_mouse.speed > 2) usb_mouse.speed = 0;
    usb_mouse.tt_hub = 0;
    usb_mouse.tt_port = (usb_mouse.speed == 2) ? 0 : 1;
    usb_mouse.direct_async = 0;
    int port_speed = usb_mouse.speed;
    int desc8_ok = (usb_try_dev_desc8(port_speed, 0, (port_speed == 2) ? 0 : 1, 0, 123) == 0);
    if (!desc8_ok && !did_port_reset && (USB0_PORTSC1 & USB_PORTSC_CCS)) {
        usb_async_stop();
        usb_wait_ms(10);
        if (usb_port_reset() == 0) {
            did_port_reset = 1;
            usb_wait_ms(20);
            usb_clear_port_changes();
            usb_mouse.speed = (USB0_PORTSC1 >> 26) & 3u;
            if (usb_mouse.speed > 2) usb_mouse.speed = port_speed;
            port_speed = usb_mouse.speed;
            desc8_ok = (usb_try_dev_desc8(port_speed, 0, (port_speed == 2) ? 0 : 1, 0, 129) == 0);
        }
    }
    if (!desc8_ok && port_speed != 2) {
        int alt_speed = (port_speed == 1) ? 0 : 1;
        desc8_ok = (usb_try_dev_desc8(alt_speed, 0, 1, 0, 124) == 0);
    }
    if (!desc8_ok && port_speed != 2) {
        desc8_ok = (usb_try_dev_desc8(port_speed, 0, 0, 0, 125) == 0);
    }
    if (!desc8_ok && port_speed != 2) {
        int alt_speed = (port_speed == 1) ? 0 : 1;
        desc8_ok = (usb_try_dev_desc8(alt_speed, 0, 0, 0, 126) == 0);
    }
    if (!desc8_ok && (USB0_PORTSC1 & (USB_PORTSC_CCS | USB_PORTSC_PE)) !=
        (USB_PORTSC_CCS | USB_PORTSC_PE)) {
        usb_mouse.last_fail = 1;
        usb_mouse.retry_after = USB0_FRINDEX & 0x3fffu;
        usb_mouse_mark(122);
        return;
    }
    if (!desc8_ok) {
        lib_printf("[VIBE] usb mouse: dev desc8 failed port=%x sts=%x speed=%d\n", USB0_PORTSC1, USB0_USBSTS, usb_mouse.speed);
        usb_host_controller_initialized = 0;
        usb_mouse.last_fail = 2;
        usb_mouse.retry_after = USB0_FRINDEX & 0x3fffu;
        usb_mouse_mark(130);
        return;
    }
    usb_mouse_mark(30);
    usb_mouse.ep0_mps = usb_mouse.data[7] ? usb_mouse.data[7] : 8;
    if (usb_mouse.ep0_mps > 64) usb_mouse.ep0_mps = 64;
    if (did_port_reset) {
        /*
         * Full enumeration after a fresh bus reset.  Do not reset again when
         * the loader already left the port enabled; that was making some
         * keyboards drop back to no-connect and blink their lock LEDs.
         */
        usb_async_stop();
        usb_wait_ms(10);
        usb_clear_port_changes();
        usb_mouse.speed = (USB0_PORTSC1 >> 26) & 3u;
        if (usb_mouse.speed > 2) usb_mouse.speed = port_speed;
        usb_mouse.tt_port = (usb_mouse.speed == 2) ? 0 : 1;
    }
    usb_mouse_mark(40);
    if (usb_control(0, 0x00, 5, 1, 0, 0, 0) != 0) {
        lib_puts("[VIBE] usb mouse: set address failed\n");
        usb_host_controller_initialized = 0;
        usb_mouse.last_fail = 2;
        usb_mouse.retry_after = USB0_FRINDEX & 0x3fffu;
        return;
    }
    usb_mouse_mark(50);
    usb_wait_ms(20);
    usb_mouse.addr = 1;
    if (usb_control(1, 0x80, 6, 0x0200, 0, usb_mouse.data, 9) != 0) {
        lib_puts("[VIBE] usb mouse: config head failed\n");
        usb_host_controller_initialized = 0;
        usb_mouse.last_fail = 2;
        usb_mouse.retry_after = USB0_FRINDEX & 0x3fffu;
        return;
    }
    usb_mouse_mark(60);
    usb_cache_invalidate_range(usb_mouse.data, 64);
    int total = usb_mouse.data[2] | (usb_mouse.data[3] << 8);
    if (total < 9) total = 34;
    if (total > (int)sizeof(usb_mouse.data)) total = sizeof(usb_mouse.data);
    if (usb_control(1, 0x80, 6, 0x0200, 0, usb_mouse.data, total) != 0) {
        lib_printf("[VIBE] usb mouse: config full failed len=%d\n", total);
        usb_host_controller_initialized = 0;
        usb_mouse.last_fail = 2;
        usb_mouse.retry_after = USB0_FRINDEX & 0x3fffu;
        return;
    }
    usb_mouse_mark(70);
    usb_cache_invalidate_range(usb_mouse.data, 64);
    volatile uint32 *dbg = (volatile uint32 *)FPGA_MOUSE_BASE;
    dbg[8] = ((uint32)usb_mouse.data[3] << 24) | ((uint32)usb_mouse.data[2] << 16) |
             ((uint32)usb_mouse.data[1] << 8) | usb_mouse.data[0];
    dbg[9] = ((uint32)usb_mouse.data[7] << 24) | ((uint32)usb_mouse.data[6] << 16) |
             ((uint32)usb_mouse.data[5] << 8) | usb_mouse.data[4];
    dbg[10] = ((uint32)usb_mouse.data[11] << 24) | ((uint32)usb_mouse.data[10] << 16) |
              ((uint32)usb_mouse.data[9] << 8) | usb_mouse.data[8];
    dbg[11] = ((uint32)usb_mouse.data[15] << 24) | ((uint32)usb_mouse.data[14] << 16) |
              ((uint32)usb_mouse.data[13] << 8) | usb_mouse.data[12];
    dbg[12] = (uint32)total;
    int iface = usb_hid_parse_config(usb_mouse.data, total);
    if (iface < 0 || (usb_mouse.intr_ep == 0 && usb_mouse.kbd_ep == 0)) {
        for (int i = 0; i + 2 <= total && usb_mouse.data[i] >= 2; i += usb_mouse.data[i]) {
            if (i + usb_mouse.data[i] > total) break;
            if (usb_mouse.data[i + 1] == 5 && usb_mouse.data[i] >= 7 &&
                (usb_mouse.data[i + 2] & 0x80) && ((usb_mouse.data[i + 3] & 3) == 3)) {
                iface = 0;
                int ep = usb_mouse.data[i + 2] & 0x0f;
                int mps = usb_mouse.data[i + 4] | (usb_mouse.data[i + 5] << 8);
                if (mps >= 7) {
                    usb_mouse.kbd_iface = iface;
                    usb_mouse.kbd_ep = ep;
                    usb_mouse.kbd_mps = mps;
                    if (usb_mouse.kbd_mps > (int)sizeof(usb_mouse.kbd_report))
                        usb_mouse.kbd_mps = sizeof(usb_mouse.kbd_report);
                } else {
                    usb_mouse.iface = iface;
                    usb_mouse.intr_ep = ep;
                    usb_mouse.intr_mps = mps;
                    if (usb_mouse.intr_mps > (int)sizeof(usb_mouse.report))
                        usb_mouse.intr_mps = sizeof(usb_mouse.report);
                }
                usb_mouse_mark(78);
                break;
            }
        }
        if (iface < 0 || (usb_mouse.intr_ep == 0 && usb_mouse.kbd_ep == 0)) {
            lib_printf("[VIBE] usb hid: using fallback HID EP1 IN len=%d cls=%x/%x/%x\n",
                       total, usb_mouse.data[14], usb_mouse.data[15], usb_mouse.data[16]);
            iface = 0;
            usb_mouse.kbd_iface = iface;
            usb_mouse.kbd_ep = 1;
            usb_mouse.kbd_mps = 7;
            usb_mouse_mark(79);
        }
    }
    usb_mouse_mark(80);
    int cfg = usb_mouse.data[5];
    if (usb_control(1, 0x00, 9, cfg, 0, 0, 0) != 0) {
        lib_puts("[VIBE] usb hid: set config failed\n");
        usb_host_controller_initialized = 0;
        usb_mouse.last_fail = 2;
        usb_mouse.retry_after = USB0_FRINDEX & 0x3fffu;
        return;
    }
    usb_mouse_mark(90);
    if (usb_mouse.intr_ep) {
        usb_control(1, 0x21, 11, 0, usb_mouse.iface, 0, 0); /* boot protocol */
        usb_control(1, 0x21, 10, 0, usb_mouse.iface, 0, 0); /* idle */
        usb_intr_qh_init(&usb_mouse.intr_qh, 1, usb_mouse.intr_ep, usb_mouse.intr_mps,
                         usb_mouse.speed);
        usb_mouse.mouse_ready = 1;
    }
    if (usb_mouse.kbd_ep) {
        usb_control(1, 0x21, 11, 0, usb_mouse.kbd_iface, 0, 0); /* boot protocol */
        usb_control(1, 0x21, 10, 0, usb_mouse.kbd_iface, 0, 0); /* idle */
        usb_intr_qh_init(&usb_mouse.kbd_qh, 1, usb_mouse.kbd_ep, usb_mouse.kbd_mps,
                         usb_mouse.speed);
        usb_mouse.kbd_ready = 1;
    }

    if (usb_mouse.kbd_ready) {
        usb_periodic_start(&usb_mouse.kbd_qh);
    } else if (usb_mouse.mouse_ready) {
        usb_periodic_start(&usb_mouse.intr_qh);
    }
    usb_mouse.ready = usb_mouse.mouse_ready || usb_mouse.kbd_ready;
    usb_mouse_mark(usb_mouse.kbd_ready && !usb_mouse.mouse_ready ? 101 : 100);
}

static void usb_host_kick_power(void)
{
    if (usb_host_power_kicked) return;
    USB0_USBMODE = USBMODE_HOST;
    USB0_CONFIGFLAG = 0x00000001u;
    USB0_SBUSCFG = 0x00000007u;
    USB0_CTRLDSSEG = 0x00000000u;
    USB0_BURSTSIZE = 0x00001010u;
    usb_port_power_on();
    USB0_USBSTS = 0x0000003fu;
    USB0_USBCMD = USB0_USBCMD | 0x00080001u;
    usb_host_power_kicked = 1;
}

static int usb_ulpi_wait_idle(uint32 guard)
{
    while (guard-- > 0) {
        uint32 v = USB0_ULPI_VIEW;
        if ((v & 0xc0000000u) == 0) return 0;
    }
    return -1;
}

static int usb_ulpi_wait_view_clear(uint32 mask, uint32 guard)
{
    while (guard-- > 0) {
        uint32 v = USB0_ULPI_VIEW;
        if ((v & mask) == 0) return 0;
    }
    return -1;
}

static void usb_ulpi_write_host_reg(uint8 reg, uint8 val)
{
    USB0_ULPI_VIEW = 0xa0000000u;
    usb_ulpi_wait_view_clear(0x80000000u, 200000u);
    USB0_ULPI_VIEW = 0x60000000u | ((uint32)reg << 16) | val;
    usb_ulpi_wait_view_clear(0x40000000u, 200000u);
}

static void usb_ulpi_write_reg(uint8 reg, uint8 val)
{
    usb_ulpi_write_host_reg(reg, val);
}

static void usb_ulpi_configure_host_phy(void)
{
    /*
     * Keep the external USB3320 in normal host mode.  Some keyboards light
     * LEDs from VBUS even when the PHY is still not driving packet traffic.
     */
    usb_ulpi_write_reg(0x0a, 0x86u);
    usb_ulpi_write_reg(0x04, 0x41u);
    usb_ulpi_write_reg(0x07, 0x00u);
    usb_ulpi_write_reg(0x0b, 0x60u);
}

static void usb_phy_recover_from_os(void)
{
    volatile uint32 *m = (volatile uint32 *)FPGA_MOUSE_BASE;
    usb_mouse_mark(117);

    memset(&usb_mouse, 0, sizeof(usb_mouse));
    usb_keyboard_pointer_clear();
    usb_mouse_state_cleared = 1;
    usb_mouse.last_fail = 1;
    usb_last_port_connected = 0;

    USB0_USBCMD = USB0_USBCMD & ~0x00000030u;
    USB0_ASYNCLIST = 0;
    USB0_PERIODICLIST = 0;
    USB0_USBSTS = 0x0000003fu;
    USB0_USBCMD = 0x00080000u;

    GPIO_DIRM1 = GPIO_DIRM1 | USB_PHY_RESET_MIO46_BIT;
    GPIO_OEN1 = GPIO_OEN1 | USB_PHY_RESET_MIO46_BIT;
    GPIO_DATA1 = GPIO_DATA1 & ~USB_PHY_RESET_MIO46_BIT;
    usb_delay(100000);
    GPIO_DATA1 = GPIO_DATA1 | USB_PHY_RESET_MIO46_BIT;
    usb_delay(500000);

    USB0_USBCMD = 0x00080002u;
    for (int i = 0; i < 100000; i++) {
        if ((USB0_USBCMD & 0x00000002u) == 0) break;
    }
    USB0_USBMODE = USBMODE_HOST;
    USB0_CONFIGFLAG = 0x00000001u;
    USB0_SBUSCFG = 0x00000007u;
    USB0_CTRLDSSEG = 0x00000000u;
    USB0_TTCTRL = 0x00000000u;
    USB0_BURSTSIZE = 0x00001010u;
    USB0_ASYNCLIST = 0;
    USB0_PERIODICLIST = 0;
    USB0_OTGSC = (USB0_OTGSC & ~0x007f0001u) | 0x00000026u;
    usb_ulpi_configure_host_phy();
    usb_port_power_on();
    USB0_USBSTS = 0x0000003fu;
    USB0_USBCMD = 0x00080001u;

    usb_host_power_kicked = 1;
    usb_host_controller_initialized = 0;
    usb_enum_poll_count = 0;
    usb_enum_next_poll = USB_ENUM_START_DELAY_POLLS;
    usb_enum_retry_after_ms = 0;
    usb_connect_stable_polls = 0;
    usb_no_connect_polls = 0;
    m[8] = USB0_PORTSC1;
    m[9] = USB0_USBCMD;
    m[10] = USB0_USBSTS;
    m[11] = USB0_USBMODE;
}

static void usb_hid_note_disconnected(uint32 port)
{
    volatile uint32 *m = (volatile uint32 *)FPGA_MOUSE_BASE;

    if (++usb_no_connect_polls < USB_NO_CONNECT_STABLE_POLLS) {
        m[8] = port;
        m[9] = USB0_USBCMD;
        m[10] = USB0_USBSTS;
        m[11] = USB0_USBMODE;
        usb_mouse_mark(116);
        return;
    }

    uint32 now = get_millisecond_timer();
    if (usb_phy_recover_after_ms && now < usb_phy_recover_after_ms) {
        m[8] = port;
        m[9] = USB0_USBCMD;
        m[10] = USB0_USBSTS;
        m[11] = USB0_USBMODE;
        usb_mouse_mark(118);
        return;
    }

    usb_phy_recover_after_ms = now + USB_PHY_RECOVER_RETRY_MS;
    usb_phy_recover_from_os();
    m[8] = USB0_PORTSC1;
    m[9] = USB0_USBCMD;
    m[10] = USB0_USBSTS;
    m[11] = USB0_USBMODE;
}

static void usb_mouse_poll_inner(void)
{
    usb_mouse_ensure_state();
    usb_host_kick_power();

    uint32 port = USB0_PORTSC1;
    if ((port & USB_PORTSC_CCS) == 0) {
        if (usb_mouse.ready || usb_mouse.mouse_ready || usb_mouse.kbd_ready) {
            usb_keyboard_pointer_clear();
        }
        usb_hid_note_disconnected(port);
        return;
    }
    usb_no_connect_polls = 0;

    if (!usb_last_port_connected) {
        usb_last_port_connected = 1;
        usb_connect_stable_polls = 0;
        usb_enum_poll_count = 0;
        usb_enum_next_poll = USB_ENUM_START_DELAY_POLLS;
        usb_enum_retry_after_ms = get_millisecond_timer() + USB_ENUM_POWER_SETTLE_MS;
        usb_mouse.ready = 0;
        usb_mouse.mouse_ready = 0;
        usb_mouse.kbd_ready = 0;
        usb_mouse.last_fail = 0;
        usb_mouse_mark(119);
        return;
    }

    if (!usb_mouse.ready && usb_connect_stable_polls < USB_ENUM_STABLE_POLLS) {
        usb_connect_stable_polls++;
        return;
    }

    if (!usb_mouse.ready) {
        usb_enum_poll_count++;
        if (usb_enum_poll_count < USB_ENUM_START_DELAY_POLLS) return;
        uint32 now_ms = get_millisecond_timer();
        if (usb_enum_retry_after_ms && now_ms < usb_enum_retry_after_ms) return;
        if (usb_enum_poll_count < usb_enum_next_poll) return;
        usb_mouse_init_hw();
        if (!usb_mouse.ready) {
            usb_enum_next_poll = usb_enum_poll_count + USB_ENUM_RETRY_POLLS;
            usb_enum_retry_after_ms = now_ms + USB_ENUM_RETRY_MS;
        }
        return;
    }

    usb_mouse.poll_div++;
    if (usb_mouse.kbd_ready) {
        usb_keyboard_poll();

        uint32 now = get_millisecond_timer();
        if (current_repeat_key && repeat_phase && !usb_keyboard_usage_is_pointer(current_repeat_key)) {
            if (now >= next_repeat_ms) {
                next_repeat_ms = now + 50;
                int code = usb_hid_key_to_linux(current_repeat_key);
                if (code) {
                    enqueue_input_event(0, 1, (uint16)code, 1);
                    enqueue_input_event(0, 1, (uint16)code, 0);
                }
            }
        }

        static uint32 last_kb_mouse_ms = 0;
        static uint32 kb_move_ms = 0;
        static int kb_frac_x = 0;
        static int kb_frac_y = 0;
        static int last_kb_dir_x = 0;
        static int last_kb_dir_y = 0;
        static uint32 last_kb_dir_ms = 0;
        static uint32 last_kb_active_ms = 0;
        static uint32 last_kb_wheel_ms = 0;
        if (now - last_kb_mouse_ms >= 4) {
            uint32 elapsed = last_kb_mouse_ms ? (now - last_kb_mouse_ms) : 4u;
            last_kb_mouse_ms = now;
            if (elapsed == 0) elapsed = 1;
            if (elapsed > 16) elapsed = 16;

            int move_x = 0, move_y = 0;
            if (usb_keyboard_pointer_down(0x5c, now) ||
                usb_keyboard_pointer_down(0x59, now) ||
                usb_keyboard_pointer_down(0x5f, now)) move_x -= 1;
            if (usb_keyboard_pointer_down(0x5e, now) ||
                usb_keyboard_pointer_down(0x5b, now) ||
                usb_keyboard_pointer_down(0x61, now)) move_x += 1;
            if (usb_keyboard_pointer_down(0x60, now) ||
                usb_keyboard_pointer_down(0x5f, now) ||
                usb_keyboard_pointer_down(0x61, now)) move_y -= 1;
            if (usb_keyboard_pointer_down(0x5a, now) ||
                usb_keyboard_pointer_down(0x59, now) ||
                usb_keyboard_pointer_down(0x5b, now)) move_y += 1;

            if (move_x || move_y) {
                last_kb_dir_x = move_x;
                last_kb_dir_y = move_y;
                last_kb_dir_ms = now;
            } else if ((last_kb_dir_x || last_kb_dir_y) &&
                       last_kb_dir_ms != 0 &&
                       (uint32)(now - last_kb_dir_ms) <= 48u) {
                move_x = last_kb_dir_x;
                move_y = last_kb_dir_y;
            }

            int dx = 0;
            int dy = 0;
            int speed = 0;
            if (move_x || move_y) {
                if (last_kb_active_ms == 0 ||
                    (uint32)(now - last_kb_active_ms) > 300u) {
                    kb_move_ms = 0;
                    kb_frac_x = 0;
                    kb_frac_y = 0;
                }
                last_kb_active_ms = now;
                kb_move_ms += elapsed;
                if (kb_move_ms > USB_KBD_POINTER_RAMP_MS)
                    kb_move_ms = USB_KBD_POINTER_RAMP_MS;
                speed = USB_KBD_POINTER_BASE_SPEED +
                        (int)((kb_move_ms *
                               (USB_KBD_POINTER_TOP_SPEED - USB_KBD_POINTER_BASE_SPEED)) /
                              USB_KBD_POINTER_RAMP_MS);
                if (usb_mouse.last_modifiers & 0x22) speed = 1250;
                if (move_x && move_y) speed = (speed * 7) / 10;
                int delta = speed * (int)elapsed;
                kb_frac_x += move_x * delta;
                kb_frac_y += move_y * delta;
                dx = kb_frac_x / 1000;
                dy = kb_frac_y / 1000;
                kb_frac_x -= dx * 1000;
                kb_frac_y -= dy * 1000;
            } else {
                if (last_kb_active_ms == 0 ||
                    (uint32)(now - last_kb_active_ms) > 300u) {
                    kb_move_ms = 0;
                    kb_frac_x = 0;
                    kb_frac_y = 0;
                    last_kb_dir_x = 0;
                    last_kb_dir_y = 0;
                }
            }

            int kb_click = usb_keyboard_pointer_down(0x54, now);
            int kb_right = usb_keyboard_pointer_down(0x55, now);
            int kb_wheel = 0;
            if (usb_keyboard_pointer_down(0x56, now)) kb_wheel = 1;
            int old_left = gui_clicked;
            int old_right = gui_right_clicked;
            if (dx || dy || kb_click != old_left || kb_right != old_right) {
                gui_clicked = kb_click;
                gui_right_clicked = kb_right;
                gui_mx += dx;
                if (gui_mx < 0) gui_mx = 0;
                if (gui_mx >= WIDTH) gui_mx = WIDTH - 1;
                gui_my += dy;
                if (gui_my < 0) gui_my = 0;
                if (gui_my >= INPUT_VISIBLE_HEIGHT) gui_my = INPUT_VISIBLE_HEIGHT - 1;
                if (gui_clicked && !old_left) gui_click_pending = 1;
                if (gui_right_clicked && !old_right) gui_right_click_pending = 1;
                if (kb_click != old_left || kb_right != old_right) gui_redraw_needed = 1;
            }
            {
                volatile uint32 *dbg = (volatile uint32 *)FPGA_MOUSE_BASE;
                uint32 dir_dbg = ((uint32)(move_x + 1) & 3u) |
                                 (((uint32)(move_y + 1) & 3u) << 2);
                dbg[6] = 0x4b000000u |
                         ((uint32)(speed & 0x0fffu) << 12) |
                         ((uint32)(kb_move_ms & 0x03ffu) << 2) |
                         dir_dbg;
            }
            if (kb_wheel && (last_kb_wheel_ms == 0 || (uint32)(now - last_kb_wheel_ms) >= 90u)) {
                gui_wheel += kb_wheel;
                gui_redraw_needed = 1;
                last_kb_wheel_ms = now;
            }
        }
        return;
    }

    struct ehci_qtd *q = &usb_mouse.qtd[3];
    volatile uint32 *dbg = (volatile uint32 *)FPGA_MOUSE_BASE;

    if (usb_mouse.intr_pending) {
        usb_cache_invalidate_range(q, sizeof(*q));
        uint32 token = q->token;
        if (token & EHCI_QTD_ACTIVE) {
            usb_periodic_keepalive(&usb_mouse.intr_qh);
            usb_mouse.intr_wait_ticks++;
            dbg[8] = token;
            dbg[11] = ((uint32)(usb_mouse.intr_ep & 0xff) << 24) |
                      ((uint32)(usb_mouse.intr_mps & 0xff) << 16) |
                      (uint32)(usb_mouse.poll_div & 0xff);
            dbg[12] = USB0_USBSTS;
            dbg[13] = USB0_PORTSC1;
            dbg[14] = ((uint32)(gui_my & 0xffff) << 16) | (uint32)(gui_mx & 0xffff);
            if (usb_mouse.intr_wait_ticks > USB_INTR_ACTIVE_TIMEOUT_POLLS) {
                dbg[15] = 0x74000000u | (uint32)(token & 0xffffu);
                usb_intr_endpoint_recover(&usb_mouse.intr_qh);
                return;
            }
            dbg[15] = 0x72000000u | (uint32)(token & 0xffffu);
            return;
        }

        usb_mouse.intr_pending = 0;
        usb_mouse.intr_wait_ticks = 0;
        if (token & EHCI_QTD_HALTED) {
            if (++usb_mouse.intr_halt_count >= 4) {
                dbg[15] = 0x73000000u | (uint32)(token & 0xffffu);
                usb_intr_endpoint_recover(&usb_mouse.intr_qh);
                return;
            }
        } else {
            usb_mouse.intr_halt_count = 0;
            usb_mouse.intr_dt ^= 1;
        }
        usb_mouse_apply_report(token, dbg);
        dbg[15] = 0x70000000u | (uint32)(token & 0xffffu);
    }

    memset(usb_mouse.report, 0, sizeof(usb_mouse.report));
    usb_cache_writeback_range(usb_mouse.report, sizeof(usb_mouse.report));
    usb_cache_invalidate_range(usb_mouse.report, sizeof(usb_mouse.report));
    usb_qtd_init(q, usb_mouse.report, usb_mouse.intr_mps, EHCI_PID_IN, 0, 1);
    usb_mouse.intr_qh.overlay.next = usb_phys(q);
    usb_mouse.intr_qh.overlay.token = usb_mouse.intr_dt ? 0x80000000u : 0;
    usb_mouse.intr_qh.cur_qtd = 0;
    usb_cache_writeback_range(q, sizeof(*q));
    usb_cache_writeback_range(&usb_mouse.intr_qh, sizeof(usb_mouse.intr_qh));
    usb_cache_writeback_range(usb_mouse.periodic, sizeof(usb_mouse.periodic));
    USB0_USBCMD = USB0_USBCMD | 0x00000011u;
    usb_mouse.intr_pending = 1;
    usb_mouse.intr_wait_ticks = 0;
    dbg[8] = q->token;
    dbg[11] = ((uint32)(usb_mouse.intr_ep & 0xff) << 24) |
              ((uint32)(usb_mouse.intr_mps & 0xff) << 16) |
              (uint32)(usb_mouse.poll_div & 0xff);
    dbg[12] = USB0_USBSTS;
    dbg[13] = USB0_PORTSC1;
    dbg[14] = ((uint32)(gui_my & 0xffff) << 16) | (uint32)(gui_mx & 0xffff);
    dbg[15] = 0x71000000u | (uint32)(q->token & 0xffffu);
}

static void usb_mouse_poll(void)
{
    if (usb_poll_busy) return;
    usb_poll_busy = 1;
    usb_mouse_poll_inner();
    usb_poll_busy = 0;
}

int virtio_hid_ready(void)
{
    usb_mouse_poll();
    return usb_mouse.ready && (usb_mouse.mouse_ready || usb_mouse.kbd_ready);
}

static void fpga_mouse_poll(void)
{
    usb_mouse_poll();
    if (usb_mouse.ready) return;

    volatile uint32 *m = (volatile uint32 *)FPGA_MOUSE_BASE;
    static uint32 last_seq = 0xffffffffu;
    uint32 magic = m[0];
    if (magic != 0x4d4f5553u) return;

    uint32 xy = m[6];
    uint32 state = m[7];
    uint32 seq = state & 0xffffu;
    int buttons = (int)((state >> 24) & 0xffu);
    int wheel = (int)(signed char)((state >> 16) & 0xffu);
    int old_left = gui_clicked;
    int old_right = gui_right_clicked;

    gui_mx = (int)(xy & 0xffffu);
    gui_my = (int)((xy >> 16) & 0xffffu);
    if (gui_mx < 0) gui_mx = 0;
    if (gui_my < 0) gui_my = 0;
    if (gui_mx >= WIDTH) gui_mx = WIDTH - 1;
    if (gui_my >= INPUT_VISIBLE_HEIGHT) gui_my = INPUT_VISIBLE_HEIGHT - 1;

    gui_clicked = (buttons & 1) ? 1 : 0;
    gui_right_clicked = (buttons & 2) ? 1 : 0;
    if (gui_clicked && !old_left) gui_click_pending = 1;
    if (gui_right_clicked && !old_right) gui_right_click_pending = 1;
    if (seq != last_seq && wheel != 0) gui_wheel += wheel;
    if (seq != last_seq) {
        if (wheel || gui_clicked != old_left || gui_right_clicked != old_right) {
            gui_redraw_needed = 1;
        }
        last_seq = seq;
    }
}

static int fpga_keyboard_mouse_dir_x = 0;
static int fpga_keyboard_mouse_dir_y = 0;
static int fpga_keyboard_mouse_fast = 0;
static int fpga_keyboard_mouse_div = 0;

static void fpga_keyboard_mouse_set_axis(int code, int down)
{
    int x = 0;
    int y = 0;

    switch (code) {
    case 75: case 71: case 79: x = -1; break;
    case 77: case 73: case 81: x = 1; break;
    default: break;
    }
    switch (code) {
    case 72: case 71: case 73: y = -1; break;
    case 80: case 79: case 81: y = 1; break;
    default: break;
    }

    if (x < 0) {
        if (down) fpga_keyboard_mouse_dir_x = -1;
        else if (fpga_keyboard_mouse_dir_x < 0) fpga_keyboard_mouse_dir_x = 0;
    } else if (x > 0) {
        if (down) fpga_keyboard_mouse_dir_x = 1;
        else if (fpga_keyboard_mouse_dir_x > 0) fpga_keyboard_mouse_dir_x = 0;
    }

    if (y < 0) {
        if (down) fpga_keyboard_mouse_dir_y = -1;
        else if (fpga_keyboard_mouse_dir_y < 0) fpga_keyboard_mouse_dir_y = 0;
    } else if (y > 0) {
        if (down) fpga_keyboard_mouse_dir_y = 1;
        else if (fpga_keyboard_mouse_dir_y > 0) fpga_keyboard_mouse_dir_y = 0;
    }
}

static int fpga_keyboard_mouse_handle_key(int code, int down)
{
    switch (code) {
    case 42:
    case 54:
        fpga_keyboard_mouse_fast = down;
        return 0;
    case 71: case 72: case 73: case 75: case 77: case 79: case 80: case 81:
        fpga_keyboard_mouse_set_axis(code, down);
        return 1;
    case 76:
    case 96:
        gui_clicked = down ? 1 : 0;
        if (down) gui_click_pending = 1;
        gui_redraw_needed = 1;
        return 1;
    case 78:
    case 83:
        gui_right_clicked = down ? 1 : 0;
        if (down) gui_right_click_pending = 1;
        gui_redraw_needed = 1;
        return 1;
    case 55:
        if (down) {
            gui_wheel = -1;
            gui_redraw_needed = 1;
        }
        return 1;
    case 74:
        if (down) {
            gui_wheel = 1;
            gui_redraw_needed = 1;
        }
        return 1;
    default:
        return 0;
    }
}

static void fpga_keyboard_mouse_tick(void)
{
    if (fpga_keyboard_mouse_dir_x == 0 && fpga_keyboard_mouse_dir_y == 0) {
        fpga_keyboard_mouse_div = 0;
        return;
    }
    if (++fpga_keyboard_mouse_div < 2) return;
    fpga_keyboard_mouse_div = 0;

    int step = fpga_keyboard_mouse_fast ? 12 : 6;
    gui_mx += fpga_keyboard_mouse_dir_x * step;
    gui_my += fpga_keyboard_mouse_dir_y * step;
    if (gui_mx < 0) gui_mx = 0;
    if (gui_my < 0) gui_my = 0;
    if (gui_mx >= WIDTH) gui_mx = WIDTH - 1;
    if (gui_my >= INPUT_VISIBLE_HEIGHT) gui_my = INPUT_VISIBLE_HEIGHT - 1;
}

void virtio_mouse_poll_task(void)
{
    for (;;) {
        usb_mouse_poll();
        virtio_input_poll();
        task_os();
    }
}
#endif

struct raw_input_event {
    uint16 device;
    uint16 type;
    uint16 code;
    uint32 value;
};

static volatile uint16 input_evt_head = 0;
static volatile uint16 input_evt_tail = 0;
static volatile int input_poll_busy = 0;
static struct raw_input_event input_events[INPUT_EVENT_QUEUE_SIZE];

static void enqueue_input_event(uint16 device, uint16 type, uint16 code, uint32 value) {
    uint16 next = (input_evt_tail + 1) % INPUT_EVENT_QUEUE_SIZE;
    if (next == input_evt_head) {
        input_evt_head = (input_evt_head + 1) % INPUT_EVENT_QUEUE_SIZE;
    }
    input_events[input_evt_tail].device = device;
    input_events[input_evt_tail].type = type;
    input_events[input_evt_tail].code = code;
    input_events[input_evt_tail].value = value;
    input_evt_tail = next;
}

struct input_avail {
    uint16 flags; uint16 idx; uint16 ring[INPUT_QUEUE_SIZE];
};
struct input_used_elem { uint32 id; uint32 len; };
struct input_used {
    uint16 flags; uint16 idx; struct input_used_elem ring[INPUT_QUEUE_SIZE];
};

struct keyboard {
    char pages[2 * PGSIZE];
    virtq_desc_t *desc;
    struct input_avail *avail;
    struct input_used *used;
    struct { uint16 type, code; uint32 value; } events[INPUT_QUEUE_SIZE];
    uint16 queue_size;
    uint16 used_idx;
} __attribute__((aligned(4096))) keyboard;

void virtio_keyboard_init() {
    volatile uint32 *r = (uint32*)VIRTIO_KBD_BASE; if(*r != 0x74726976) return;
    uint16 qsize = (uint16)r[VIRTIO_MMIO_QUEUE_NUM_MAX / 4];
    if (qsize == 0) return;
    if (qsize > INPUT_QUEUE_SIZE) qsize = INPUT_QUEUE_SIZE;
    keyboard.queue_size = qsize;
    memset(keyboard.pages, 0, sizeof(keyboard.pages));
    r[28]=0x0F; r[10]=PGSIZE; r[14]=qsize; r[16]=((uint32)keyboard.pages)>>12;
    keyboard.desc=(virtq_desc_t*)keyboard.pages;
    keyboard.avail=(struct input_avail*)(keyboard.pages + INPUT_QUEUE_SIZE * sizeof(virtq_desc_t));
    keyboard.used=(struct input_used*)(keyboard.pages+PGSIZE);
    for(int i=0;i<qsize;i++){ keyboard.desc[i].addr=(uint64)(uint32)&keyboard.events[i]; keyboard.desc[i].len=8; keyboard.desc[i].flags=2; keyboard.avail->ring[i]=i; }
    keyboard.avail->idx=qsize; r[28]|=4; if (os_debug) lib_puts("KBD Ready\n");
}

void virtio_keyboard_isr() {
    if(!keyboard.used || keyboard.queue_size == 0) return;
    uint16 cur = keyboard.used->idx;
    while(keyboard.used_idx != cur) {
        int id = keyboard.used->ring[keyboard.used_idx % keyboard.queue_size].id;
        if (id < 0 || id >= keyboard.queue_size) break;
        enqueue_input_event(0, keyboard.events[id].type, keyboard.events[id].code, keyboard.events[id].value);
        keyboard.avail->ring[keyboard.avail->idx % keyboard.queue_size]=id; keyboard.avail->idx++; keyboard.used_idx++;
    }
    *(volatile uint32*)(VIRTIO_KBD_BASE + 0x64) = *(volatile uint32*)(VIRTIO_KBD_BASE + 0x60) & 3;
}

struct mouse {
    char pages[2 * PGSIZE];
    virtq_desc_t *desc;
    struct input_avail *avail;
    struct input_used *used;
    struct { uint16 type, code; uint32 value; } events[INPUT_QUEUE_SIZE];
    uint16 queue_size;
    uint16 used_idx;
} __attribute__((aligned(4096))) mouse;
void virtio_mouse_init() {
#ifdef FPGA_MINIMAL
    usb_mouse_ensure_state();
    usb_mouse_mark(9);
    return;
#endif
    volatile uint32 *r = (uint32*)VIRTIO_MOUSE_BASE; if(*r != 0x74726976) return;
    uint16 qsize = (uint16)r[VIRTIO_MMIO_QUEUE_NUM_MAX / 4];
    if (qsize == 0) return;
    if (qsize > INPUT_QUEUE_SIZE) qsize = INPUT_QUEUE_SIZE;
    mouse.queue_size = qsize;
    memset(mouse.pages, 0, sizeof(mouse.pages));
    r[28]=0x0F; r[10]=PGSIZE; r[14]=qsize; r[16]=((uint32)mouse.pages)>>12;
    mouse.desc=(virtq_desc_t*)mouse.pages;
    mouse.avail=(struct input_avail*)(mouse.pages + INPUT_QUEUE_SIZE * sizeof(virtq_desc_t));
    mouse.used=(struct input_used*)(mouse.pages+PGSIZE);
    for(int i=0;i<qsize;i++){ mouse.desc[i].addr=(uint64)(uint32)&mouse.events[i]; mouse.desc[i].len=8; mouse.desc[i].flags=2; mouse.avail->ring[i]=i; }
    mouse.avail->idx=qsize; r[28]|=4; if (os_debug) lib_puts("Mouse Ready\n");
}
void virtio_mouse_isr() {
    if(!mouse.used || mouse.queue_size == 0) return;
    uint16 cur = mouse.used->idx;
    while(mouse.used_idx != cur) {
        int id = mouse.used->ring[mouse.used_idx % mouse.queue_size].id;
        if (id < 0 || id >= mouse.queue_size) break;
        enqueue_input_event(1, mouse.events[id].type, mouse.events[id].code, mouse.events[id].value);
        mouse.avail->ring[mouse.avail->idx % mouse.queue_size] = id;
        mouse.avail->idx++; mouse.used_idx++;
    }
    *(volatile uint32*)(VIRTIO_MOUSE_BASE + 0x64) = *(volatile uint32*)(VIRTIO_MOUSE_BASE + 0x60) & 3;
}

void virtio_input_poll(void) {
    static int caps_lock = 0, shift_pressed = 0, ctrl_l_pressed = 0, ctrl_r_pressed = 0;
    if (input_poll_busy) return;
    input_poll_busy = 1;
#ifdef FPGA_MINIMAL
    fpga_keyboard_mouse_tick();
#endif
    // 雿輻??蝛拙???kmap ??smap
    static char kmap[128] = {
        [1]=27, [2]='1', [3]='2', [4]='3', [5]='4', [6]='5', [7]='6', [8]='7', [9]='8', [10]='9', [11]='0', [0x0C]='-', [0x0D]='=', [0x0E]=8, [0x0F]='\t',
        [0x10]='q', [0x11]='w', [0x12]='e', [0x13]='r', [0x14]='t', [0x15]='y', [0x16]='u', [0x17]='i', [0x18]='o', [0x19]='p', [0x1A]='[', [0x1B]=']', [0x1C]=10,
        [0x1E]='a', [0x1F]='s', [0x20]='d', [0x21]='f', [0x22]='g', [0x23]='h', [0x24]='j', [0x25]='k', [0x26]='l', [0x27]=';', [0x28]='\'', [0x29]='`',
        [0x2B]='\\', [0x2C]='z', [0x2D]='x', [0x2E]='c', [0x2F]='v', [0x30]='b', [0x31]='n', [0x32]='m', [0x33]=',', [0x34]='.', [0x35]='/', [0x39]=' ',
        [0x47]='7', [0x48]='8', [0x49]='9', [0x4A]='-', [0x4B]='4', [0x4C]='5', [0x4D]='6', [0x4E]='+', [0x4F]='1', [0x50]='2', [0x51]='3', [0x52]='0', [0x53]='.', [0x60]=10, [0x62]='/'
    };
    static char smap[128] = {
        [2]='!', [3]='@', [4]='#', [5]='$', [6]='%', [7]='^', [8]='&', [9]='*', [10]='(', [11]=')', [0x0C]='_', [0x0D]='+',
        [0x1A]='{', [0x1B]='}', [0x27]=':', [0x28]='"', [0x29]='~', [0x2B]='|', [0x33]='<', [0x34]='>', [0x35]='?'
    };

    int event_budget = 32;
    while (input_evt_head != input_evt_tail && event_budget-- > 0) {
        struct raw_input_event ev = input_events[input_evt_head];
        input_evt_head = (input_evt_head + 1) % INPUT_EVENT_QUEUE_SIZE;

        if (ev.device == 0) { // Keyboard
            if (ev.type != 1) continue;

            // 1. ???? (Shift, Ctrl) - ???????
            if (ev.code == 0x2A || ev.code == 0x36) { shift_pressed = (ev.value == 1); }
            else if (ev.code == 29) {
                ctrl_l_pressed = (ev.value == 1);
                gui_ctrl_pressed = (ctrl_l_pressed || ctrl_r_pressed);
                CTRLDBG_PRINTF("[CTRLDBG] input lctrl value=%u pressed=%d any=%d\n",
                               ev.value, ctrl_l_pressed, gui_ctrl_pressed);
            }
            else if (ev.code == 97) {
                ctrl_r_pressed = (ev.value == 1);
                gui_ctrl_pressed = (ctrl_l_pressed || ctrl_r_pressed);
                CTRLDBG_PRINTF("[CTRLDBG] input rctrl value=%u pressed=%d any=%d\n",
                               ev.value, ctrl_r_pressed, gui_ctrl_pressed);
            }
            
            // 2. ???? (Key Down ??Repeat)
#ifdef FPGA_MINIMAL
            if (fpga_keyboard_mouse_handle_key(ev.code, ev.value != 0)) {
                continue;
            }
#endif

            if (ev.value == 1 || ev.value == 2) {
                if (ev.code == 46) {
                    CTRLDBG_PRINTF("[CTRLDBG] input raw-c ctrl_l=%d ctrl_r=%d any=%d\n",
                                   ctrl_l_pressed, ctrl_r_pressed, (ctrl_l_pressed || ctrl_r_pressed));
                }
                if (ev.code == 25) {
                    gui_prev_regs_pressed = 1;
                    gui_redraw_needed = 1;
                }
                // ?寞?敹急??
                if ((ctrl_l_pressed || ctrl_r_pressed) && ev.code == 15) gui_shortcut_switch_task = 1;
                else if ((ctrl_l_pressed || ctrl_r_pressed) && ev.code == 20) gui_shortcut_new_task = 1;
                else if ((ctrl_l_pressed || ctrl_r_pressed) && ev.code == 16) gui_shortcut_close_task = 1;
                else if ((ctrl_l_pressed || ctrl_r_pressed) && ev.code == 46) {
                    gui_key_submit(3);
                    CTRLDBG_PRINTF("[CTRLDBG] input ctrl-c gui_key=%d\n", gui_key);
                }
                else if (ev.code == 0x3A) caps_lock = !caps_lock;
                // ?孵??菔??寞???(?Ｗ儔 Vim ?舀)
                else if (ev.code == 103) gui_key_submit(0x10); // Up
                else if (ev.code == 108) gui_key_submit(0x11); // Down
                else if (ev.code == 105) gui_key_submit(0x12); // Left
                else if (ev.code == 106) gui_key_submit(0x13); // Right
                else if (ev.code == 111) gui_key_submit(0x14); // Delete
                else if (ev.code == 102) gui_key_submit(0x15); // Home
                else if (ev.code == 107) gui_key_submit(0x16); // End
                else if (ev.code == 104) gui_key_submit(0x17); // PageUp
                else if (ev.code == 109) gui_key_submit(0x18); // PageDown
                else if (ev.code == 110) gui_key_submit(0x19); // Insert
                else if (ev.code < 128) {
                    char k = kmap[ev.code];
                    if (shift_pressed && smap[ev.code]) gui_key_submit(smap[ev.code]);
                    else if ((k >= 'a' && k <= 'z') && (caps_lock ^ shift_pressed)) gui_key_submit(k - 'a' + 'A');
                    else gui_key_submit(k);
                }

                // 3. ???湔 GameBoy ???(銝蝙??else if嚗Ⅱ靽? gui_key 銝西?)
                if (ev.code == 0x2C) gbemu_btn_a = 1;
                if (ev.code == 0x2D) gbemu_btn_b = 1;
                if (ev.code == 0x1C || ev.code == 0x60) gbemu_btn_start = 1;
                if (ev.code == 0x0F) gbemu_btn_select = 1;
                if (ev.code == 103) gbemu_btn_up = 1;
                if (ev.code == 108) gbemu_btn_down = 1;
                if (ev.code == 105) gbemu_btn_left = 1;
                if (ev.code == 106) gbemu_btn_right = 1;
            } 
            // 4. ???暸? (Key Up)
            else if (ev.value == 0) {
                if (ev.code == 25) {
                    gui_prev_regs_pressed = 0;
                    gui_redraw_needed = 1;
                }
                if (ev.code == 0x2C) gbemu_btn_a = 0;
                if (ev.code == 0x2D) gbemu_btn_b = 0;
                if (ev.code == 0x1C || ev.code == 0x60) gbemu_btn_start = 0;
                if (ev.code == 0x0F) gbemu_btn_select = 0;
                if (ev.code == 103) gbemu_btn_up = 0;
                if (ev.code == 108) gbemu_btn_down = 0;
                if (ev.code == 105) gbemu_btn_left = 0;
                if (ev.code == 106) gbemu_btn_right = 0;
            }
        } else if (ev.device == 1) { // Mouse
            if (ev.type == 3) {
                if (ev.code == 0) gui_mx = (ev.value * WIDTH) / 32768;
                if (ev.code == 1) gui_my = (ev.value * HEIGHT) / 32768;
            } else if (ev.type == 2 && ev.code == 8) {
                gui_wheel = (int)ev.value;
            } else if (ev.type == 1 && ev.code == 0x110) {
                gui_clicked = (ev.value == 1);
                if (gui_clicked) gui_click_pending = 1;
            } else if (ev.type == 1 && ev.code == 0x111) {
                gui_right_clicked = (ev.value == 1);
                if (gui_right_clicked) gui_right_click_pending = 1;
            }
        }
    }
    input_poll_busy = 0;
}

