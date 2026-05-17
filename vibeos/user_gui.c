#include "user_gui.h"
#include "user_internal.h"
#include "jit_debugger.h"
#include "virtio.h"

#define CTRLDBG_PRINTF(...) do { } while (0)

uint32_t demo3d_fps_last_ms = 0;
uint32_t demo3d_fps_frames = 0;
uint32_t demo3d_fps_value = 0;
uint32_t demo3d_last_frame_ms = 0;
volatile int gui_redraw_needed = 0;
int gui_task_id = -1;
int z_order[MAX_WINDOWS];
int active_win_idx = -1;
uint8_t sheet_map[WIDTH * HEIGHT] __attribute__((aligned(16)));
int dither_err_r0[WIDTH + 2], dither_err_g0[WIDTH + 2], dither_err_b0[WIDTH + 2];
int dither_err_r1[WIDTH + 2], dither_err_g1[WIDTH + 2], dither_err_b1[WIDTH + 2];

#ifdef FPGA_MINIMAL
static void gui_update_window_area_with_cursor(struct Window *w, int old_mx, int old_my, int new_mx, int new_my)
{
    (void)old_mx;
    (void)old_my;
    int x = w->maximized ? 0 : w->x;
    int y = w->maximized ? 0 : w->y;
    int ww = w->maximized ? WIDTH : w->w;
    int wh = w->maximized ? DESKTOP_H : w->h;
    vga_update_rect(x, y, ww, wh);
    vga_update_rect(0, DESKTOP_H, WIDTH, TASKBAR_H);
    vga_present_cursor(new_mx, new_my, gui_cursor_mode, GUI_VISIBLE_H);
}

static void gui_update_taskbar_with_cursor(int mx, int my, int mode)
{
    draw_taskbar();
    vga_update_rect(0, DESKTOP_H, WIDTH, TASKBAR_H);
    vga_present_cursor(mx, my, mode, GUI_VISIBLE_H);
}

static int gui_terminal_fast_edit_key(struct Window *w, char key)
{
    if (!w || w->kind != WINDOW_KIND_TERMINAL) return 0;
    if (w->executing_cmd || w->waiting_wget || w->submit_locked) return 0;
    if (w->ssh_auth_mode) return key == 8 || (key >= 32 && key < 127);
    if (key == 8) return 1;
    if (key >= 32 && key < 127) return 1;
    return key >= 0x10 && key <= 0x16;
}

static void gui_fast_flush_terminal_window(struct Window *w, int mx, int my, int mode)
{
    if (!w || !w->active || w->minimized) return;
    draw_window(w);
    gui_update_window_area_with_cursor(w, mx, my, mx, my);
}
#endif

void close_window(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS) return;
    if (wins[idx].active && wins[idx].kind == WINDOW_KIND_NETSURF) {
        extern void netsurf_release_window(int win_id);
        netsurf_release_window(idx);
    }
    if (active_win_idx == idx) {
        active_win_idx = -1;
    }
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (z_order[i] != idx) continue;
        for (int k = i; k < MAX_WINDOWS - 1; k++) {
            z_order[k] = z_order[k + 1];
        }
        z_order[MAX_WINDOWS - 1] = idx;
        break;
    }
    reset_window(&wins[idx], idx);
    wins[idx].taskbar_anim = 0;
    wins[idx].selecting = 0;
    wins[idx].has_selection = 0;
    if (active_win_idx == -1) {
        cycle_active_window();
    }
    request_gui_redraw();
}

int taskbar_button_x(int idx) {
    return TASKBAR_START_X + idx * (TASKBAR_BTN_W + TASKBAR_BTN_GAP);
}

void set_window_title(struct Window *w, int idx) {
    lib_strcpy(w->title, "Term #");
    char num[12];
    lib_itoa((uint32_t)idx, num);
    lib_strcat(w->title, num);
}

void request_gui_redraw(void) {
    gui_redraw_needed = 1;
    need_resched = 1;
    if (gui_task_id >= 0) task_wake(gui_task_id);
}

int active_window_valid(void) {
    if (active_win_idx < 0 || active_win_idx >= MAX_WINDOWS) return 0;
    if (!wins[active_win_idx].active) return 0;
    if (wins[active_win_idx].minimized) return 0;
    return 1;
}

int any_window_active(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (wins[i].active && !wins[i].minimized) return 1;
    }
    return 0;
}

int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int cursor_mode_for_resize_dir(int dir) {
    if ((dir & RESIZE_LEFT) && (dir & RESIZE_TOP)) return CURSOR_SIZE_D2;
    if ((dir & RESIZE_RIGHT) && (dir & RESIZE_BOTTOM)) return CURSOR_SIZE_D2;
    if ((dir & RESIZE_RIGHT) && (dir & RESIZE_TOP)) return CURSOR_SIZE_D1;
    if ((dir & RESIZE_LEFT) && (dir & RESIZE_BOTTOM)) return CURSOR_SIZE_D1;
    if (dir & (RESIZE_LEFT | RESIZE_RIGHT)) return CURSOR_SIZE_H;
    if (dir & (RESIZE_TOP | RESIZE_BOTTOM)) return CURSOR_SIZE_V;
    return CURSOR_NORMAL;
}

void resize_image_window(struct Window *w, int new_scale) {
    if (w->kind != WINDOW_KIND_IMAGE) return;
    if (new_scale < 1) new_scale = 1;
    if (new_scale > 12) new_scale = 12;
    if (new_scale == w->image_scale) return;

    int center_x = w->x + w->w / 2;
    int center_y = w->y + w->h / 2;

    w->image_scale = new_scale;
    w->w = w->image_w * new_scale + 20;
    if (w->w < 220) w->w = 220;
    if (w->w > WIDTH) w->w = WIDTH;
    w->h = w->image_h * new_scale + 46;
    if (w->h < 140) w->h = 140;
    if (w->h > DESKTOP_H) w->h = DESKTOP_H;

    w->x = center_x - w->w / 2;
    w->y = center_y - w->h / 2;
    w->x = clamp_int(w->x, 0, WIDTH - w->w);
    w->y = clamp_int(w->y, 0, DESKTOP_H - w->h);
}

int hit_resize_zone(struct Window *w, int mx, int my) {
    int wx = w->x, wy = w->y, ww = w->w, wh = w->h;
    int dir = RESIZE_NONE;
    if (w->maximized || w->minimized) return RESIZE_NONE;
    if (mx < wx || mx >= wx + ww || my < wy || my >= wy + wh) return RESIZE_NONE;
    if (mx < wx + RESIZE_MARGIN) dir |= RESIZE_LEFT;
    else if (mx >= wx + ww - RESIZE_MARGIN) dir |= RESIZE_RIGHT;
    if (my < wy + RESIZE_MARGIN) dir |= RESIZE_TOP;
    else if (my >= wy + wh - RESIZE_MARGIN) dir |= RESIZE_BOTTOM;
    return dir;
}

void begin_resize(struct Window *w, int dir, int mx, int my) {
    w->resizing = 1;
    w->resize_dir = dir;
    w->resize_start_mx = mx;
    w->resize_start_my = my;
    w->resize_start_x = w->x;
    w->resize_start_y = w->y;
    w->resize_start_w = w->w;
    w->resize_start_h = w->h;
}

void apply_resize(struct Window *w, int mx, int my) {
    int dx = mx - w->resize_start_mx;
    int dy = my - w->resize_start_my;
    int new_x = w->resize_start_x;
    int new_y = w->resize_start_y;
    int new_w = w->resize_start_w;
    int new_h = w->resize_start_h;

    if (w->resize_dir & RESIZE_RIGHT) {
        new_w = w->resize_start_w + dx;
    }
    if (w->resize_dir & RESIZE_BOTTOM) {
        new_h = w->resize_start_h + dy;
    }
    if (w->resize_dir & RESIZE_LEFT) {
        new_x = w->resize_start_x + dx;
        new_w = w->resize_start_w - dx;
        if (new_w < MIN_WIN_W) {
            new_x -= (MIN_WIN_W - new_w);
            new_w = MIN_WIN_W;
        }
    }
    if (w->resize_dir & RESIZE_TOP) {
        new_y = w->resize_start_y + dy;
        new_h = w->resize_start_h - dy;
        if (new_h < MIN_WIN_H) {
            new_y -= (MIN_WIN_H - new_h);
            new_h = MIN_WIN_H;
        }
    }

    new_w = clamp_int(new_w, MIN_WIN_W, WIDTH);
    new_h = clamp_int(new_h, MIN_WIN_H, DESKTOP_H);
    new_x = clamp_int(new_x, 0, WIDTH - new_w);
    new_y = clamp_int(new_y, 0, DESKTOP_H - new_h);

    w->x = new_x;
    w->y = new_y;
    w->w = new_w;
    w->h = new_h;
    if (w->kind == WINDOW_KIND_TERMINAL) terminal_clamp_v_offset(w);
    if (w->kind == WINDOW_KIND_NETSURF) {
        w->v_offset = 0;
        w->ns_h_offset = 0;
        w->ns_input_active = 0;
        w->ns_resize_pending = 1;
        w->ns_resize_last_ms = sys_now();
        extern void netsurf_invalidate_layout(int win_id);
        netsurf_invalidate_layout(w->id);
    }
}

void refresh_window_after_geometry_change(struct Window *w) {
    if (!w) return;
    if (w->kind == WINDOW_KIND_TERMINAL) {
        terminal_clamp_v_offset(w);
    }
    if (w->kind == WINDOW_KIND_NETSURF) {
        w->v_offset = 0;
        w->ns_h_offset = 0;
        w->ns_input_active = 0;
        w->ns_resize_pending = 1;
        w->ns_resize_last_ms = sys_now();
        extern void netsurf_invalidate_layout(int win_id);
        netsurf_invalidate_layout(w->id);
    }
}

void maximize_window(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS) return;
    struct Window *w = &wins[idx];
    if (!w->active || w->minimized) return;
    if (!w->maximized) {
        w->prev_x = w->x;
        w->prev_y = w->y;
        w->prev_w = w->w;
        w->prev_h = w->h;
        w->maximized = 1;
    }
    w->dragging = 0;
    w->scroll_dragging = 0;
    w->resizing = 0;
    w->resize_dir = RESIZE_NONE;
    refresh_window_after_geometry_change(w);
}

void restore_window(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS) return;
    struct Window *w = &wins[idx];
    if (!w->active) return;
    w->minimized = 0;
    if (w->maximized) {
        w->x = w->prev_x;
        w->y = w->prev_y;
        w->w = w->prev_w;
        w->h = w->prev_h;
        w->maximized = 0;
    }
    w->dragging = 0;
    w->scroll_dragging = 0;
    w->resizing = 0;
    w->resize_dir = RESIZE_NONE;
    refresh_window_after_geometry_change(w);
}

void minimize_window(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS) return;
    struct Window *w = &wins[idx];
    if (!w->active) return;
    w->minimized = 1;
    w->dragging = 0;
    w->scroll_dragging = 0;
    w->resizing = 0;
    w->resize_dir = RESIZE_NONE;
    if (active_win_idx == idx) active_win_idx = -1;
}

void snap_window_by_key(int idx, int key) {
    if (idx < 0 || idx >= MAX_WINDOWS) return;
    struct Window *w = &wins[idx];
    if (!w->active || w->minimized) return;

    if (!w->maximized) {
        w->prev_x = w->x;
        w->prev_y = w->y;
        w->prev_w = w->w;
        w->prev_h = w->h;
    }

    int half_w = WIDTH / 2;
    int half_h = DESKTOP_H / 2;
    int new_x = 0;
    int new_y = 0;
    int new_w = WIDTH;
    int new_h = DESKTOP_H;

    if (key == 0x12) {
        new_w = half_w;
        new_h = DESKTOP_H;
    } else if (key == 0x13) {
        new_x = half_w;
        new_w = WIDTH - half_w;
        new_h = DESKTOP_H;
    } else if (key == 0x10) {
        new_w = WIDTH;
        new_h = half_h;
    } else if (key == 0x11) {
        new_y = half_h;
        new_w = WIDTH;
        new_h = DESKTOP_H - half_h;
    } else {
        return;
    }

    w->x = new_x;
    w->y = new_y;
    w->w = clamp_int(new_w, MIN_WIN_W, WIDTH);
    w->h = clamp_int(new_h, MIN_WIN_H, DESKTOP_H);
    w->maximized = 0;
    w->dragging = 0;
    w->scroll_dragging = 0;
    w->resizing = 0;
    w->resize_dir = RESIZE_NONE;
    refresh_window_after_geometry_change(w);
}

void bring_to_front(int idx) {
    if (idx < 0 || idx >= MAX_WINDOWS) return;
    int p = -1; for(int i=0; i<MAX_WINDOWS; i++) if(z_order[i] == idx) { p = i; break; }
    if(p != -1) {
        for(int k=p; k<MAX_WINDOWS-1; k++) z_order[k] = z_order[k+1];
        z_order[MAX_WINDOWS-1] = idx;
        wins[idx].minimized = 0;
        active_win_idx = idx;
        wins[idx].taskbar_anim = 8;
        request_gui_redraw();
    }
}

void cycle_active_window() {
    int start_pos = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (z_order[i] == active_win_idx) {
            start_pos = i;
            break;
        }
    }

    if (start_pos == -1) {
        for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
            int cid = z_order[i];
            if (cid >= 0 && cid < MAX_WINDOWS && wins[cid].active && !wins[cid].minimized) {
                active_win_idx = cid;
                return;
            }
        }
        active_win_idx = -1;
        return;
    }

    int current = z_order[start_pos];
    if (current < 0 || current >= MAX_WINDOWS) {
        active_win_idx = -1;
        return;
    }

    // Rotate the current top window to the back so Shift+Tab cycles all jobs,
    // instead of bouncing between only the last two.
    for (int i = start_pos; i > 0; i--) {
        z_order[i] = z_order[i - 1];
    }
    z_order[0] = current;

    for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
        int cid = z_order[i];
        if (cid >= 0 && cid < MAX_WINDOWS && wins[cid].active && !wins[cid].minimized) {
            active_win_idx = cid;
            return;
        }
    }

    active_win_idx = -1;
}

void create_new_task() {
    for(int i=0; i<MAX_WINDOWS; i++) if(!wins[i].active) {
        int margin = WIDTH / 16;
        int title_y = HEIGHT / 10;
        int default_w = WIDTH - margin * 2;
        int default_h = DESKTOP_H - title_y - margin;
        if (default_w > 520) default_w = 520;
        if (default_h > 320) default_h = 320;
        if (default_w < MIN_WIN_W) default_w = WIDTH;
        if (default_h < MIN_WIN_H) default_h = DESKTOP_H;
        // 1. COMPLETELY ZERO OUT THE STRUCT PHYSICALLY
        memset(&wins[i], 0, sizeof(struct Window));
        
        reset_window(&wins[i], i);
        wins[i].active=1; wins[i].maximized=0; wins[i].minimized=0;
        wins[i].kind = WINDOW_KIND_TERMINAL;
        wins[i].x = clamp_int(margin + i * (margin / 2), 0, WIDTH - default_w);
        wins[i].y = clamp_int(title_y + i * (margin / 2), 0, DESKTOP_H - default_h);
        wins[i].w = default_w;
        wins[i].h = default_h;
        
        // 2. FORCE RESET CRITICAL COUNTERS TO ZERO
        wins[i].total_rows = 1;
        wins[i].cur_col = 12;
        wins[i].edit_len = 0;    // FIX: Reclaim from garbage values
        wins[i].cursor_pos = 0;
        wins[i].submit_locked = 0;
        wins[i].executing_cmd = 0;
        memset(wins[i].cmd_buf, 0, sizeof(wins[i].cmd_buf));
        lib_strcpy(wins[i].lines[0], PROMPT);

        terminal_env_bootstrap(&wins[i]);
        terminal_load_bashrc(&wins[i]);
        
        // 3. FINAL SANITY CHECK - Ensure bashrc didn't mess up state
        wins[i].edit_len = 0;
        wins[i].cursor_pos = 0;
        wins[i].cmd_buf[0] = '\0';
        
        set_window_title(&wins[i], i);
        seed_terminal_history(&wins[i]);
        bring_to_front(i); 
        active_win_idx = i;
        break;
    }
}

void draw_window(struct Window *w) {
    if (!w->active || w->minimized) return;
    int x = w->x, y = w->y, ww = w->w, wh = w->h;
    if (w->maximized) { x = 0; y = 0; ww = WIDTH; wh = DESKTOP_H; }
    for(int j=y; j<y+wh && j<DESKTOP_H; j++) { if(j<0) continue; for(int i=x; i<x+ww && i<WIDTH; i++) { if(i<0) continue; sheet_map[j*WIDTH + i] = w->id + 1; } }
    extern void draw_round_rect_fill(int,int,int,int,int,int);
    extern void draw_round_rect_wire(int,int,int,int,int,int);
    extern void draw_rect_fill(int,int,int,int,int);
    extern void draw_vertical_gradient(int,int,int,int,int,int);
    extern void draw_bevel_rect(int,int,int,int,int,int,int);
    draw_rect_fill(x + 5, y + 6, ww, wh, UI_C_SHADOW);
    draw_round_rect_fill(x, y, ww, wh, UI_RADIUS, COL_WIN_BG);
    draw_round_rect_wire(x, y, ww, wh, UI_RADIUS, COL_WIN_BRD);
    int color = (active_win_idx == w->id) ? COL_TITLE_ACT : COL_TITLE_INACT;
    int title_top = (active_win_idx == w->id) ? 3 : 2;
    int title_bot = (active_win_idx == w->id) ? 2 : 1;
    draw_round_rect_fill(x+2, y+2, ww-4, 26, UI_RADIUS-2, color); 
    draw_vertical_gradient(x + 3, y + 3, ww - 6, 10, title_top, title_bot);
    int title_max = (ww - 110) / 8;
    if (title_max < 6) title_max = 6;
    if (title_max > 60) title_max = 60;
    if (w->kind == WINDOW_KIND_TERMINAL) {
        char title[80];
        lib_strcpy(title, w->title);
        lib_strcat(title, " [x");
        char num[4];
        lib_itoa((uint32_t)terminal_font_scale(w), num);
        lib_strcat(title, num);
        lib_strcat(title, "]");
        title[title_max] = '\0';
        draw_text(x + 10, y + 6, title, UI_C_TEXT);
    } else if (w->kind == WINDOW_KIND_DEMO3D) {
        char title[80];
        char num[12];
        lib_strcpy(title, w->title);
        lib_strcat(title, " [");
        lib_itoa(demo3d_fps_value, num);
        lib_strcat(title, num);
        lib_strcat(title, " FPS]");
        title[title_max] = '\0';
        draw_text(x + 10, y + 6, title, UI_C_TEXT);
    } else {
        char title[80];
        lib_strcpy(title, w->title);
        title[title_max] = '\0';
        draw_text(x + 10, y + 6, title, UI_C_TEXT);
    }
    {
        int bx = x + ww - 34, by = y + 5;
        int hover_x = (gui_mx >= bx && gui_mx < bx + 22 && gui_my >= by && gui_my < by + 16);
        int close_fill = hover_x ? 4 : 2;
        int close_light = hover_x ? 15 : 14;
        int close_dark = hover_x ? 1 : 1;
        draw_bevel_rect(bx, by, 22, 16, close_fill, close_light, close_dark);
        draw_text(x + ww - 29, y + 7, "X", UI_C_TEXT);
    }
    {
        int bx = x + ww - 59, by = y + 5;
        int hover_x = (gui_mx >= bx && gui_mx < bx + 22 && gui_my >= by && gui_my < by + 16);
        int max_fill = hover_x ? 5 : 3;
        int max_light = hover_x ? 15 : 14;
        int max_dark = hover_x ? 1 : 1;
        draw_bevel_rect(bx, by, 22, 16, max_fill, max_light, max_dark);
        draw_text(x + ww - 54, y + 7, "M", UI_C_TEXT);
    }
    {
        int bx = x + ww - 84, by = y + 5;
        int hover_x = (gui_mx >= bx && gui_mx < bx + 22 && gui_my >= by && gui_my < by + 16);
        int min_fill = hover_x ? 4 : 1;
        int min_light = hover_x ? 15 : 14;
        int min_dark = hover_x ? 1 : 1;
        draw_bevel_rect(bx, by, 22, 16, min_fill, min_light, min_dark);
        draw_text(x + ww - 79, y + 7, "-", UI_C_TEXT);
    }
    if (w->kind == WINDOW_KIND_IMAGE) {
        int scale = w->image_scale > 0 ? w->image_scale : 1;
        int draw_w = w->image_w * scale;
        int draw_h = w->image_h * scale;
        int inner_w = ww - 20;
        int inner_h = wh - 36;
        if (inner_w < 0) inner_w = 0;
        if (inner_h < 0) inner_h = 0;
        if (draw_w > inner_w) draw_w = inner_w;
        if (draw_h > inner_h) draw_h = inner_h;
        if (draw_w > WIDTH) draw_w = WIDTH;
        if (draw_h > HEIGHT) draw_h = HEIGHT;
        memset(dither_err_r0, 0, sizeof(dither_err_r0));
        memset(dither_err_g0, 0, sizeof(dither_err_g0));
        memset(dither_err_b0, 0, sizeof(dither_err_b0));
        memset(dither_err_r1, 0, sizeof(dither_err_r1));
        memset(dither_err_g1, 0, sizeof(dither_err_g1));
        memset(dither_err_b1, 0, sizeof(dither_err_b1));
        if (scale == 1) {
            for (int py = 0; py < w->image_h; py++) {
                memset(dither_err_r1, 0, sizeof(dither_err_r1));
                memset(dither_err_g1, 0, sizeof(dither_err_g1));
                memset(dither_err_b1, 0, sizeof(dither_err_b1));
                for (int px = 0; px < w->image_w; px++) {
                    int r, g, b;
                    rgb_from_rgb565(w->image[py * IMG_MAX_W + px], &r, &g, &b);
                    r += dither_err_r0[px] >> 4;
                    g += dither_err_g0[px] >> 4;
                    b += dither_err_b0[px] >> 4;
                    if (r < 0) r = 0; else if (r > 255) r = 255;
                    if (g < 0) g = 0; else if (g > 255) g = 255;
                    if (b < 0) b = 0; else if (b > 255) b = 255;
                    int idx, qr, qg, qb;
                    quantize_rgb_to_palette(r, g, b, &idx, &qr, &qg, &qb);
                    int er = (r - qr), eg = (g - qg), eb = (b - qb);
                    dither_err_r0[px + 1] += er * 7;
                    dither_err_g0[px + 1] += eg * 7;
                    dither_err_b0[px + 1] += eb * 7;
                    dither_err_r1[px - (px > 0 ? 1 : 0)] += er * (px > 0 ? 3 : 0);
                    dither_err_g1[px - (px > 0 ? 1 : 0)] += eg * (px > 0 ? 3 : 0);
                    dither_err_b1[px - (px > 0 ? 1 : 0)] += eb * (px > 0 ? 3 : 0);
                    dither_err_r1[px] += er * 5;
                    dither_err_g1[px] += eg * 5;
                    dither_err_b1[px] += eb * 5;
                    dither_err_r1[px + 1] += er;
                    dither_err_g1[px + 1] += eg;
                    dither_err_b1[px + 1] += eb;
                    putpixel(x + 10 + px, y + 34 + py, idx);
                }
                memcpy(dither_err_r0, dither_err_r1, sizeof(dither_err_r0));
                memcpy(dither_err_g0, dither_err_g1, sizeof(dither_err_g0));
                memcpy(dither_err_b0, dither_err_b1, sizeof(dither_err_b0));
            }
        } else {
            for (int dy = 0; dy < draw_h; dy++) {
                memset(dither_err_r1, 0, sizeof(dither_err_r1));
                memset(dither_err_g1, 0, sizeof(dither_err_g1));
                memset(dither_err_b1, 0, sizeof(dither_err_b1));
                int fy = dy * 256 / scale;
                int sy0 = fy >> 8;
                int wy = fy & 0xFF;
                int sy1 = sy0 + 1;
                if (sy0 < 0) sy0 = 0;
                if (sy0 >= w->image_h) sy0 = w->image_h - 1;
                if (sy1 >= w->image_h) sy1 = w->image_h - 1;
                for (int dx = 0; dx < draw_w; dx++) {
                    int fx = dx * 256 / scale;
                    int sx0 = fx >> 8;
                    int wx = fx & 0xFF;
                    int sx1 = sx0 + 1;
                    if (sx0 < 0) sx0 = 0;
                    if (sx0 >= w->image_w) sx0 = w->image_w - 1;
                    if (sx1 >= w->image_w) sx1 = w->image_w - 1;

                    uint16_t c00 = w->image[sy0 * IMG_MAX_W + sx0];
                    uint16_t c10 = w->image[sy0 * IMG_MAX_W + sx1];
                    uint16_t c01 = w->image[sy1 * IMG_MAX_W + sx0];
                    uint16_t c11 = w->image[sy1 * IMG_MAX_W + sx1];
                    int r00, g00, b00, r10, g10, b10, r01, g01, b01, r11, g11, b11;
                    rgb_from_rgb565(c00, &r00, &g00, &b00);
                    rgb_from_rgb565(c10, &r10, &g10, &b10);
                    rgb_from_rgb565(c01, &r01, &g01, &b01);
                    rgb_from_rgb565(c11, &r11, &g11, &b11);

                    int w00 = (256 - wx) * (256 - wy);
                    int w10 = wx * (256 - wy);
                    int w01 = (256 - wx) * wy;
                    int w11 = wx * wy;
                    int r = (r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11) >> 16;
                    int g = (g00 * w00 + g10 * w10 + g01 * w01 + g11 * w11) >> 16;
                    int b = (b00 * w00 + b10 * w10 + b01 * w01 + b11 * w11) >> 16;
                    r += dither_err_r0[dx] >> 4;
                    g += dither_err_g0[dx] >> 4;
                    b += dither_err_b0[dx] >> 4;
                    if (r < 0) r = 0; else if (r > 255) r = 255;
                    if (g < 0) g = 0; else if (g > 255) g = 255;
                    if (b < 0) b = 0; else if (b > 255) b = 255;
                    int idx, qr, qg, qb;
                    quantize_rgb_to_palette(r, g, b, &idx, &qr, &qg, &qb);
                    int er = (r - qr), eg = (g - qg), eb = (b - qb);
                    dither_err_r0[dx + 1] += er * 7;
                    dither_err_g0[dx + 1] += eg * 7;
                    dither_err_b0[dx + 1] += eb * 7;
                    dither_err_r1[dx - (dx > 0 ? 1 : 0)] += er * (dx > 0 ? 3 : 0);
                    dither_err_g1[dx - (dx > 0 ? 1 : 0)] += eg * (dx > 0 ? 3 : 0);
                    dither_err_b1[dx - (dx > 0 ? 1 : 0)] += eb * (dx > 0 ? 3 : 0);
                    dither_err_r1[dx] += er * 5;
                    dither_err_g1[dx] += eg * 5;
                    dither_err_b1[dx] += eb * 5;
                    dither_err_r1[dx + 1] += er;
                    dither_err_g1[dx + 1] += eg;
                    dither_err_b1[dx + 1] += eb;
                    putpixel(x + 10 + dx, y + 34 + dy, idx);
                }
                memcpy(dither_err_r0, dither_err_r1, sizeof(dither_err_r0));
                memcpy(dither_err_g0, dither_err_g1, sizeof(dither_err_g0));
                memcpy(dither_err_b0, dither_err_b1, sizeof(dither_err_b0));
            }
        }
        return;
    }
    if (w->kind == WINDOW_KIND_EDITOR) {
        editor_render(w, x, y, ww, wh);
        return;
    } else if (w->kind == WINDOW_KIND_JIT_DEBUGGER) {
        draw_jit_debugger_content(w, x, y, ww, wh);
        return;
    } else if (w->kind == WINDOW_KIND_GBEMU) {
        draw_gbemu_content(w, x, y, ww, wh);
        return;
    }
    if (w->kind == WINDOW_KIND_FPS_GAME) {
        draw_fps_content(w, x, y, ww, wh);
        return;
    }
    if (w->kind == WINDOW_KIND_DEMO3D) {
        draw_demo3d_content(w, x, y, ww, wh);
        return;
    }
    if (w->kind == WINDOW_KIND_NETSURF) {
        // 導覽列 UI
        draw_rect_fill(x, y + 30, ww, 34, UI_C_PANEL);
        draw_rect_fill(x, y + 64, ww, 1, UI_C_BORDER);
        draw_bevel_rect(x + 10, y + 38, 24, 20, (w->ns_history_pos > 0 ? 3 : 1), 14, 1);
        draw_text(x + 16, y + 40, "<", 0);
        draw_bevel_rect(x + 40, y + 38, 24, 20, (w->ns_history_pos < w->ns_history_count - 1 ? 3 : 1), 14, 1);
        draw_text(x + 46, y + 40, ">", 0);
        draw_bevel_rect(x + 75, y + 38, ww - 85, 20, 0, 15, 0);
        const char *bar_url = (w->ns_target_url[0] ? w->ns_target_url : w->ns_url);
        draw_text(x + 80, y + 40, bar_url, UI_C_TEXT_DIM);
        if (w->ns_input_active) {
            int cur_x = x + 80 + strlen(bar_url) * 8;
            if (cur_x < x + ww - 10) draw_rect_fill(cur_x, y + 40, 2, 14, 0);
        }
        netsurf_render_frame(w, x, y, ww, wh);
        return;
    }
    int rv = terminal_visible_rows(w);
    int line_h = terminal_line_h(w);
    int char_w = terminal_char_w(w);
    int char_h = terminal_char_h(w);
    int scale = terminal_font_scale(w);
    int ssr = 0, ssc = 0, ser = 0, sec = 0;
    int show_sel = (w->kind == WINDOW_KIND_TERMINAL && terminal_has_nonempty_selection(w));
    if (show_sel) terminal_selection_normalized(w, &ssr, &ssc, &ser, &sec);
    for (int i = 0; i < rv; i++) {
        int idx = w->v_offset + i; if (idx < w->total_rows) {
            if (show_sel && idx >= ssr && idx <= ser) {
                int c0 = (idx == ssr) ? ssc : 0;
                int c1 = (idx == ser) ? sec : line_text_len(w->lines[idx]);
                int max_cols = terminal_visible_cols(w);
                if (c0 < 0) c0 = 0;
                if (c1 < c0) c1 = c0;
                if (c0 > max_cols) c0 = max_cols;
                if (c1 > max_cols) c1 = max_cols;
                if (c1 > c0) {
                    draw_rect_fill(x + 10 + c0 * char_w, y + 40 + (i * line_h), (c1 - c0) * char_w, line_h, UI_C_SELECTION);
                }
            }
            if (w->kind == WINDOW_KIND_TERMINAL) {
                int tc = (w->lines[idx][0] == 'd') ? COL_DIR : COL_TEXT;
                char clipped[COLS];
                int max_cols = terminal_visible_cols(w);
                int j = 0;
                while (j < max_cols && w->lines[idx][j]) {
                    clipped[j] = w->lines[idx][j];
                    j++;
                }
                clipped[j] = '\0';
                draw_text_scaled(x + 10, y + 40 + (i * line_h), clipped, tc, scale);
            }
        }
    }
    if (active_win_idx == w->id &&
        w->total_rows > 0 &&
        ((((uint32_t)sys_now()) / 500U) & 1U) == 0U) {
        int cx = x + 10 + (w->cur_col * char_w), cy = y + 40 + ((w->total_rows - 1 - w->v_offset) * line_h);
        if (cx < x + ww - 15 && cy < y + wh - 10 && cy > y + 30) draw_rect_fill(cx, cy, char_w, char_h, COL_TEXT);
    }
    if (w->total_rows > rv) {
        int sx = x + ww - 12, sy = y + 28, sh = wh - 30;
        draw_rect_fill(sx, sy, 8, sh, UI_C_BORDER); 
        int th = (rv * sh) / w->total_rows; if (th < 15) th = 15;
        int ms = terminal_scroll_max(w);
        int ty = sy;
        if (ms > 0 && sh > th) ty = sy + (w->v_offset * (sh - th)) / ms;
        draw_rect_fill(sx, ty, 8, th, UI_C_SCROLL_THUMB);
    }
}

void draw_taskbar() {
    for(int j=DESKTOP_H; j<GUI_VISIBLE_H; j++) for(int i=0; i<WIDTH; i++) sheet_map[j*WIDTH + i] = TASKBAR_ID;
    extern void draw_rect_fill(int,int,int,int,int);
    extern void draw_vertical_gradient(int,int,int,int,int,int);
    extern void draw_bevel_rect(int,int,int,int,int,int,int);
    int root_hover = (gui_mx >= 8 && gui_mx < 80 && gui_my >= DESKTOP_H + 4 && gui_my < DESKTOP_H + 26);
    draw_bevel_rect(0, DESKTOP_H, WIDTH, TASKBAR_H, UI_C_PANEL, UI_C_TEXT_MUTED, UI_C_PANEL_DEEP);
    draw_vertical_gradient(1, DESKTOP_H + 1, WIDTH - 2, TASKBAR_H - 2, UI_C_PANEL_LIGHT, UI_C_PANEL_DARK);
    draw_hline(0, DESKTOP_H + 1, WIDTH, UI_C_TEXT_MUTED);
    draw_hline(0, DESKTOP_H + TASKBAR_H - 2, WIDTH, UI_C_PANEL_DEEP);
    if (root_hover) {
        draw_bevel_rect(7, DESKTOP_H + 3, 74, 24, UI_C_PANEL_LIGHT, UI_C_TEXT_MUTED, UI_C_PANEL_DEEP);
        draw_hline(8, DESKTOP_H + 3, 72, UI_C_TEXT_DIM);
    } else {
        draw_bevel_rect(8, DESKTOP_H + 4, 72, 22, UI_C_PANEL, UI_C_TEXT_MUTED, UI_C_PANEL_DEEP);
    }
    draw_text(18, DESKTOP_H + 8, "[ROOT]", UI_C_TEXT);
    for (int i = 0; i < MAX_WINDOWS; i++) if (wins[i].active) {
        int tx = taskbar_button_x(i), c = UI_C_TEXT;
        extern void draw_round_rect_wire(int,int,int,int,int,int);
        int hover = (gui_mx >= tx && gui_mx < tx + TASKBAR_BTN_W && gui_my >= DESKTOP_H + 5 && gui_my < DESKTOP_H + 25);
        int by = DESKTOP_H + 5;
        int fill = UI_C_PANEL, light = UI_C_TEXT_DIM, dark = UI_C_PANEL_DEEP;
        if (active_win_idx == i) {
            fill = UI_C_PANEL_ACTIVE;
            light = UI_C_TEXT;
            dark = UI_C_PANEL_DEEP;
        }
        if (hover) {
            by -= 1;
            fill = UI_C_PANEL_HOVER;
            light = UI_C_TEXT;
            dark = UI_C_PANEL_DEEP;
        }
        draw_bevel_rect(tx, by, TASKBAR_BTN_W, 20, fill, light, dark);
        draw_round_rect_wire(tx, DESKTOP_H + 5, TASKBAR_BTN_W, 20, 5, c);
        char label[10];
        int j = 0;
        while (j < 7 && wins[i].title[j]) {
            label[j] = wins[i].title[j];
            j++;
        }
        label[j] = '\0';
        draw_text(tx + 6, DESKTOP_H + 9, label, UI_C_SHADOW);
        draw_text(tx + 5, DESKTOP_H + 8, label, c);
    }
    {
        char status[24];
        status[0] = '\0';
        if (active_win_idx >= 0 && active_win_idx < MAX_WINDOWS &&
            wins[active_win_idx].active && wins[active_win_idx].kind == WINDOW_KIND_TERMINAL &&
            wins[active_win_idx].shell_status[0]) {
            lib_strcpy(status, wins[active_win_idx].shell_status);
        } else {
            for (int i = 0; i < MAX_WINDOWS; i++) {
                if (wins[i].active && wins[i].kind == WINDOW_KIND_TERMINAL && wins[i].shell_status[0]) {
                    lib_strcpy(status, wins[i].shell_status);
                    break;
                }
            }
        }
        if (status[0]) {
            status[12] = '\0';
            draw_bevel_rect(WIDTH - 176, DESKTOP_H + 4, 68, 22, UI_C_PANEL_LIGHT, UI_C_TEXT, UI_C_PANEL_DEEP);
            draw_text(WIDTH - 169, DESKTOP_H + 8, status, UI_C_SHADOW);
            draw_text(WIDTH - 170, DESKTOP_H + 8, status, UI_C_TEXT);
        }
    }
    char clock[32];
    format_clock(clock);
    int clock_px = 19 * 8;
    int clock_x = WIDTH - clock_px - 12;
    if (clock_x < 0) clock_x = 0;
    draw_bevel_rect(clock_x - 7, DESKTOP_H + 4, clock_px + 14, 22, UI_C_PANEL_LIGHT, UI_C_TEXT, UI_C_PANEL_DEEP);
    draw_text(clock_x + 1, DESKTOP_H + 8, clock, UI_C_SHADOW);
    draw_text(clock_x, DESKTOP_H + 8, clock, UI_C_TEXT);
}

void gui_task(void) {
    extern void vga_update();
    extern void virtio_input_poll();
    for(int i=0; i<MAX_WINDOWS; i++) { reset_window(&wins[i], i); z_order[i] = i; }
#ifdef FPGA_MINIMAL
    create_new_task();
    request_gui_redraw();
#endif
    lib_printf("[BOOT] gui_task start active_win_idx=%d\n", active_win_idx);
    int last_mx = gui_mx, last_my = gui_my;
    int last_cursor_mode = -1;
    int last_text_cursor_blink = -1;
    uint32_t last_gui_frame_ms = 0;
    uint32_t last_cursor_frame_ms = 0;
    uint32_t last_pointer_motion_ms = 0;
    uint32_t last_taskbar_clock_ms = 0;
    int pending_text_cursor_blink = 0;
    while (1) {
        int redraw_needed = 0;
        int partial_flush_active = 0;
        int terminal_edit_queued = 0;
        int terminal_fast_flush = 0;
        int has_demo3d = 0;
        release_finished_app_commands();
        gui_cursor_mode = CURSOR_NORMAL;
        virtio_input_poll();
        if (active_window_valid() && wins[active_win_idx].kind == WINDOW_KIND_TERMINAL) {
            struct Window *aw = &wins[active_win_idx];
            int injected = 0;
            for (int i = 0; i < 128; i++) {
                int ch = uart_input_pop();
                if (ch < 0) break;
                window_input_push(aw, (char)ch);
                injected = 1;
            }
            if (injected) {
                aw->mailbox = 1;
                wake_terminal_worker_for_window(active_win_idx);
                redraw_needed = 1;
                partial_flush_active = 1;
            }
        }
        if (gui_shortcut_new_task) { gui_shortcut_new_task = 0; create_new_task(); redraw_needed = 1; }
        if (gui_shortcut_close_task) { gui_shortcut_close_task = 0; if (active_win_idx != -1) close_window(active_win_idx); redraw_needed = 1; }
        if (gui_shortcut_switch_task) { gui_shortcut_switch_task = 0; cycle_active_window(); redraw_needed = 1; }

        // --- NetSurf Input Dispatch START ---
        if (active_window_valid()) {
            struct Window *aw = &wins[active_win_idx];
            if (aw->kind == WINDOW_KIND_NETSURF) {
                extern void netsurf_handle_input(struct Window *w, int mx, int my, int clicked);
                netsurf_handle_input(aw, gui_mx, gui_my, gui_clicked);
                if (aw->ns_input_active) gui_key = 0; // Hijack keys for URL bar
            }
        }
        // --- NetSurf Input Dispatch END ---

        if (gui_click_pending) {
            gui_click_pending = 0; 
            redraw_needed = 1;
            int local_mx = gui_mx, local_my = gui_my;
            if (local_mx >= 0 && local_mx < WIDTH && local_my >= 0 && local_my < GUI_VISIBLE_H) {
                int tid = sheet_map[local_my * WIDTH + local_mx];
                if (tid == TASKBAR_ID) {
                    if (local_mx < 100) create_new_task();
                    else { for(int i=0; i<MAX_WINDOWS; i++) { int tx = taskbar_button_x(i); if(wins[i].active && local_mx >= tx && local_mx < tx + TASKBAR_BTN_W) { bring_to_front(i); wins[i].minimized = 0; break; } } }
                } else if (tid > 0 && tid <= MAX_WINDOWS) {
                    int idx = tid - 1; bring_to_front(idx);
                    struct Window *w = &wins[idx]; int ww = w->maximized ? WIDTH : w->w;
                    int wx = w->maximized ? 0 : w->x, wy = w->maximized ? 0 : w->y;
                    int resize_dir = hit_resize_zone(w, local_mx, local_my);
                    if (resize_dir != RESIZE_NONE) {
                        gui_cursor_mode = cursor_mode_for_resize_dir(resize_dir);
                        begin_resize(w, resize_dir, local_mx, local_my);
                    } else if (local_my < wy + 30) {
                        if (local_mx > wx + ww - 40) { close_window(idx); }
                        else if (local_mx > wx + ww - 75) {
                            if(!w->maximized) {
                                w->prev_x=w->x; w->prev_y=w->y; w->prev_w=w->w; w->prev_h=w->h; w->maximized=1;
                                w->dragging = 0; w->scroll_dragging = 0; w->resizing = 0; w->resize_dir = RESIZE_NONE;
                                if (w->kind == WINDOW_KIND_NETSURF) { w->v_offset = 0; w->ns_h_offset = 0; w->ns_input_active = 0; w->ns_resize_pending = 1; w->ns_resize_last_ms = sys_now(); extern void netsurf_invalidate_layout(int win_id); netsurf_invalidate_layout(idx); }
                            } else {
                                w->x=w->prev_x; w->y=w->prev_y; w->w=w->prev_w; w->h=w->prev_h; w->maximized=0;
                                w->dragging = 0; w->scroll_dragging = 0; w->resizing = 0; w->resize_dir = RESIZE_NONE;
                                if (w->kind == WINDOW_KIND_NETSURF) { w->v_offset = 0; w->ns_h_offset = 0; w->ns_input_active = 0; w->ns_resize_pending = 1; w->ns_resize_last_ms = sys_now(); extern void netsurf_invalidate_layout(int win_id); netsurf_invalidate_layout(idx); }
                            }
                        } else if (local_mx > wx + ww - 110) { w->minimized = 1; active_win_idx = -1; }
                        else {
                            static uint32_t last_click_ms = 0;
                            static int last_click_idx = -1;
                            uint32_t now = sys_now();
                            if (last_click_idx == idx && (now - last_click_ms) < 400) {
                                if (!w->maximized) {
                                    w->prev_x = w->x; w->prev_y = w->y; w->prev_w = w->w; w->prev_h = w->h; w->maximized = 1;
                                    w->dragging = 0; w->scroll_dragging = 0; w->resizing = 0; w->resize_dir = RESIZE_NONE;
                                    if (w->kind == WINDOW_KIND_NETSURF) {
 w->v_offset = 0; w->ns_h_offset = 0; w->ns_input_active = 0; w->ns_resize_pending = 1; w->ns_resize_last_ms = sys_now(); extern void netsurf_invalidate_layout(int win_id); netsurf_invalidate_layout(idx); }
                                } else {
                                    w->x = w->prev_x; w->y = w->prev_y; w->w = w->prev_w; w->h = w->prev_h; w->maximized = 0;
                                    w->dragging = 0; w->scroll_dragging = 0; w->resizing = 0; w->resize_dir = RESIZE_NONE;
                                    if (w->kind == WINDOW_KIND_NETSURF) {
 w->v_offset = 0; w->ns_h_offset = 0; w->ns_input_active = 0; w->ns_resize_pending = 1; w->ns_resize_last_ms = sys_now(); extern void netsurf_invalidate_layout(int win_id); netsurf_invalidate_layout(idx); }
                                }
                                last_click_ms = 0;
                                w->dragging = 0;
                            } else {
                                last_click_ms = now;
                                last_click_idx = idx;
                                if (!w->maximized) { w->dragging = 1; w->drag_off_x = local_mx - w->x; w->drag_off_y = local_my - w->y; }
                            }
                        }
                    } else if (local_mx > wx + ww - 15) { w->scroll_dragging = 1; }
                    else if (w->kind == WINDOW_KIND_DEMO3D) {
                        w->demo_dragging = 1;
                        w->demo_drag_last_mx = local_mx;
                        w->demo_drag_last_my = local_my;
                        w->demo_auto_spin = 0;
                    } else if (w->kind == WINDOW_KIND_JIT_DEBUGGER) {
                        jit_debugger_handle_mouse_down(w, local_mx, local_my,
                                                       wx, wy, ww,
                                                       w->maximized ? DESKTOP_H : w->h);
                    } else if (w->kind == WINDOW_KIND_TERMINAL) {
                        int row, col;
                        if (terminal_mouse_to_cell(w, local_mx, local_my, &row, &col)) {
                            w->selecting = 1;
                            w->has_selection = 1;
                            w->sel_start_row = w->sel_end_row = row;
                            w->sel_start_col = w->sel_end_col = col;
                        } else {
                            terminal_clear_selection(w);
                        }
                    }
                } else active_win_idx = -1;
            }
        }
        if (gui_right_click_pending) {
            gui_right_click_pending = 0;
            redraw_needed = 1;
            if (active_window_valid()) {
                struct Window *aw = &wins[active_win_idx];
                if (aw->kind == WINDOW_KIND_TERMINAL || aw->kind == WINDOW_KIND_EDITOR) {
                    terminal_paste_clipboard(aw);
                }
            }
        }
        if (!gui_clicked) {
            for(int i=0; i<MAX_WINDOWS; i++) {
                if (wins[i].kind == WINDOW_KIND_NETSURF && wins[i].ns_resize_pending) {
                    if ((uint32_t)(sys_now() - wins[i].ns_resize_last_ms) >= 120U) {
                        extern int netsurf_refresh_current_view(int win_id);
                        netsurf_refresh_current_view(i);
                        wins[i].ns_resize_pending = 0;
                    }
                }
                if (wins[i].kind == WINDOW_KIND_TERMINAL && wins[i].selecting && wins[i].has_selection) {
                    terminal_copy_selection(&wins[i]);
                }
                wins[i].dragging = 0;
                wins[i].scroll_dragging = 0;
                wins[i].demo_dragging = 0;
                wins[i].debug_drag_split = 0;
                wins[i].resizing = 0;
                wins[i].resize_dir = RESIZE_NONE;
                wins[i].selecting = 0;
            }
        }
        else if (active_window_valid()) {
            struct Window *aw = &wins[active_win_idx];
            if (aw->resizing) {
                gui_cursor_mode = cursor_mode_for_resize_dir(aw->resize_dir);
                apply_resize(aw, gui_mx, gui_my);
                redraw_needed = 1;
            } else if (aw->dragging) {
                aw->x = clamp_int(gui_mx - aw->drag_off_x, 0, WIDTH - aw->w);
                aw->y = clamp_int(gui_my - aw->drag_off_y, 0, DESKTOP_H - aw->h);
                redraw_needed = 1;
            }
            else if (aw->scroll_dragging) {
                int wy = aw->maximized ? 0 : aw->y, wh = aw->maximized ? DESKTOP_H : aw->h;
                int sy = wy + 28, sh = wh - 30;
                int ms = terminal_scroll_max(aw);
                if (ms > 0 && sh > 0) {
                    aw->v_offset = ((gui_my - sy) * ms) / sh;
                    terminal_clamp_v_offset(aw);
                }
                redraw_needed = 1;
            } else if (aw->kind == WINDOW_KIND_DEMO3D && aw->demo_dragging) {
                int dx = gui_mx - aw->demo_drag_last_mx;
                int dy = gui_my - aw->demo_drag_last_my;
                if (dx != 0 || dy != 0) {
                    aw->demo_angle = (aw->demo_angle - dx) % 360;
                    if (aw->demo_angle < 0) aw->demo_angle += 360;
                    aw->demo_pitch = (aw->demo_pitch + dy) % 360;
                    if (aw->demo_pitch < 0) aw->demo_pitch += 360;
                    aw->demo_drag_last_mx = gui_mx;
                    aw->demo_drag_last_my = gui_my;
                    redraw_needed = 1;
                }
            } else if (aw->kind == WINDOW_KIND_JIT_DEBUGGER && aw->debug_drag_split) {
                int wx = aw->maximized ? 0 : aw->x;
                int wy = aw->maximized ? 0 : aw->y;
                int ww = aw->maximized ? WIDTH : aw->w;
                int wh = aw->maximized ? DESKTOP_H : aw->h;
                jit_debugger_handle_mouse_drag(aw, gui_mx, gui_my, wx, wy, ww, wh);
                redraw_needed = 1;
            } else if (aw->kind == WINDOW_KIND_TERMINAL && aw->selecting) {
                int row, col;
                if (terminal_mouse_to_cell(aw, gui_mx, gui_my, &row, &col)) {
                    aw->sel_end_row = row;
                    aw->sel_end_col = col;
                    redraw_needed = 1;
                }
            }
        }
        if (!active_window_valid()) {
            active_win_idx = -1;
        }
        if (gui_cursor_mode == CURSOR_NORMAL && !gui_clicked) {
            for (int i = MAX_WINDOWS - 1; i >= 0; i--) {
                int idx = z_order[i];
                if (idx < 0 || idx >= MAX_WINDOWS) continue;
                if (!wins[idx].active || wins[idx].minimized) continue;
                int dir = hit_resize_zone(&wins[idx], gui_mx, gui_my);
                if (dir != RESIZE_NONE) {
                    gui_cursor_mode = cursor_mode_for_resize_dir(dir);
                    break;
                }
            }
        }
        if (gui_wheel != 0 && active_window_valid()) {
            struct Window *aw = &wins[active_win_idx];
            if (aw->kind == WINDOW_KIND_IMAGE) {
                if (gui_wheel > 0) resize_image_window(aw, aw->image_scale + gui_wheel);
                else if (gui_wheel < 0) resize_image_window(aw, aw->image_scale + gui_wheel);
            } else if (aw->kind == WINDOW_KIND_DEMO3D && !gui_ctrl_pressed) {
                if (gui_wheel > 0) aw->demo_dist -= 20;
                else if (gui_wheel < 0) aw->demo_dist += 20;
                if (aw->demo_dist < 120) aw->demo_dist = 120;
                if (aw->demo_dist > 2000) aw->demo_dist = 2000;
                gui_wheel = 0;
            } else if (aw->kind == WINDOW_KIND_TERMINAL && gui_ctrl_pressed) {
                if (gui_wheel > 0) resize_terminal_font(aw, aw->term_font_scale + 1);
                else if (gui_wheel < 0) resize_terminal_font(aw, aw->term_font_scale - 1);
            } else if (aw->kind == WINDOW_KIND_JIT_DEBUGGER) {
                int wx = aw->maximized ? 0 : aw->x;
                int wy = aw->maximized ? 0 : aw->y;
                int ww = aw->maximized ? WIDTH : aw->w;
                int wh = aw->maximized ? DESKTOP_H : aw->h;
                jit_debugger_handle_wheel(aw, gui_mx, gui_my, wx, wy, ww, wh, gui_wheel, gui_ctrl_pressed);
            } else {
                aw->v_offset -= gui_wheel;
                terminal_clamp_v_offset(aw);
            }
            gui_wheel = 0;
            redraw_needed = 1;
        }
        int key_batch = 0;
        while (active_window_valid() && key_batch < 16 && gui_key_pull_next()) {
            key_batch++;
            int key_consumed = 0;
            if (gui_ctrl_pressed && (gui_key == 0x10 || gui_key == 0x11 || gui_key == 0x12 || gui_key == 0x13)) {
                snap_window_by_key(active_win_idx, gui_key);
                gui_key = 0;
                key_consumed = 1;
            } else if (gui_ctrl_pressed && (gui_key == 'm' || gui_key == 'M')) {
                maximize_window(active_win_idx);
                gui_key = 0;
                key_consumed = 1;
            } else if (gui_ctrl_pressed && (gui_key == 'n' || gui_key == 'N')) {
                restore_window(active_win_idx);
                gui_key = 0;
                key_consumed = 1;
            } else if (gui_ctrl_pressed && (gui_key == 'b' || gui_key == 'B')) {
                minimize_window(active_win_idx);
                gui_key = 0;
                key_consumed = 1;
            } else if (wins[active_win_idx].kind == WINDOW_KIND_DEMO3D) {
                handle_demo3d_input(&wins[active_win_idx], &gui_key);
            } else if (wins[active_win_idx].kind == WINDOW_KIND_FPS_GAME) {
                handle_fps_input(&wins[active_win_idx], &gui_key, &redraw_needed);
            } else if (wins[active_win_idx].kind == WINDOW_KIND_GBEMU) {
                handle_gbemu_input(&wins[active_win_idx], &gui_key);
            } else if (wins[active_win_idx].kind == WINDOW_KIND_JIT_DEBUGGER) {
                jit_debugger_handle_key(&wins[active_win_idx], (char)gui_key);
                gui_key = 0;
                key_consumed = 1;
            } else if (wins[active_win_idx].kind == WINDOW_KIND_EDITOR &&
                       !(gui_ctrl_pressed && (gui_key == 'v' || gui_key == 'V' || gui_key == 22)) &&
                       gui_key != 3) {
                editor_handle_key(&wins[active_win_idx], (char)gui_key);
                gui_key = 0;
                key_consumed = 1;
            }
            if (key_consumed) {
                gui_key = 0;
            } else if (gui_key == 3) { // Ctrl+C
                CTRLDBG_PRINTF("[CTRLDBG] gui win=%d exec=%d wget=%d jit=%d\n",
                               active_win_idx,
                               wins[active_win_idx].executing_cmd,
                               wins[active_win_idx].waiting_wget,
                               os_jit_owner_active(wins[active_win_idx].id));
                if (wins[active_win_idx].kind == WINDOW_KIND_TERMINAL &&
                    (wins[active_win_idx].submit_locked ||
                     wins[active_win_idx].executing_cmd ||
                     wins[active_win_idx].waiting_wget ||
                     os_jit_owner_active(wins[active_win_idx].id))) {
                    // 如果正在執行命令，執行中斷
                    broadcast_ctrl_c();
                    gui_key = 0;
                } else if (wins[active_win_idx].kind == WINDOW_KIND_TERMINAL &&
                           wins[active_win_idx].has_selection &&
                           wins[active_win_idx].edit_len == 0) {
                    // 沒有正在輸入命令時，Ctrl+C 才作為複製選取
                    terminal_copy_selection(&wins[active_win_idx]);
                    gui_key = 0;
                } else if (wins[active_win_idx].kind == WINDOW_KIND_TERMINAL) {
                    // 其他情況：清空輸入
                    wins[active_win_idx].submit_locked = 0;
                    wins[active_win_idx].cancel_requested = 0;
                    clear_window_input_queue(&wins[active_win_idx]);
                    clear_prompt_input(&wins[active_win_idx]);
                    if (wins[active_win_idx].total_rows > 0) redraw_prompt_line(&wins[active_win_idx], wins[active_win_idx].total_rows - 1);
                    gui_key = 0;
                }
            } else if ((gui_ctrl_pressed && (gui_key == 'v' || gui_key == 'V')) || gui_key == 22) {
                // 支援所有類型視窗貼上
                terminal_paste_clipboard(&wins[active_win_idx]);
                gui_key = 0;
            } else if (wins[active_win_idx].kind == WINDOW_KIND_TERMINAL && gui_ctrl_pressed &&
                       (gui_key == '+' || gui_key == '=' || gui_key == '-' || gui_key == '0')) {
                if (gui_key == '+' || gui_key == '=') resize_terminal_font(&wins[active_win_idx], wins[active_win_idx].term_font_scale + 1);
                else if (gui_key == '-') resize_terminal_font(&wins[active_win_idx], wins[active_win_idx].term_font_scale - 1);
                else resize_terminal_font(&wins[active_win_idx], 1);
                gui_key = 0;
            } else {
                if (wins[active_win_idx].kind == WINDOW_KIND_TERMINAL) {
                    char term_key = gui_key;
                    struct Window *tw = &wins[active_win_idx];
                    window_input_push(tw, gui_key);
#ifdef FPGA_MINIMAL
                    if (gui_terminal_fast_edit_key(tw, term_key)) {
                        handle_window_mailbox(tw);
                        terminal_fast_flush = 1;
                    } else
#endif
                    {
                        tw->mailbox = 1;
                        wake_terminal_worker_for_window(active_win_idx);
                    }
                    partial_flush_active = 1;
                    if (term_key != 10 && term_key != 3 && !terminal_fast_flush) terminal_edit_queued = 1;
                }
                gui_key = 0;
            }
            redraw_needed = 1;
        }
        if (gui_key != 0 && !active_window_valid()) { gui_key = 0; }
#ifdef FPGA_MINIMAL
        if (terminal_edit_queued) {
            task_os();
        }
        if (terminal_fast_flush && active_window_valid() && wins[active_win_idx].kind == WINDOW_KIND_TERMINAL) {
            gui_fast_flush_terminal_window(&wins[active_win_idx], gui_mx, gui_my, gui_cursor_mode);
            last_mx = gui_mx;
            last_my = gui_my;
            last_cursor_mode = gui_cursor_mode;
            last_cursor_frame_ms = sys_now();
            gui_redraw_needed = 0;
            lib_delay(0);
            task_os();
            continue;
        }
#endif
        if ((wget_job.active || wget_job.request_pending || (wget_job.done && wget_job.success && wget_job.save_pending)) &&
            wget_job.owner_win_id >= 0 && wget_job.owner_win_id < MAX_WINDOWS &&
            wins[wget_job.owner_win_id].active && wins[wget_job.owner_win_id].kind == WINDOW_KIND_TERMINAL &&
            wget_job.progress_row < ROWS && wget_progress_dirty()) {
            set_wget_progress_line(&wins[wget_job.owner_win_id], wget_job.progress_row);
            redraw_needed = 1;
        }
        if (gbemu_has_active_window()) {
            gbemu_sync_gamepad();
            if (gbemu_tick_active(50000)) redraw_needed = 1;
        }
        int cursor_dirty = 0;
        if (gui_mx != last_mx || gui_my != last_my) {
            cursor_dirty = 1;
        }
        uint32_t ui_now = sys_now();
        if (cursor_dirty) last_pointer_motion_ms = ui_now;
        int pointer_busy = cursor_dirty ||
            (last_pointer_motion_ms != 0 &&
             (uint32_t)(ui_now - last_pointer_motion_ms) < 80U);
        int taskbar_clock_due = 0;
        if (last_taskbar_clock_ms == 0) {
            last_taskbar_clock_ms = ui_now;
        } else if ((uint32_t)(ui_now - last_taskbar_clock_ms) >= 1000U) {
            taskbar_clock_due = 1;
        }
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (wins[i].active && !wins[i].minimized && wins[i].kind == WINDOW_KIND_DEMO3D) {
                has_demo3d = 1;
                break;
            }
        }
        if (has_demo3d) {
            uint32_t now = sys_now();
            if (demo3d_last_frame_ms == 0 || now - demo3d_last_frame_ms >= 16U) {
                for (int i = 0; i < MAX_WINDOWS; i++) {
                    if (!wins[i].active || wins[i].minimized || wins[i].kind != WINDOW_KIND_DEMO3D) continue;
                    if (wins[i].demo_auto_spin) {
                        wins[i].demo_angle = (wins[i].demo_angle + 3) % 360;
                    }
                }
                demo3d_last_frame_ms = now;
                demo3d_fps_frames++;
                if (demo3d_fps_last_ms == 0) demo3d_fps_last_ms = now;
                if (now - demo3d_fps_last_ms >= 1000U) {
                    uint32_t elapsed = now - demo3d_fps_last_ms;
                    if (elapsed == 0) elapsed = 1;
                    demo3d_fps_value = (demo3d_fps_frames * 1000U) / elapsed;
                    demo3d_fps_frames = 0;
                    demo3d_fps_last_ms = now;
                }
                redraw_needed = 1;
            }
        }
        if (gui_cursor_mode != last_cursor_mode) {
            cursor_dirty = 1;
        }
        if (active_window_valid() &&
            ((wins[active_win_idx].kind == WINDOW_KIND_TERMINAL && wins[active_win_idx].total_rows > 0) ||
             wins[active_win_idx].kind == WINDOW_KIND_EDITOR ||
             wins[active_win_idx].kind == WINDOW_KIND_JIT_DEBUGGER)) {
            int blink = (int)(((ui_now / 500U) & 1U) == 0U);
            if (pending_text_cursor_blink && !pointer_busy) {
                redraw_needed = 1;
                partial_flush_active = 1;
                last_text_cursor_blink = blink;
                pending_text_cursor_blink = 0;
            } else if (blink != last_text_cursor_blink) {
#ifdef FPGA_MINIMAL
                if (pointer_busy && !redraw_needed && !gui_redraw_needed) {
                    pending_text_cursor_blink = 1;
                } else
#endif
                {
                    redraw_needed = 1;
                    partial_flush_active = 1;
                    last_text_cursor_blink = blink;
                    pending_text_cursor_blink = 0;
                }
            }
        } else if (last_text_cursor_blink != -1) {
            redraw_needed = 1;
            last_text_cursor_blink = -1;
            pending_text_cursor_blink = 0;
        }
        if (!redraw_needed && !gui_redraw_needed && taskbar_clock_due && any_window_active()) {
#ifdef FPGA_MINIMAL
            uint32_t frame_now = sys_now();
            gui_update_taskbar_with_cursor(gui_mx, gui_my, gui_cursor_mode);
            last_taskbar_clock_ms = frame_now;
            last_cursor_frame_ms = frame_now;
            last_mx = gui_mx;
            last_my = gui_my;
            last_cursor_mode = gui_cursor_mode;
            lib_delay(0);
            task_os();
            continue;
#else
            redraw_needed = 1;
#endif
        }
        if (!redraw_needed && !gui_redraw_needed && cursor_dirty && any_window_active()) {
#ifdef FPGA_MINIMAL
            uint32_t frame_now = sys_now();
            if (last_cursor_frame_ms != 0 && (uint32_t)(frame_now - last_cursor_frame_ms) < 8U) {
                task_os();
                continue;
            }
            last_cursor_frame_ms = frame_now;
            vga_present_cursor(gui_mx, gui_my, gui_cursor_mode, GUI_VISIBLE_H);
            last_mx = gui_mx;
            last_my = gui_my;
            last_cursor_mode = gui_cursor_mode;
            lib_delay(0);
            task_os();
            continue;
#else
            redraw_needed = 1;
#endif
        }
        if (redraw_needed || (gui_redraw_needed && any_window_active())) {
            uint32_t frame_now = sys_now();
            if (last_gui_frame_ms != 0 && (uint32_t)(frame_now - last_gui_frame_ms) < 16U) {
                gui_redraw_needed = 1;
                task_os();
                continue;
            }
            last_gui_frame_ms = frame_now;
            static int boot_redraw_logged = 0;
            if (!boot_redraw_logged) boot_redraw_logged = 1;
            gui_redraw_needed = 0;
            extern void draw_rect_fill(int,int,int,int,int);
            extern void draw_vertical_gradient(int,int,int,int,int,int);

            // 優化：如果 GameBoy 視窗在跑，不畫背景梯度，改用簡單的填色或直接跳過
            if (!gbemu_has_active_window()) {
                draw_vertical_gradient(0, 0, WIDTH, DESKTOP_H, UI_C_DESKTOP, UI_C_PANEL_DARK);
            } else {
                // 如果需要背景，只畫工作列以外的部分，或者乾脆不畫（保留上一幀內容）
                // 這裡我們只填色，不跑梯度計算
                draw_rect_fill(0, 0, WIDTH, DESKTOP_H, UI_C_DESKTOP);
            }

            draw_rect_fill(0, DESKTOP_H, WIDTH, TASKBAR_H, UI_C_DESKTOP);
            memset(sheet_map, 0, sizeof(sheet_map));
            for(int i=0; i<MAX_WINDOWS; i++) draw_window(&wins[z_order[i]]);
            draw_taskbar();
            last_taskbar_clock_ms = frame_now;
#ifndef FPGA_MINIMAL
            draw_cursor(gui_mx, gui_my, 14, gui_cursor_mode);
#endif
#ifdef FPGA_MINIMAL
            if (partial_flush_active && active_window_valid()) {
                gui_update_window_area_with_cursor(&wins[active_win_idx], last_mx, last_my, gui_mx, gui_my);
            } else {
                vga_update();
                vga_present_cursor(gui_mx, gui_my, gui_cursor_mode, GUI_VISIBLE_H);
            }
            last_cursor_frame_ms = frame_now;
#else
            vga_update();
#endif
            last_mx = gui_mx;
            last_my = gui_my;
            last_cursor_mode = gui_cursor_mode;
        }
        lib_delay(0);
#ifdef FPGA_MINIMAL
        task_os();
#else
        if (need_resched) { need_resched = 0; task_os(); }
#endif
    }
}
