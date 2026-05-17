#include "jit_debugger.h"
#include "tcc_runtime.h"
#include "lib.h"
#include "string.h"
#include "vga.h"
#include "user_wget.h"

extern int jit_debug_snapshot(char *out, int out_max);
extern int jit_debug_snapshot_prev(char *out, int out_max);
extern int jit_debug_continue(char *out, int out_max);
extern int jit_debug_step(char *out, int out_max);
extern int jit_debug_step_instruction(char *out, int out_max);
extern int jit_debug_add_breakpoint(const char *file, int line, char *out, int out_max);
extern int jit_debug_set_watch(uint32_t addr, uint32_t len, char *out, int out_max);
extern int jit_debug_watch_dump(char *out, int out_max);
extern int jit_debug_asm_dump(char *out, int out_max);
extern int jit_debug_current_line(void);
extern const char *jit_debug_current_file(void);
extern int os_jit_cancel_by_owner(int owner_win_id);
extern volatile int gui_redraw_needed;
extern int gui_prev_regs_pressed;

#define DBG_SPLIT_NONE 0
#define DBG_SPLIT_VERT 1
#define DBG_SPLIT_HORZ 2
#define DBG_SPLIT_BOTH 3

#define DBG_PANE_NONE 0
#define DBG_PANE_REGS 1
#define DBG_PANE_MEM 2
#define DBG_PANE_SOURCE 3
#define DBG_PANE_CONSOLE 4

static void dbg_set_status(struct Window *w, const char *msg) {
    if (!w) return;
    if (!msg) msg = "";
    lib_strcpy(w->editor_status, msg);
}

static const char *dbg_basename(const char *path) {
    const char *base = path;
    if (!path) return "";
    while (*path) {
        if (*path == '/' || *path == '\\') base = path + 1;
        path++;
    }
    return base;
}

static int dbg_path_is_synthetic(const char *path) {
    const char *base = dbg_basename(path);
    int len;
    if (!base || !base[0]) return 1;
    if (strcmp(base, "<string>") == 0) return 1;
    len = (int)strlen(base);
    if (len >= 2 && base[0] == '<' && base[len - 1] == '>') return 1;
    return 0;
}

static void dbg_append(char *out, int out_max, const char *s) {
    int n;
    int i = 0;
    if (!out || out_max <= 0 || !s) return;
    n = strlen(out);
    while (s[i] && n < out_max - 1) out[n++] = s[i++];
    out[n] = '\0';
}

static char *dbg_skip_space(char *p) {
    while (p && (*p == ' ' || *p == '\t')) p++;
    return p;
}

static int dbg_parse_hex_u32(const char *s, uint32_t *out, const char **endp) {
    uint32_t v = 0;
    int any = 0;
    if (!s || !out) return -1;
    while (*s == ' ' || *s == '\t') s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        int d = -1;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        v = (v << 4) | (uint32_t)d;
        any = 1;
        s++;
    }
    if (!any) return -1;
    *out = v;
    if (endp) *endp = s;
    return 0;
}

static void dbg_draw_text_slice(int x, int y, const char *s, int color, int max_chars, int scroll_x) {
    char buf[160];
    int i = 0;
    if (!s) s = "";
    if (scroll_x < 0) scroll_x = 0;
    while (*s && scroll_x > 0) {
        s++;
        scroll_x--;
    }
    if (max_chars <= 0) return;
    if (max_chars >= (int)sizeof(buf)) max_chars = (int)sizeof(buf) - 1;
    while (s[i] && i < max_chars) {
        buf[i] = s[i];
        i++;
    }
    buf[i] = '\0';
    draw_text(x, y, buf, color);
}

static void dbg_draw_text_trunc(int x, int y, const char *s, int color, int max_chars) {
    dbg_draw_text_slice(x, y, s, color, max_chars, 0);
}

static int dbg_clamp_int(int v, int lo, int hi);

static void dbg_draw_scrollbars(int x, int y, int w, int h, int scroll_x, int scroll_y, int content_cols, int content_lines) {
    int line_h = 14;
    int visible_lines = h / line_h;
    int visible_cols = w / 8;
    if (w < 24 || h < 24) return;
    if (visible_lines < 1) visible_lines = 1;
    if (visible_cols < 1) visible_cols = 1;
    if (content_lines > visible_lines) {
        int max_scroll = content_lines - visible_lines;
        int track_h = h - 8;
        int thumb_h = (track_h * visible_lines) / content_lines;
        int thumb_y;
        if (max_scroll < 1) max_scroll = 1;
        scroll_y = dbg_clamp_int(scroll_y, 0, max_scroll);
        if (thumb_h < 10) thumb_h = 10;
        if (thumb_h > track_h) thumb_h = track_h;
        thumb_y = y + 4 + ((track_h - thumb_h) * scroll_y) / max_scroll;
        draw_vline(x + w - 7, y + 4, track_h, UI_C_BORDER);
        draw_rect_fill(x + w - 9, thumb_y, 4, thumb_h, UI_C_TEXT_DIM);
    }
    if (content_cols > visible_cols) {
        int max_scroll = content_cols - visible_cols;
        int track_w = w - 12;
        int thumb_w = (track_w * visible_cols) / content_cols;
        int thumb_x;
        if (max_scroll < 1) max_scroll = 1;
        scroll_x = dbg_clamp_int(scroll_x, 0, max_scroll);
        if (thumb_w < 14) thumb_w = 14;
        if (thumb_w > track_w) thumb_w = track_w;
        thumb_x = x + 4 + ((track_w - thumb_w) * scroll_x) / max_scroll;
        draw_hline(x + 4, y + h - 7, track_w, UI_C_BORDER);
        draw_rect_fill(thumb_x, y + h - 9, thumb_w, 4, UI_C_TEXT_DIM);
    }
}

static void dbg_draw_snapshot_lines(int x, int y, int y_end, const char *snap, int max_chars, int scroll_x, int scroll_y) {
    char line[96];
    int li = 0;
    int row_y = y;
    int line_h = 14;
    int line_no = 0;

    for (int i = 0; snap && snap[i] && row_y < y_end; i++) {
        if (snap[i] == '\n' || li >= (int)sizeof(line) - 1) {
            line[li] = '\0';
            if (line_no >= scroll_y) {
                int changed = (line[0] == '!');
                dbg_draw_text_slice(x, row_y, line, changed ? 12 : UI_C_TEXT, max_chars, scroll_x);
                row_y += line_h;
            }
            line_no++;
            li = 0;
            if (snap[i] == '\n') continue;
        }
        line[li++] = snap[i];
    }
    if (li > 0 && row_y < y_end) {
        line[li] = '\0';
        if (line_no >= scroll_y) dbg_draw_text_slice(x, row_y, line, UI_C_TEXT, max_chars, scroll_x);
    }
}

static void dbg_draw_asm_watch_lines(int x, int y, int y_end, int pane_w,
                                     const char *text, int max_chars, int scroll_x, int scroll_y) {
    char line[128];
    int li = 0;
    int row_y = y;
    int line_h = 14;
    int line_no = 0;

    for (int i = 0; text && text[i] && row_y < y_end; i++) {
        if (text[i] == '\n' || li >= (int)sizeof(line) - 1) {
            int is_current;
            int is_range;
            line[li] = '\0';
            if (line_no >= scroll_y) {
                is_current = (line[0] == '=' && line[1] == '>');
                is_range = (line[0] == '*' && line[1] == ' ');
                if (is_current) {
                    draw_rect_fill(x - 2, row_y - 1, pane_w - 8, line_h - 2, UI_C_SELECTION);
                    dbg_draw_text_slice(x, row_y, line, UI_C_TEXT, max_chars, scroll_x);
                } else if (is_range) {
                    dbg_draw_text_slice(x, row_y, line, UI_C_TEXT_MUTED, max_chars, scroll_x);
                } else {
                    dbg_draw_text_slice(x, row_y, line, UI_C_TEXT, max_chars, scroll_x);
                }
                row_y += line_h;
            }
            line_no++;
            li = 0;
            if (text[i] == '\n') continue;
        }
        line[li++] = text[i];
    }
    if (li > 0 && row_y < y_end) {
        line[li] = '\0';
        if (line_no >= scroll_y) dbg_draw_text_slice(x, row_y, line, UI_C_TEXT, max_chars, scroll_x);
    }
}

static void dbg_console_clear(struct Window *w) {
    if (!w) return;
    w->app_stdout_len = 0;
    w->app_stdout[0] = '\0';
}

static void dbg_console_append_char(struct Window *w, char ch) {
    int len;
    if (!w) return;
    if (ch == '\r') return;
    len = w->app_stdout_len;
    if (len < 0 || len >= (int)sizeof(w->app_stdout)) {
        len = 0;
        w->app_stdout[0] = '\0';
    }
    if (len >= (int)sizeof(w->app_stdout) - 2) {
        int drop = 0;
        while (w->app_stdout[drop] && w->app_stdout[drop] != '\n') drop++;
        if (w->app_stdout[drop] == '\n') drop++;
        memmove(w->app_stdout, w->app_stdout + drop, len - drop + 1);
        len = strlen(w->app_stdout);
    }
    if (len < (int)sizeof(w->app_stdout) - 1) {
        w->app_stdout[len++] = ch;
        w->app_stdout[len] = '\0';
        w->app_stdout_len = len;
        w->debug_console_scroll_y = 9999;
    }
}

void jit_debugger_console_write(int owner_win_id, const char *s) {
    (void)owner_win_id;
    if (!s) return;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!wins[i].active || wins[i].kind != WINDOW_KIND_JIT_DEBUGGER) continue;
        for (const char *p = s; *p; p++) dbg_console_append_char(&wins[i], *p);
    }
    gui_redraw_needed = 1;
}

static void dbg_draw_console(struct Window *w, int x, int y, int y_end, int max_chars) {
    int line_h = 14;
    int max_lines = (y_end - y) / line_h;
    int line_count = 0;
    int start_line = 0;
    int current = 0;
    int row_y = y;
    if (!w || max_lines <= 0) return;
    if (max_chars > 0) max_chars -= 2;
    if (max_chars < 4) return;
    for (int i = 0; w->app_stdout[i]; i++) {
        if (w->app_stdout[i] == '\n') line_count++;
    }
    if (w->app_stdout[0]) line_count++;
    start_line = w->debug_console_scroll_y;
    if (line_count > max_lines && start_line > line_count - max_lines) start_line = line_count - max_lines;
    if (start_line < 0) start_line = 0;
    w->debug_console_scroll_y = start_line;
    {
        char line[128];
        int li = 0;
        for (int i = 0;; i++) {
            char ch = w->app_stdout[i];
            if (ch == '\n' || ch == '\0' || li >= (int)sizeof(line) - 1) {
                line[li] = '\0';
                if (current >= start_line && row_y + line_h <= y_end) {
                    dbg_draw_text_slice(x, row_y, line, UI_C_TEXT, max_chars, w->debug_console_scroll_x);
                    row_y += line_h;
                }
                current++;
                li = 0;
                if (ch == '\0') break;
                if (ch == '\n') continue;
            }
            if (li < max_chars + w->debug_console_scroll_x + 2 &&
                li < (int)sizeof(line) - 1) {
                line[li++] = ch;
            }
        }
    }
}

static int dbg_clamp_int(int v, int lo, int hi) {
    if (hi < lo) hi = lo;
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void dbg_layout(struct Window *w, int x, int y, int ww, int wh,
                       int *area_x, int *area_y, int *area_w, int *area_h,
                       int *split_x, int *split_y, int *status_y) {
    int top = y + 36;
    int sy = y + wh - 18;
    int aw = ww - 12;
    int ah = sy - top - 2;
    int sx;
    int sp_y;
    int min_left;
    int min_right;
    int min_top;
    int min_bottom;

    if (aw < 1) aw = 1;
    if (ah < 1) ah = 1;
    min_left = aw >= 620 ? 240 : aw / 3;
    min_right = aw >= 620 ? 260 : aw / 3;
    min_top = ah >= 260 ? 100 : ah / 3;
    min_bottom = ah >= 260 ? 100 : ah / 3;
    if (min_left < 80) min_left = 80;
    if (min_right < 80) min_right = 80;
    if (min_top < 50) min_top = 50;
    if (min_bottom < 50) min_bottom = 50;
    if (aw < min_left + min_right + 8) {
        min_left = aw / 2;
        min_right = aw - min_left;
    }
    if (ah < min_top + min_bottom + 8) {
        min_top = ah / 2;
        min_bottom = ah - min_top;
    }
    sx = w && w->debug_split_x > 0 ? w->debug_split_x : aw / 2;
    sp_y = w && w->debug_split_y > 0 ? w->debug_split_y : ah / 2;
    sx = dbg_clamp_int(sx, min_left, aw - min_right);
    sp_y = dbg_clamp_int(sp_y, min_top, ah - min_bottom);

    if (area_x) *area_x = x + 6;
    if (area_y) *area_y = top;
    if (area_w) *area_w = aw;
    if (area_h) *area_h = ah;
    if (split_x) *split_x = sx;
    if (split_y) *split_y = sp_y;
    if (status_y) *status_y = sy;
}

static int dbg_pane_at(struct Window *w, int mx, int my, int x, int y, int ww, int wh) {
    int area_x, area_y, area_w, area_h, split_x, split_y, status_y;
    int sx, sy;
    if (!w) return DBG_PANE_NONE;
    dbg_layout(w, x, y, ww, wh, &area_x, &area_y, &area_w, &area_h, &split_x, &split_y, &status_y);
    (void)status_y;
    if (mx < area_x || mx >= area_x + area_w || my < area_y || my >= area_y + area_h) return DBG_PANE_NONE;
    sx = area_x + split_x;
    sy = area_y + split_y;
    if (mx < sx && my < sy) return DBG_PANE_REGS;
    if (mx < sx && my >= sy) return DBG_PANE_MEM;
    if (mx >= sx && my < sy) return DBG_PANE_SOURCE;
    return DBG_PANE_CONSOLE;
}

static int dbg_pane_visible_cols(struct Window *w, int pane, int x, int y, int ww, int wh) {
    int area_x, area_y, area_w, area_h, split_x, split_y, status_y;
    int cols;
    if (!w) return 1;
    dbg_layout(w, x, y, ww, wh, &area_x, &area_y, &area_w, &area_h, &split_x, &split_y, &status_y);
    (void)area_x; (void)area_y; (void)area_h; (void)split_y; (void)status_y;
    if (pane == DBG_PANE_REGS || pane == DBG_PANE_MEM) {
        cols = (split_x - 22) / 8;
    } else {
        cols = (area_w - split_x - 58) / 8;
    }
    if (cols < 1) cols = 1;
    return cols;
}

static int dbg_pane_visible_lines(struct Window *w, int pane, int x, int y, int ww, int wh) {
    int area_x, area_y, area_w, area_h, split_x, split_y, status_y;
    int lines;
    if (!w) return 1;
    dbg_layout(w, x, y, ww, wh, &area_x, &area_y, &area_w, &area_h, &split_x, &split_y, &status_y);
    (void)area_x; (void)area_y; (void)area_w; (void)area_h;
    if (pane == DBG_PANE_REGS) {
        lines = (split_y - 36) / 14;
    } else if (pane == DBG_PANE_MEM) {
        lines = (status_y - (area_y + split_y) - 36) / 14;
    } else if (pane == DBG_PANE_SOURCE) {
        lines = (split_y - 32) / 14;
    } else {
        lines = (status_y - (area_y + split_y) - 34) / 14;
    }
    if (lines < 1) lines = 1;
    return lines;
}

static int dbg_text_line_count(const char *s) {
    int lines = 0;
    if (!s || !s[0]) return 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == '\n') lines++;
    }
    return lines + 1;
}

static int dbg_text_max_line_len(const char *s) {
    int max_len = 0;
    int len = 0;
    if (!s) return 0;
    for (int i = 0;; i++) {
        char ch = s[i];
        if (ch == '\n' || ch == '\0') {
            if (len > max_len) max_len = len;
            len = 0;
            if (ch == '\0') break;
        } else {
            len++;
        }
    }
    return max_len;
}

static int dbg_console_max_line_len(struct Window *w) {
    if (!w) return 0;
    return dbg_text_max_line_len(w->app_stdout);
}

static int dbg_console_line_count(struct Window *w) {
    if (!w) return 0;
    return dbg_text_line_count(w->app_stdout);
}

static void dbg_build_asm_watch_pane(char *out, int out_max) {
    char asm_buf[OUT_BUF_SIZE];
    char watch_buf[OUT_BUF_SIZE];
    if (!out || out_max <= 0) return;
    out[0] = '\0';
    if (jit_debug_asm_dump(asm_buf, sizeof(asm_buf)) != 0) {
        lib_strcpy(asm_buf, "asm: no paused jit code.");
    }
    if (jit_debug_watch_dump(watch_buf, sizeof(watch_buf)) != 0) {
        lib_strcpy(watch_buf, "watch: none");
    }
    dbg_append(out, out_max, asm_buf);
    dbg_append(out, out_max, "\n");
    dbg_append(out, out_max, watch_buf);
}

static void dbg_build_regs_pane(char *out, int out_max) {
    if (!out || out_max <= 0) return;
    out[0] = '\0';
    if (gui_prev_regs_pressed && jit_debug_snapshot_prev(out, out_max) == 0) return;
    jit_debug_snapshot(out, out_max);
}

static int dbg_source_max_line_len(struct Window *w) {
    int max_len = 0;
    if (!w) return 0;
    for (int i = 0; i < w->editor_line_count; i++) {
        int len = strlen(w->editor_lines[i]);
        if (len > max_len) max_len = len;
    }
    return max_len;
}

static int dbg_pane_content_lines(struct Window *w, int pane) {
    char tmp[OUT_BUF_SIZE];
    if (!w) return 0;
    if (pane == DBG_PANE_REGS) {
        dbg_build_regs_pane(tmp, sizeof(tmp));
        if (!tmp[0]) return 0;
        return dbg_text_line_count(tmp);
    }
    if (pane == DBG_PANE_MEM) {
        dbg_build_asm_watch_pane(tmp, sizeof(tmp));
        return dbg_text_line_count(tmp);
    }
    if (pane == DBG_PANE_SOURCE) return w->editor_line_count;
    if (pane == DBG_PANE_CONSOLE) return dbg_console_line_count(w);
    return 0;
}

static int dbg_pane_content_cols(struct Window *w, int pane) {
    char tmp[OUT_BUF_SIZE];
    if (!w) return 0;
    if (pane == DBG_PANE_REGS) {
        dbg_build_regs_pane(tmp, sizeof(tmp));
        if (!tmp[0]) return 0;
        return dbg_text_max_line_len(tmp);
    }
    if (pane == DBG_PANE_MEM) {
        dbg_build_asm_watch_pane(tmp, sizeof(tmp));
        return dbg_text_max_line_len(tmp);
    }
    if (pane == DBG_PANE_SOURCE) return dbg_source_max_line_len(w);
    if (pane == DBG_PANE_CONSOLE) return dbg_console_max_line_len(w);
    return 0;
}

static int dbg_pane_max_scroll_x(struct Window *w, int pane, int x, int y, int ww, int wh) {
    int content_cols = dbg_pane_content_cols(w, pane);
    int visible_cols = dbg_pane_visible_cols(w, pane, x, y, ww, wh);
    int max_scroll = content_cols - visible_cols;
    return max_scroll > 0 ? max_scroll : 0;
}

static int dbg_pane_max_scroll_y(struct Window *w, int pane, int x, int y, int ww, int wh) {
    int content_lines = dbg_pane_content_lines(w, pane);
    int visible_lines = dbg_pane_visible_lines(w, pane, x, y, ww, wh);
    int max_scroll = content_lines - visible_lines;
    return max_scroll > 0 ? max_scroll : 0;
}

static void dbg_clamp_all_scroll(struct Window *w, int x, int y, int ww, int wh) {
    if (!w) return;
    w->debug_regs_scroll_x = dbg_clamp_int(w->debug_regs_scroll_x, 0, dbg_pane_max_scroll_x(w, DBG_PANE_REGS, x, y, ww, wh));
    w->debug_regs_scroll_y = dbg_clamp_int(w->debug_regs_scroll_y, 0, dbg_pane_max_scroll_y(w, DBG_PANE_REGS, x, y, ww, wh));
    w->debug_mem_scroll_x = dbg_clamp_int(w->debug_mem_scroll_x, 0, dbg_pane_max_scroll_x(w, DBG_PANE_MEM, x, y, ww, wh));
    w->debug_mem_scroll_y = dbg_clamp_int(w->debug_mem_scroll_y, 0, dbg_pane_max_scroll_y(w, DBG_PANE_MEM, x, y, ww, wh));
    w->debug_source_scroll_x = dbg_clamp_int(w->debug_source_scroll_x, 0, dbg_pane_max_scroll_x(w, DBG_PANE_SOURCE, x, y, ww, wh));
    w->debug_source_scroll_y = dbg_clamp_int(w->debug_source_scroll_y, 0, dbg_pane_max_scroll_y(w, DBG_PANE_SOURCE, x, y, ww, wh));
    w->debug_console_scroll_x = dbg_clamp_int(w->debug_console_scroll_x, 0, dbg_pane_max_scroll_x(w, DBG_PANE_CONSOLE, x, y, ww, wh));
    w->debug_console_scroll_y = dbg_clamp_int(w->debug_console_scroll_y, 0, dbg_pane_max_scroll_y(w, DBG_PANE_CONSOLE, x, y, ww, wh));
}

static int dbg_load_source_path(struct Window *w, const char *path) {
    unsigned char *buf = NULL;
    uint32_t size = 0;
    uint32_t p_bno = 0;
    char p_cwd[128];
    char leaf[20];
    char abs_path[128];

    if (!w || !path || !path[0]) return -1;
    if (resolve_editor_target(w, path, &p_bno, p_cwd, leaf) != 0 || !leaf[0]) {
        lib_printf("[JITDBG] source load resolve fail path=%s\n", path ? path : "(null)");
        return -1;
    }
    lib_printf("[JITDBG] source load resolve ok path=%s cwd=%s leaf=%s bno=%u\n",
               path, p_cwd[0] ? p_cwd : "/root", leaf, p_bno);
    if (load_file_bytes_alloc(w, path, &buf, &size) != 0 || !buf) {
        lib_printf("[JITDBG] source load read fail path=%s\n", path);
        return -1;
    }

    build_editor_path(abs_path, p_cwd, leaf);
    editor_load_bytes(w, buf, size);
    free(buf);
    buf = NULL;

    lib_strcpy(w->editor_name, leaf);
    lib_strcpy(w->editor_path, abs_path);
    lib_strcpy(w->debug_source_path, abs_path);
    lib_strcpy(w->editor_cwd, p_cwd[0] ? p_cwd : "/root");
    w->cwd_bno = p_bno ? p_bno : 1;
    w->editor_file_bno = 0;
    w->editor_file_size = size;
    w->editor_readonly = 1;
    lib_strcpy(w->title, "JIT Debugger: ");
    shorten_path_for_title(w->title + strlen(w->title), abs_path, 40);
    lib_printf("[JITDBG] source load ok path=%s abs=%s size=%u\n", path, abs_path, size);
    return 0;
}

static void dbg_reload_source_if_needed(struct Window *w) {
    const char *file = jit_debug_current_file();
    char fallback[128];
    const char *base;
    if (!w || !file || !file[0]) return;
    if (dbg_path_is_synthetic(file)) {
        lib_printf("[JITDBG] source reload skip synthetic=%s current=%s\n",
                   file,
                   w->debug_source_path[0] ? w->debug_source_path : "(none)");
        return;
    }
    if (strcmp(w->debug_source_path, file) == 0) return;
    lib_printf("[JITDBG] source reload want=%s current=%s entry=%s\n",
               file,
               w->debug_source_path[0] ? w->debug_source_path : "(none)",
               w->debug_entry_path[0] ? w->debug_entry_path : "(none)");
    if (dbg_load_source_path(w, file) == 0) return;

    base = dbg_basename(file);
    if (base[0] && strcmp(base, dbg_basename(w->debug_source_path)) == 0) {
        lib_printf("[JITDBG] source reload keep-current file=%s base=%s current=%s\n",
                   file, base, w->debug_source_path[0] ? w->debug_source_path : "(none)");
        return;
    }

    fallback[0] = '\0';
    if (w->debug_entry_path[0]) {
        const char *entry_base = dbg_basename(w->debug_entry_path);
        int cwd_len = (int)strlen(w->debug_entry_path) - (int)strlen(entry_base);
        if (cwd_len > 0) {
            if (cwd_len >= (int)sizeof(fallback)) cwd_len = (int)sizeof(fallback) - 1;
            memcpy(fallback, w->debug_entry_path, (size_t)cwd_len);
            fallback[cwd_len] = '\0';
            if (base[0]) lib_strcat(fallback, base);
        }
    }
    if (fallback[0]) lib_printf("[JITDBG] source reload fallback=%s base=%s\n", fallback, base[0] ? base : "(none)");
    if (fallback[0] && dbg_load_source_path(w, fallback) == 0) return;

    lib_printf("[JITDBG] source reload failed want=%s fallback=%s\n",
               file, fallback[0] ? fallback : "(none)");
    dbg_set_status(w, "debug source load failed");
}

void draw_jit_debugger_content(struct Window *w, int x, int y, int ww, int wh) {
    char snap[OUT_BUF_SIZE];
    char watch[OUT_BUF_SIZE];
    int area_x, area_y, area_w, area_h, split_x, split_y;
    int status_y;
    int right_x;
    int lower_y;
    int line_h = 14;
    int row_y;
    int current_line;
    int left_chars;
    int right_chars;

    if (!w) return;
    dbg_layout(w, x, y, ww, wh, &area_x, &area_y, &area_w, &area_h, &split_x, &split_y, &status_y);
    dbg_clamp_all_scroll(w, x, y, ww, wh);
    right_x = area_x + split_x;
    lower_y = area_y + split_y;

    draw_rect_fill(area_x, area_y, area_w, area_h, UI_C_PANEL);
    draw_vline(right_x, area_y + 2, area_h - 4, UI_C_BORDER);
    draw_hline(area_x + 2, lower_y, area_w - 4, UI_C_BORDER);
    draw_rect_fill(right_x - 2, lower_y - 2, 5, 5, UI_C_TEXT_DIM);
    draw_text(area_x + 8, area_y + 8, "Registers", UI_C_TEXT);
    draw_text(area_x + 8, lower_y + 8, "Asm / Watch", UI_C_TEXT);
    draw_text(right_x + 12, area_y + 8, "Source", UI_C_TEXT);
    draw_text(right_x + 12, lower_y + 8, "Console", UI_C_TEXT);

    dbg_reload_source_if_needed(w);
    dbg_build_regs_pane(snap, sizeof(snap));
    left_chars = (split_x - 22) / 8;
    if (left_chars < 20) left_chars = 20;
    dbg_draw_snapshot_lines(area_x + 8, area_y + 26, lower_y - 10, snap, left_chars,
                            w->debug_regs_scroll_x, w->debug_regs_scroll_y);
    dbg_draw_scrollbars(area_x + 4, area_y + 24, split_x - 12, lower_y - area_y - 32,
                        w->debug_regs_scroll_x, w->debug_regs_scroll_y,
                        dbg_text_max_line_len(snap), dbg_text_line_count(snap));

    dbg_build_asm_watch_pane(watch, sizeof(watch));
    dbg_draw_asm_watch_lines(area_x + 8, lower_y + 26, status_y - 10, split_x - 12,
                             watch, left_chars, w->debug_mem_scroll_x, w->debug_mem_scroll_y);
    dbg_draw_scrollbars(area_x + 4, lower_y + 24, split_x - 12, status_y - lower_y - 34,
                        w->debug_mem_scroll_x, w->debug_mem_scroll_y,
                        dbg_text_max_line_len(watch), dbg_text_line_count(watch));

    row_y = area_y + 28;
    current_line = jit_debug_current_line();
    right_chars = (area_w - split_x - 58) / 8;
    if (right_chars < 20) right_chars = 20;
    {
        int source_base_line = w->editor_loaded_start_line + 1;
        int current_row = current_line - source_base_line;
        int visible_source_lines = (lower_y - 4 - row_y) / line_h;
        if (visible_source_lines < 1) visible_source_lines = 1;
        if (current_row >= 0 &&
            (current_row < w->debug_source_scroll_y ||
             current_row >= w->debug_source_scroll_y + visible_source_lines)) {
            w->debug_source_scroll_y = current_row - 2;
            if (w->debug_source_scroll_y < 0) w->debug_source_scroll_y = 0;
        }
    }
    if (w->debug_source_scroll_y < 0) w->debug_source_scroll_y = 0;
    if (w->debug_source_scroll_y >= w->editor_line_count) w->debug_source_scroll_y = w->editor_line_count > 0 ? w->editor_line_count - 1 : 0;
    w->debug_source_scroll_y = dbg_clamp_int(w->debug_source_scroll_y, 0,
                                             dbg_pane_max_scroll_y(w, DBG_PANE_SOURCE, x, y, ww, wh));
    for (int i = w->debug_source_scroll_y; i < w->editor_line_count && row_y < lower_y - 4; i++) {
        char num[12];
        int line_no = w->editor_loaded_start_line + i + 1;
        int is_current = (current_line > 0 && line_no == current_line);

        if (is_current) {
            draw_rect_fill(right_x + 8, row_y - 1, area_w - split_x - 16, line_h - 2, UI_C_SELECTION);
        }
        lib_itoa((uint32_t)line_no, num);
        draw_text(right_x + 12, row_y, num, is_current ? UI_C_TEXT : UI_C_TEXT_DIM);
        dbg_draw_text_slice(right_x + 48, row_y, w->editor_lines[i], UI_C_TEXT, right_chars, w->debug_source_scroll_x);
        row_y += line_h;
    }
    dbg_draw_scrollbars(right_x + 8, area_y + 24, area_w - split_x - 20, lower_y - area_y - 32,
                        w->debug_source_scroll_x, w->debug_source_scroll_y,
                        dbg_source_max_line_len(w), w->editor_line_count);

    dbg_draw_console(w, right_x + 12, lower_y + 26, status_y - 8, right_chars);
    dbg_draw_scrollbars(right_x + 8, lower_y + 24, area_w - split_x - 20, status_y - lower_y - 34,
                        w->debug_console_scroll_x, w->debug_console_scroll_y,
                        dbg_console_max_line_len(w), dbg_console_line_count(w));

    draw_rect_fill(x + 3, status_y, ww - 6, 14, UI_C_SELECTION);
    if (w->editor_mode == 2) {
        char cmd[COLS + 2];
        cmd[0] = ':';
        cmd[1] = '\0';
        lib_strcat(cmd, w->editor_cmd);
        draw_text(x + 10, status_y + 1, cmd, UI_C_TEXT);
        if (((sys_now() / 500U) & 1U) == 0U) {
            int cx = x + 18 + w->editor_cmd_len * 8;
            if (cx < x + ww - 10) draw_rect_fill(cx, status_y + 2, 8, 12, UI_C_TEXT);
        }
    } else {
        draw_text(x + 10, status_y + 1,
                  w->editor_status[0] ? w->editor_status : ":c continue  :q close",
                  UI_C_TEXT);
    }
}

int jit_debugger_handle_wheel(struct Window *w, int mx, int my, int x, int y, int ww, int wh, int wheel, int horizontal) {
    int pane;
    int delta;
    if (!w || wheel == 0) return 0;
    pane = dbg_pane_at(w, mx, my, x, y, ww, wh);
    if (pane == DBG_PANE_NONE) return 0;
    delta = wheel > 0 ? -3 : 3;
    if (horizontal) {
        int max_x = dbg_pane_max_scroll_x(w, pane, x, y, ww, wh);
        if (pane == DBG_PANE_REGS) w->debug_regs_scroll_x = dbg_clamp_int(w->debug_regs_scroll_x + delta, 0, max_x);
        else if (pane == DBG_PANE_MEM) w->debug_mem_scroll_x = dbg_clamp_int(w->debug_mem_scroll_x + delta, 0, max_x);
        else if (pane == DBG_PANE_SOURCE) w->debug_source_scroll_x = dbg_clamp_int(w->debug_source_scroll_x + delta, 0, max_x);
        else if (pane == DBG_PANE_CONSOLE) w->debug_console_scroll_x = dbg_clamp_int(w->debug_console_scroll_x + delta, 0, max_x);
    } else {
        int max_y = dbg_pane_max_scroll_y(w, pane, x, y, ww, wh);
        if (pane == DBG_PANE_REGS) w->debug_regs_scroll_y = dbg_clamp_int(w->debug_regs_scroll_y + delta, 0, max_y);
        else if (pane == DBG_PANE_MEM) w->debug_mem_scroll_y = dbg_clamp_int(w->debug_mem_scroll_y + delta, 0, max_y);
        else if (pane == DBG_PANE_SOURCE) w->debug_source_scroll_y = dbg_clamp_int(w->debug_source_scroll_y + delta, 0, max_y);
        else if (pane == DBG_PANE_CONSOLE) w->debug_console_scroll_y = dbg_clamp_int(w->debug_console_scroll_y + delta, 0, max_y);
    }
    return 1;
}

int jit_debugger_handle_mouse_down(struct Window *w, int mx, int my, int x, int y, int ww, int wh) {
    int area_x, area_y, area_w, area_h, split_x, split_y, status_y;
    int sx, sy;
    if (!w) return 0;
    dbg_layout(w, x, y, ww, wh, &area_x, &area_y, &area_w, &area_h, &split_x, &split_y, &status_y);
    (void)status_y;
    sx = area_x + split_x;
    sy = area_y + split_y;
    if (mx < area_x || mx >= area_x + area_w || my < area_y || my >= area_y + area_h) return 0;
    if (mx >= sx - 4 && mx <= sx + 4 && my >= sy - 4 && my <= sy + 4) {
        w->debug_drag_split = DBG_SPLIT_BOTH;
        return 1;
    }
    if (mx >= sx - 4 && mx <= sx + 4) {
        w->debug_drag_split = DBG_SPLIT_VERT;
        return 1;
    }
    if (my >= sy - 4 && my <= sy + 4) {
        w->debug_drag_split = DBG_SPLIT_HORZ;
        return 1;
    }
    return 0;
}

void jit_debugger_handle_mouse_drag(struct Window *w, int mx, int my, int x, int y, int ww, int wh) {
    int area_x, area_y, area_w, area_h, split_x, split_y, status_y;
    if (!w || !w->debug_drag_split) return;
    dbg_layout(w, x, y, ww, wh, &area_x, &area_y, &area_w, &area_h, &split_x, &split_y, &status_y);
    (void)status_y;
    if (w->debug_drag_split == DBG_SPLIT_VERT || w->debug_drag_split == DBG_SPLIT_BOTH) {
        w->debug_split_x = dbg_clamp_int(mx - area_x, 96, area_w - 96);
    }
    if (w->debug_drag_split == DBG_SPLIT_HORZ || w->debug_drag_split == DBG_SPLIT_BOTH) {
        w->debug_split_y = dbg_clamp_int(my - area_y, 72, area_h - 72);
    }
}

int open_jit_debugger_window(struct Window *term, const char *path) {
    uint32_t size = 0;
    uint32_t p_bno = 0;
    char p_cwd[128], leaf[20], abs_path[128];

    if (!term || !path || !*path) return -1;
    if (load_file_bytes(term, path, file_io_buf, WGET_MAX_FILE_SIZE, &size) != 0) return -1;
    if (resolve_editor_target(term, path, &p_bno, p_cwd, leaf) != 0 || !leaf[0]) return -1;
    build_editor_path(abs_path, p_cwd, leaf);

    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (wins[i].active) continue;
        reset_window(&wins[i], i);
        wins[i].active = 1;
        wins[i].kind = WINDOW_KIND_JIT_DEBUGGER;
        wins[i].maximized = 1;
        wins[i].x = 0;
        wins[i].y = 0;
        wins[i].w = WIDTH;
        wins[i].h = DESKTOP_H;
        lib_strcpy(wins[i].title, "JIT Debugger: ");
        shorten_path_for_title(wins[i].title + strlen(wins[i].title), abs_path, 40);
        lib_strcpy(wins[i].editor_path, abs_path);
        lib_strcpy(wins[i].debug_entry_path, abs_path);
        lib_strcpy(wins[i].debug_source_path, abs_path);
        lib_strcpy(wins[i].editor_cwd, p_cwd[0] ? p_cwd : "/root");
        wins[i].cwd_bno = p_bno ? p_bno : 1;
        editor_load_bytes(&wins[i], file_io_buf, size);
        wins[i].editor_mode = 0;
        wins[i].editor_cmd_len = 0;
        wins[i].editor_cmd[0] = '\0';
        wins[i].debug_split_x = 0;
        wins[i].debug_split_y = 0;
        wins[i].debug_drag_split = 0;
        dbg_console_clear(&wins[i]);
        dbg_set_status(&wins[i], ":b file.c line  :watch addr len  :si step  :c continue  :r rerun");
            bring_to_front(i);
            return i;
    }
    return -3;
}

int jit_debugger_exec_cmd(struct Window *term, char *arg, char *out, int out_max) {
    char abs_path[128];
    uint32_t p_bno = 0;
    char p_cwd[128], leaf[20];

    if (!term || !arg || !out || out_max <= 0) return 0;

    if ((strncmp(arg, "dbg", 3) == 0 && (arg[3] == '\0' || arg[3] == ' ')) ||
        (arg[0] == 'd' && (arg[1] == '\0' || arg[1] == ' '))) {
        arg += (arg[0] == 'd' && arg[1] != 'b') ? 1 : 3;
        while (*arg == ' ') arg++;
        if (*arg == '\0') {
            jit_debug_snapshot(out, out_max);
            return 1;
        }
        int dbg_win_id = open_jit_debugger_window(term, arg);
        if (dbg_win_id < 0) {
            lib_strcpy(out, "ERR: Debugger source open failed.");
            return 1;
        }
        if (resolve_editor_target(term, arg, &p_bno, p_cwd, leaf) != 0 || !leaf[0]) {
            lib_strcpy(out, "ERR: Debugger source path failed.");
            return 1;
        }
        build_editor_path(abs_path, p_cwd, leaf);
        appfs_set_cwd(term->cwd_bno, term->cwd);
        os_jit_run_bg_debug_file(abs_path, dbg_win_id, out, out_max);
        return 1;
    }

    if ((arg[0] == 'c' && (arg[1] == '\0' || arg[1] == ' ')) ||
        (strncmp(arg, "cont", 4) == 0 && (arg[4] == '\0' || arg[4] == ' '))) {
        jit_debug_continue(out, out_max);
        return 1;
    }

    return 0;
}

static void jit_debugger_apply_command(struct Window *w) {
    char msg[OUT_BUF_SIZE];
    if (!w) return;
    w->editor_cmd[w->editor_cmd_len] = '\0';
    if (w->editor_cmd_len > 0) {
        lib_strcpy(w->saved_cmd, w->editor_cmd);
    } else if (w->saved_cmd[0]) {
        lib_strcpy(w->editor_cmd, w->saved_cmd);
        w->editor_cmd_len = strlen(w->editor_cmd);
    }

    if (strcmp(w->editor_cmd, "q") == 0 || strcmp(w->editor_cmd, "q!") == 0) {
        close_window(w->id);
        return;
    }
    if (strcmp(w->editor_cmd, "cls") == 0 || strcmp(w->editor_cmd, "clear") == 0) {
        dbg_console_clear(w);
        w->debug_console_scroll_x = 0;
        w->debug_console_scroll_y = 0;
        dbg_set_status(w, "console cleared");
    } else if (strcmp(w->editor_cmd, "r") == 0 || strcmp(w->editor_cmd, "run") == 0) {
        const char *path = w->debug_entry_path[0] ? w->debug_entry_path : w->editor_path;
        dbg_console_clear(w);
        os_jit_cancel_by_owner(w->id);
        appfs_set_cwd(w->cwd_bno, w->editor_cwd[0] ? w->editor_cwd : "/root");
        os_jit_run_bg_debug_file(path, w->id, msg, sizeof(msg));
        dbg_set_status(w, msg);
    } else if (strcmp(w->editor_cmd, "c") == 0 || strcmp(w->editor_cmd, "cont") == 0) {
        jit_debug_continue(msg, sizeof(msg));
        dbg_set_status(w, msg);
    } else if (strcmp(w->editor_cmd, "si") == 0) {
        jit_debug_step_instruction(msg, sizeof(msg));
        dbg_set_status(w, msg);
    } else if (strcmp(w->editor_cmd, "ni") == 0 || strcmp(w->editor_cmd, "ns") == 0 ||
               strcmp(w->editor_cmd, "sn") == 0) {
        jit_debug_step(msg, sizeof(msg));
        dbg_set_status(w, msg);
    } else if (strncmp(w->editor_cmd, "b ", 2) == 0 || strncmp(w->editor_cmd, "break ", 6) == 0) {
        char *p = w->editor_cmd + (w->editor_cmd[0] == 'b' ? 1 : 5);
        char file[128];
        int fi = 0;
        int line = 0;
        p = dbg_skip_space(p);
        while (*p && *p != ' ' && *p != '\t' && fi < (int)sizeof(file) - 1) file[fi++] = *p++;
        file[fi] = '\0';
        p = dbg_skip_space(p);
        line = atoi(p);
        jit_debug_add_breakpoint(file, line, msg, sizeof(msg));
        dbg_set_status(w, msg);
    } else if (strncmp(w->editor_cmd, "watch ", 6) == 0 || strncmp(w->editor_cmd, "w ", 2) == 0) {
        char *p = w->editor_cmd + (w->editor_cmd[0] == 'w' && w->editor_cmd[1] == ' ' ? 1 : 5);
        uint32_t addr = 0;
        uint32_t len = 64;
        const char *endp = NULL;
        p = dbg_skip_space(p);
        if (dbg_parse_hex_u32(p, &addr, &endp) == 0) {
            p = (char *)endp;
            p = dbg_skip_space(p);
            if (*p) len = (uint32_t)atoi(p);
            jit_debug_set_watch(addr, len, msg, sizeof(msg));
        } else {
            lib_strcpy(msg, "usage: :watch <hex_addr> [len]");
        }
        dbg_set_status(w, msg);
    } else if (w->editor_cmd[0] == '\0') {
        dbg_set_status(w, ":b file.c line  :watch addr len  :si step  :c continue");
    } else {
        dbg_set_status(w, "unknown debugger command");
    }
    w->editor_mode = 0;
    w->editor_cmd_len = 0;
    w->editor_cmd[0] = '\0';
}

void jit_debugger_handle_key(struct Window *w, char key) {
    if (!w) return;
    if (w->editor_mode == 2) {
        if (key == 27) {
            w->editor_mode = 0;
            w->editor_cmd_len = 0;
            w->editor_cmd[0] = '\0';
            dbg_set_status(w, ":b file.c line  :watch addr len  :si step  :c continue");
            return;
        }
        if (key == 10 || key == '\r') {
            jit_debugger_apply_command(w);
            return;
        }
        if (key == 8 || key == 127) {
            if (w->editor_cmd_len > 0) w->editor_cmd_len--;
            w->editor_cmd[w->editor_cmd_len] = '\0';
            return;
        }
        if (key >= 32 && key < 127 && w->editor_cmd_len < COLS - 2) {
            w->editor_cmd[w->editor_cmd_len++] = key;
            w->editor_cmd[w->editor_cmd_len] = '\0';
        }
        return;
    }

    if (key == ':') {
        w->editor_mode = 2;
        w->editor_cmd_len = 0;
        w->editor_cmd[0] = '\0';
        return;
    }
    if (key == 10 || key == '\r') {
        if (w->saved_cmd[0]) {
            w->editor_cmd_len = 0;
            w->editor_cmd[0] = '\0';
            jit_debugger_apply_command(w);
        }
        return;
    }
}
