#include "user_internal.h"
#include "common.h"

extern int gbemu_btn_a;
extern int gbemu_btn_b;
extern int gbemu_btn_start;
extern int gbemu_btn_select;
extern int gbemu_btn_up;
extern int gbemu_btn_down;
extern int gbemu_btn_left;
extern int gbemu_btn_right;
extern void gb_timer_init(void);
extern int audio_init(void);
extern void gbemu_audio_enable_diag_beep(int frames);
extern int ac97_audio_init(void);
extern void gbemu_audio_pump(void);

static emu_context gbemu_ctx;
static int gbemu_loaded;
static int gbemu_window_id = -1;
static uint32_t gbemu_last_frame;
static int gbemu_exit_code;
static int gbemu_audio_ready;

// 修正：將 FPS 變數移到最上方，這樣下方的所有函式(包含 gbemu_reset_context)都能認識它們
static u32 fps_last_frame_count = 0;
static u32 fps_last_time = 0;

emu_context *emu_get_context(void) {
    return &gbemu_ctx;
}

// 外部定義，用於手動內聯
extern void timer_tick_complex(void);
extern u16 timer_div;

extern u32 ppu_line_ticks;
extern u8 ppu_current_mode;

extern bool dma_active(void);
extern void dma_tick(void);

void emu_cycles(int cpu_cycles) {
    ppu_context *p = ppu_get_context();
    for (int i = 0; i < cpu_cycles; i++) {
        if (ppu_current_mode == MODE_VBLANK) {
            // VBlank 快速跳轉邏輯
            gbemu_ctx.ticks += 4; timer_div += 4;
            if ((timer_div & 0xFF) < 4) timer_tick_complex();
            p->line_ticks += 4;
            if (p->line_ticks >= TICKS_PER_LINE) ppu_tick();
        } else {
            // 標準週期展開
            for (int j = 0; j < 4; j++) {
                gbemu_ctx.ticks++; timer_div++; 
                if ((timer_div & 0xFF) == 0) timer_tick_complex();
                ppu_tick();
            }
        }
        // 內聯 DMA 檢查，只有在活動時才進去
        if (dma_active()) dma_tick();
    }
}

void exit(int code) {
    gbemu_exit_code = code;
    gbemu_ctx.running = false;
    gbemu_ctx.paused = false;
    gbemu_ctx.too = true;
    gbemu_ctx.die = true;
}

void delay(u32 ms) {
    lib_delay((volatile int)ms);
}

u32 get_ticks() {
    return sys_now();
}

static void gbemu_copy_title(struct Window *w) {
    cart_context *cart = cart_get_context();
    char title[24];
    title[0] = '\0';
    if (cart && cart->header && cart->header->title[0]) {
        snprintf(title, sizeof(title), "GB: %.18s", cart->header->title);
    } else {
        lib_strcpy(title, "GBEMU");
    }
    lib_strcpy(w->title, title);
}

static void gbemu_reset_context(void) {
    memset(&gbemu_ctx, 0, sizeof(gbemu_ctx));
    gbemu_loaded = 0;
    gbemu_last_frame = 0;
    gbemu_window_id = -1;
    gbemu_audio_ready = 0;
    
    // 現在編譯器認得這兩個變數了
    fps_last_time = 0;
    fps_last_frame_count = 0;
}

static int gbemu_load_from_bytes(struct Window *term, const unsigned char *buf, uint32_t size, const char *rom_name, char *out, int out_max) {
    cart_context *cart = cart_get_context();
    if (!buf || size < 0x150) {
        if (out && out_max > 0) lib_strcpy(out, "ERR: Invalid ROM.");
        return -1;
    }

    if (cart->rom_data) free(cart->rom_data);
    for (int i = 0; i < 16; i++) {
        if (cart->ram_banks[i]) free(cart->ram_banks[i]);
        cart->ram_banks[i] = 0;
    }
    memset(cart, 0, sizeof(*cart));
    cart->rom_size = size;
    cart->rom_data = malloc(size);
    if (!cart->rom_data) {
        if (out && out_max > 0) lib_strcpy(out, "ERR: Out of memory.");
        return -2;
    }
    memcpy(cart->rom_data, buf, size);
    if (rom_name) {
        snprintf(cart->filename, sizeof(cart->filename), "%s", rom_name);
    } else {
        lib_strcpy(cart->filename, "rom.gb");
    }
    cart->header = (rom_header *)(cart->rom_data + 0x100);
    cart->header->title[15] = 0;
    cart->battery = cart_battery();
    cart->need_save = false;
    cart_setup_banking();

    gbemu_reset_context();
    gamepad_init();
    gb_timer_init();
    cpu_init();
    ppu_init();
    gbemu_ctx.running = true;
    gbemu_ctx.paused = false;
    gbemu_ctx.too = false;
    gbemu_ctx.die = false;
    gbemu_ctx.ticks = 0;
    gbemu_loaded = 1;
    gbemu_last_frame = ppu_get_context()->current_frame;
    lib_printf("[GBEMU] loaded rom='%s' size=%u\n", cart->filename, size);

    if (out && out_max > 0) {
        lib_strcpy(out, ">> GBEMU Opened.");
    }
    return 0;
}

static void gbemu_sync_button_state(void) {
    gamepad_state *st = gamepad_get_state();
    if (!st) return;
    st->a = gbemu_btn_a;
    st->b = gbemu_btn_b;
    st->start = gbemu_btn_start;
    st->select = gbemu_btn_select;
    st->up = gbemu_btn_up;
    st->down = gbemu_btn_down;
    st->left = gbemu_btn_left;
    st->right = gbemu_btn_right;
}

void gbemu_sync_gamepad(void) {
    gbemu_sync_button_state();
}

void gbemu_audio_pump(void) {
    if (!gbemu_audio_ready) {
        lib_printf("[GBEMU] init audio\n");
        audio_init();
        if (ac97_audio_init() == 0) {
            gbemu_audio_ready = 1;
            lib_printf("[GBEMU] audio ready\n");
        } else {
            lib_printf("[GBEMU] audio init failed\n");
            return;
        }
    }
    ac97_audio_pump();
}

int gbemu_has_active_window(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!wins[i].active || wins[i].minimized) continue;
        if (wins[i].kind == WINDOW_KIND_GBEMU) return 1;
    }
    return 0;
}

static u32 emu_clock_ms = 0;

int gbemu_tick_active(int budget) {
    if (!gbemu_loaded) return 0;
    
    u32 now = sys_now();
    if (fps_last_time == 0) {
        fps_last_time = now;
        fps_last_frame_count = ppu_get_context() ? ppu_get_context()->current_frame : 0;
    }

    // 計算 FPS (每秒顯示一次)
    if (now - fps_last_time >= 1000) {
        u32 current_frame = ppu_get_context()->current_frame;
        u32 delta_frames = (current_frame >= fps_last_frame_count) ? (current_frame - fps_last_frame_count) : 0;
        u32 delta_time = now - fps_last_time;
        if (delta_time == 0) delta_time = 1;
        u32 current_fps = (delta_frames * 1000) / delta_time;
        
        lib_printf("[FPS] frames=%u, ms=%u, fps=%u\n", delta_frames, delta_time, current_fps);

        char title[64];
        snprintf(title, sizeof(title), "GB: FPS %u", current_fps);
        lib_strcpy(wins[gbemu_window_id].title, title);
        
        fps_last_frame_count = current_frame;
        fps_last_time = now;
    }

    // --- 突破 60 FPS 的關鍵：一次跑完 2 幀才交還控制權給 GUI ---
    int frames_to_run = 2; 
    int total_frames = 0;

    while (total_frames < frames_to_run) {
        u32 start_frame = ppu_get_context()->current_frame;
        int limit = 500000; 
        while (limit-- > 0 && gbemu_ctx.running) {
            if (!cpu_step()) break;
            if (ppu_get_context()->current_frame != start_frame) {
                total_frames++;
                ac97_audio_pump(); // 每跑完一幀就餵一次音訊，確保音樂不間斷
                break; // 跳出當前幀的模擬循環，進入下一幀
            }
        }
        if (limit <= 0) break; // 防止死循環
    }
    return (total_frames > 0);
}

void handle_gbemu_input(struct Window *aw, char *gui_key) {
    if (!aw || !gui_key) return;
    if (*gui_key == 3) {
        close_window(aw->id);
        *gui_key = 0;
        return;
    }
    *gui_key = 0;
}

void draw_gbemu_content(struct Window *w, int x, int y, int ww, int wh) {
    int ox = x + 10;
    int oy = y + 34;
    int scale = 4;
    extern uint8_t *vga_get_vbuf(void);
    uint8_t *vbuf = vga_get_vbuf();

    if (gbemu_loaded && ppu_get_context() && ppu_get_context()->video_buffer) {
        uint32_t *vb = ppu_get_context()->video_buffer;
        static uint8_t line_buffer[160 * 4];

        for (int py = 0; py < 144; py++) {
            int dy = oy + (py * scale);
            if (dy < 0 || dy >= HEIGHT) continue;
            
            uint32_t *row_ptr = &vb[py * 160];
            for (int px = 0; px < 160; px++) {
                uint32_t color = row_ptr[px];
                unsigned char r = (unsigned char)((color >> 16) & 0xFF);
                unsigned char g = (unsigned char)((color >> 8) & 0xFF);
                unsigned char b = (unsigned char)(color & 0xFF);
                uint8_t p_idx = (uint8_t)palette_index_from_rgb(r, g, b);
                line_buffer[px * 4 + 0] = p_idx;
                line_buffer[px * 4 + 1] = p_idx;
                line_buffer[px * 4 + 2] = p_idx;
                line_buffer[px * 4 + 3] = p_idx;
            }

            for (int sy = 0; sy < scale; sy++) {
                int ty = dy + sy;
                if (ty >= 0 && ty < HEIGHT) {
                    memcpy(&vbuf[ox + ty * WIDTH], line_buffer, 160 * 4);
                }
            }
        }
        return;
    }

    draw_rect_fill(ox, oy, 160 * scale, 144 * scale, 0);
    draw_text_scaled(ox + 8, oy + 8, "GBEMU", COL_TEXT, 1);
    draw_text_scaled(ox + 8, oy + 24, "Loading ROM...", COL_TEXT, 1);
}

int open_gbemu_window(struct Window *term, const char *rom_path) {
    unsigned char *rom_buf = 0;
    uint32_t rom_size = 0;
    int rc = load_file_bytes_alloc(term, rom_path, &rom_buf, &rom_size);
    if (rc != 0) return rc;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (wins[i].active && wins[i].kind == WINDOW_KIND_GBEMU) {
            close_window(i);
        }
    }

    int win = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!wins[i].active) {
            win = i;
            break;
        }
    }
    if (win < 0) {
        free(rom_buf);
        return -3;
    }

    reset_window(&wins[win], win);
    wins[win].active = 1;
    wins[win].minimized = 0;
    wins[win].maximized = 0;
    wins[win].kind = WINDOW_KIND_GBEMU;
    wins[win].w = 660;
    wins[win].h = 616;
    wins[win].x = (WIDTH - wins[win].w) / 2;
    wins[win].y = (DESKTOP_H - wins[win].h) / 2;
    lib_strcpy(wins[win].title, "GBEMU");
    bring_to_front(win);

    rc = gbemu_load_from_bytes(term, rom_buf, rom_size, rom_path, 0, 0);
    free(rom_buf);
    if (rc != 0) {
        close_window(win);
        return rc;
    }

    gbemu_copy_title(&wins[win]);
    gbemu_window_id = win;
    gbemu_audio_pump();
    gui_redraw_needed = 1;
    need_resched = 1;
    return 0;
}