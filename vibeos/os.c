#include "os.h"
#include "vga.h"
#include "mbedtls_port.h"
#include "virtio.h"

extern void trap_init(void);
extern void trap_early_init(void);
extern void virtio_disk_init(void);
extern void virtio_net_init(void);
extern void virtio_keyboard_init(void);
extern void virtio_mouse_init(void);
extern void ps_gem_probe(void);
extern void ps_gem_init(void);
extern void virtio_input_poll(void);
extern void draw_rect_fill(int x, int y, int w, int h, int color);
extern void draw_text(int x, int y, const char *s, int color);
extern void draw_text_scaled(int x, int y, const char *s, int color, int scale);
extern void draw_text_scaled_clipped(int x, int y, const char *s, int color, int scale, int clip_x0, int clip_y0, int clip_x1, int clip_y1);
extern void vga_update(void);
extern int gui_mx, gui_my;
volatile int need_resched = 0;
int os_debug = 0;

#ifdef FPGA_MINIMAL
volatile uint32_t fpga_boot_trace[16];

static void fpga_boot_mark(int idx, uint32_t value)
{
	if (idx >= 0 && idx < 16) {
		fpga_boot_trace[idx] = value;
		asm volatile("" ::: "memory");
	}
}
#endif

void os_kernel()
{
	task_os();
}

void disk_read() {}

#ifdef FPGA_MINIMAL
static int fpga_minimal_strlen(const char *s)
{
	int n = 0;
	while (s && s[n]) n++;
	return n;
}

static int fpga_minimal_text_scale_fit(const char *s, int max_w, int max_h, int preferred)
{
	int len = fpga_minimal_strlen(s);
	int scale = preferred;
	if (scale < 1) scale = 1;
	while (scale > 1 && (len * 8 * scale > max_w || 16 * scale > max_h)) {
		scale--;
	}
	return scale;
}

static void fpga_minimal_draw_fit_text(int x, int y, int w, int h, const char *s, int color, int preferred)
{
	(void)w;
	(void)h;
	(void)preferred;
	draw_text(x, y, s, color);
}

static void fpga_minimal_draw_boot(const char *storage_status)
{
	const int u = WIDTH / 32;
	const int top_h = HEIGHT / 10;
	const int task_h = HEIGHT / 10;
	const int win_x = u;
	const int win_y = top_h + u;
	const int win_w = WIDTH - u * 2;
	const int win_h = HEIGHT - top_h - task_h - u * 2;
	const int title_h = win_h / 5;
	const int body_x = win_x + u;
	const int body_y = win_y + title_h + u;
	const int body_w = win_w - u * 2;
	const int row_h = (win_h - title_h - u * 2) / 4;

	draw_rect_fill(0, 0, WIDTH, HEIGHT, UI_C_DESKTOP);
	draw_rect_fill(0, 0, WIDTH, top_h, UI_C_PANEL_DARK);
	fpga_minimal_draw_fit_text(u, top_h / 4, WIDTH / 3, top_h / 2, "VIBE OS", UI_C_TEXT, 2);
	fpga_minimal_draw_fit_text(WIDTH - WIDTH / 3 - u, top_h / 4, WIDTH / 3, top_h / 2,
				   "FPGA HDMI", UI_C_TEXT_DIM, 1);

	draw_rect_fill(win_x, win_y, win_w, win_h, UI_C_PANEL);
	draw_rect_fill(win_x, win_y, win_w, title_h, UI_C_PANEL_ACTIVE);
	draw_rect_fill(win_x, win_y + title_h - 2, win_w, 2, UI_C_BORDER);
	fpga_minimal_draw_fit_text(win_x + u, win_y + title_h / 5, win_w - u * 2, title_h * 3 / 5,
				   "VIBE OS ROOT", UI_C_TEXT, 4);

	draw_rect_fill(body_x, body_y, body_w, row_h - u / 2, UI_C_PANEL_LIGHT);
	fpga_minimal_draw_fit_text(body_x + u, body_y + u, body_w - u * 2, row_h - u * 2,
				   "ROOT /", UI_C_TEXT, 2);

	draw_rect_fill(body_x, body_y + row_h, body_w, row_h - u / 2, UI_C_PANEL_DEEP);
	fpga_minimal_draw_fit_text(body_x + u, body_y + row_h + u, body_w - u * 2, row_h - u * 2,
				   storage_status, UI_C_TEXT, 2);

	draw_rect_fill(body_x, body_y + row_h * 2, body_w / 2 - u / 2, row_h - u / 2, UI_C_PANEL_LIGHT);
	draw_rect_fill(body_x + body_w / 2 + u / 2, body_y + row_h * 2, body_w / 2 - u / 2, row_h - u / 2, UI_C_PANEL_LIGHT);
	fpga_minimal_draw_fit_text(body_x + u, body_y + row_h * 2 + u, body_w / 2 - u * 2, row_h - u * 2,
				   "DISK ONLY", UI_C_TEXT_DIM, 1);
	fpga_minimal_draw_fit_text(body_x + body_w / 2 + u, body_y + row_h * 2 + u,
				   body_w / 2 - u * 2, row_h - u * 2, "1024X768", UI_C_TEXT_DIM, 1);

	draw_rect_fill(body_x, body_y + row_h * 3, body_w, row_h / 3, UI_C_PANEL_LIGHT);
	draw_rect_fill(body_x, body_y + row_h * 3, body_w * 3 / 4, row_h / 3, UI_C_TEXT);

	draw_rect_fill(0, HEIGHT - task_h, WIDTH, task_h, UI_C_PANEL_DARK);
	draw_rect_fill(u, HEIGHT - task_h + u / 2, WIDTH / 5, task_h - u, UI_C_PANEL_ACTIVE);
	fpga_minimal_draw_fit_text(u * 2, HEIGHT - task_h + u, WIDTH / 5 - u * 2, task_h - u * 2,
				   "ROOT", UI_C_TEXT, 1);
	vga_update();
}

static void fpga_minimal_draw_cursor(void)
{
	int x = gui_mx;
	int y = gui_my;
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x > WIDTH - 8) x = WIDTH - 8;
	if (y > HEIGHT - 12) y = HEIGHT - 12;
	draw_rect_fill(x, y, 8, 2, UI_C_TEXT);
	draw_rect_fill(x, y, 2, 12, UI_C_TEXT);
	draw_rect_fill(x + 2, y + 2, 2, 8, UI_C_TEXT);
	draw_rect_fill(x + 4, y + 4, 2, 5, UI_C_TEXT);
	vga_update();
}

static void fpga_minimal_block_smoke(void)
{
	struct blk w;
	struct blk r;
	memset(&w, 0, sizeof(w));
	memset(&r, 0, sizeof(r));
	w.blockno = 7;
	r.blockno = 7;
	w.data[0] = 'V';
	w.data[1] = 'I';
	w.data[2] = 'B';
	w.data[3] = 'E';
	w.data[4] = 0x42;
	virtio_disk_rw(&w, 1);
	virtio_disk_rw(&r, 0);
	if (r.data[0] == 'V' && r.data[1] == 'I' && r.data[2] == 'B' && r.data[3] == 'E' && r.data[4] == 0x42) {
		lib_puts("[VIBE] block smoke: read/write OK\n");
	} else {
		panic("ramdisk smoke failed");
	}
}

static void fpga_minimal_usb0_host_init(void)
{
	volatile uint32_t * const usb_cmd  = (volatile uint32_t *)0xE0002140UL;
	volatile uint32_t * const usb_sts  = (volatile uint32_t *)0xE0002144UL;
	volatile uint32_t * const ulpi_view = (volatile uint32_t *)0xE0002170UL;
	volatile uint32_t * const configflag = (volatile uint32_t *)0xE0002180UL;
	volatile uint32_t * const portsc1  = (volatile uint32_t *)0xE0002184UL;
	volatile uint32_t * const otgsc    = (volatile uint32_t *)0xE00021A4UL;
	volatile uint32_t * const usb_mode = (volatile uint32_t *)0xE00021A8UL;
	volatile uint32_t * const mouse_dbg = (volatile uint32_t *)0x10004000UL;

	/*
	 * The XSDB loader owns the destructive USB PHY/OTG release sequence.
	 * Do not touch ULPI/OTGSC/PORTSC here; doing so can drop a working
	 * keyboard/mouse back to no-connect.  The HID driver will do the
	 * non-destructive EHCI prepare/retry path when its poll task starts.
	 */
	mouse_dbg[8] = *portsc1;
	mouse_dbg[9] = *usb_cmd;
	mouse_dbg[10] = *usb_sts;
	mouse_dbg[11] = *usb_mode;
	mouse_dbg[12] = *otgsc;
	mouse_dbg[13] = *ulpi_view;
	mouse_dbg[14] = *configflag;
	mouse_dbg[15] = 0x55534253UL;
	if (os_debug) {
		lib_printf("[VIBE] usb0: cmd=%x sts=%x port=%x mode=%x otg=%x ulpi=%x\n",
			   *usb_cmd, *usb_sts, *portsc1, *usb_mode, *otgsc, *ulpi_view);
	}
}

static void fpga_minimal_mouse_mark(int code)
{
	volatile uint32_t *m = (volatile uint32_t *)0x10004000UL;
	m[15] = (uint32_t)code;
}
#endif

void os_start()
{
#ifdef FPGA_MINIMAL
	fpga_boot_mark(0, 0x100);
	fpga_minimal_mouse_mark(1);
	/* FPGA minimal uses cooperative polling and does not enable machine IRQs. */
	fpga_boot_mark(1, 0x101);
	fpga_minimal_mouse_mark(2);
#else
	uart_init();
#endif
#ifdef FPGA_MINIMAL
	fpga_boot_mark(2, 0x102);
	fpga_minimal_mouse_mark(3);
#else
	lib_puts("\n[VIBE] booting minimal FPGA profile\n");
#endif
#ifdef FPGA_MINIMAL
	fpga_boot_mark(3, 0x103);
	fpga_minimal_mouse_mark(4);
#endif
	page_init();
#ifdef FPGA_MINIMAL
	fpga_boot_mark(4, 0x104);
	fpga_minimal_mouse_mark(5);
#endif
	vga_init();
#ifdef FPGA_MINIMAL
	fpga_boot_mark(5, 0x105);
	fpga_minimal_mouse_mark(6);
#endif
#ifdef FPGA_MINIMAL
	/* Let the GUI task render the first visible frame. */
	fpga_boot_mark(6, 0x106);
	fpga_minimal_mouse_mark(7);
#else
	draw_rect_fill(0, 0, WIDTH, HEIGHT, 0);
	draw_text(16, 16, "VIBE OS BOOT", 15);
	draw_text(16, 36, "storage: initializing", 13);
	vga_update();
#endif
	if (os_debug) lib_puts("Hacking OS Initializing...\n");
	if (os_debug) lib_printf("[BOOT] after uart/page/vga\n");
#ifdef FPGA_MINIMAL
	fpga_minimal_mouse_mark(20);
	fpga_minimal_usb0_host_init();
	fpga_minimal_mouse_mark(21);
	/* USB HID state is initialized lazily by virtio_mouse_poll_task(). */
	fpga_minimal_mouse_mark(30);
	fpga_minimal_mouse_mark(40);
	/* GEM init is deferred to network_task so UI/keyboard boot cannot be
	   blocked by PHY/MDIO state. */
	fpga_minimal_mouse_mark(50);
	task_init();
	user_init();
	fpga_minimal_mouse_mark(60);
	while (1) {
		int current_task = task_next();
		if (current_task < 0) continue;
		task_go(current_task);
	}
#endif
	if (os_debug) lib_printf("[BOOT] before mbedtls_os_init\n");
	mbedtls_os_init();
	if (os_debug) lib_printf("[BOOT] after mbedtls_os_init\n");
	if (os_debug) lib_printf("[BOOT] before user_init\n");
	user_init();
	if (os_debug) lib_printf("[BOOT] after user_init\n");
	if (os_debug) lib_printf("[BOOT] before trap_init\n");
	trap_init();
	if (os_debug) lib_printf("[BOOT] after trap_init\n");
	if (os_debug) lib_printf("[BOOT] before plic_init\n");
	plic_init();
	if (os_debug) lib_printf("[BOOT] after plic_init\n");
#ifdef FPGA_MINIMAL
	return;
#endif
	if (os_debug) lib_printf("[BOOT] before virtio_disk_init\n");
	virtio_disk_init();
	if (os_debug) lib_printf("[BOOT] after virtio_disk_init\n");
#ifdef FPGA_MINIMAL
	lib_puts("[VIBE] block device: ready\n");
	lib_puts("[VIBE] skipping net/keyboard/audio drivers\n");
#else
	if (os_debug) lib_printf("[BOOT] before virtio_net_init\n");
	virtio_net_init();
	if (os_debug) lib_printf("[BOOT] after virtio_net_init\n");
	if (os_debug) lib_printf("[BOOT] before virtio_keyboard_init\n");
	virtio_keyboard_init();
	if (os_debug) lib_printf("[BOOT] after virtio_keyboard_init\n");
	if (os_debug) lib_printf("[BOOT] before virtio_mouse_init\n");
	virtio_mouse_init();
	if (os_debug) lib_printf("[BOOT] after virtio_mouse_init\n");
#endif
	if (os_debug) lib_printf("[BOOT] before timer_init\n");
	timer_init();
	if (os_debug) lib_printf("[BOOT] after timer_init\n");
	if (os_debug) lib_printf("[BOOT] before page_test\n");
	page_test();
	if (os_debug) lib_printf("[BOOT] after page_test\n");
}

int os_main(void) {
    if (os_debug) lib_printf("[BOOT] os_main enter\n");
    os_start();
    if (os_debug) lib_printf("[BOOT] os_main after start\n");
#ifdef FPGA_MINIMAL
    lib_puts("[VIBE] scheduler: running FPGA UI profile\n");
#endif
    while (1) {
        int current_task = task_next();
        if (os_debug) lib_printf("[BOOT] task_next=%d\n", current_task);
        if (current_task < 0) continue;
        if (os_debug) lib_printf("Go Task %d\n", current_task);
        if (os_debug) lib_printf("[BOOT] task_go=%d\n", current_task);
        task_go(current_task);
    }
}
