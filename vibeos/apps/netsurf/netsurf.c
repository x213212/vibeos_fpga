#include "user_internal.h"
#include <stdlib.h>
#include "hubbub/hubbub.h"
#include "hubbub/parser.h"

int netsurf_init_attempted[MAX_WINDOWS];
static hubbub_parser *netsurf_parsers[MAX_WINDOWS];

#define NS_MAX_LINKS 128
#define NS_MAX_IMAGE_W 1024
#define NS_MAX_IMAGE_H 1024
#define NS_DITHER_LINE_W (NS_MAX_IMAGE_W * 4 + 8)

struct ns_link {
    int start_line;
    int end_line;
    char href[128];
};

struct ns_click {
    int kind;
    int x;
    int y;
    int w;
    int h;
    int line;
    char href[160];
    char input_type[16];
    char input_value[128];
    char input_name[64];
};

struct ns_wrp_control {
    int kind;
    int x;
    int y;
    int w;
    int h;
    char type[16];
    char value[128];
    char name[64];
};

static struct ns_state {
    char raw[32768];
    int raw_len;
    char source[32768];
    int source_len;
    char lines[1024][256];
    int line_count;
    int max_line_len;
    uint8_t bg_color;
    uint8_t text_color;
    int in_script_tag;
    int in_style_tag;
    int in_pre_tag;
    int in_form_tag;
    int in_textarea_tag;
    int in_table_tag;
    int in_row_tag;
    int table_cell_count;
    int in_button_tag;
    int in_link_tag;
    int wrp_page;
    int pending_break;
    int layout_width;
    int layout_scale;
    int last_was_space;
    int cur_line_idx;
    int cur_line_len;
    int layout_raw_len;
    int has_run_test_button;
    int run_test_done;
    char current_link_href[128];
    int has_image;
    int image_loading;
    int image_ready;
    int image_line_idx;
    int image_w;
    int image_h;
    char image_url[160];
    char image_alt[80];
    char image_map_href[128];
    int image_draw_x;
    int image_draw_y;
    int image_draw_w;
    int image_draw_h;
    int current_link_start_line;
    int wrp_ctrl_count;
    struct ns_wrp_control wrp_ctrls[24];
    int click_count;
    struct ns_click clicks[128];
    int hover_click_idx;
    int active_ctrl_idx;
    int nav_reset_pending;       // 是否正等待新頁面資料以執行重置
    int waiting_for_id;          // 是否正等待新頁面的 Map ID
    int fav_active;              // 我的最愛選單是否開啟
    int mouse_down;
    
    // --- 操作隊列 (Action Queue) ---
    struct ns_queued_action {
        int mx, my;
        char kstr[32];
        char target[256];
        int is_click;
    } action_queue[32];
    int action_head, action_tail, action_count;

    uint16_t *image_pixels;      // 正在顯示的緩衝區
    struct ns_link links[NS_MAX_LINKS];
    int link_count;
} ns_states[MAX_WINDOWS];

static uint16_t *shared_back_buffer = NULL;

static hubbub_error token_handler(const hubbub_token *token, void *pw);
static void ns_reset_doc(struct ns_state *s, int keep_image);
static void ns_start_navigation(struct Window *w, const char *url);
static int ns_start_document_navigation(struct Window *w, const char *url);

#define NS_FAV_COUNT 8
static const char *ns_fav_titles[NS_FAV_COUNT] = {
    "68k News", "Hacker News", "DuckDuckGo", "FrogFind", "Lite Wiki", "Low-tech Mag", "Wttr.in", "Wikipedia"
};
static const char *ns_fav_urls[NS_FAV_COUNT] = {
    "http://68k.news",
    "https://news.ycombinator.com",
    "https://duckduckgo.com",
    "http://frogfind.com",
    "https://wikiless.org",
    "https://solar.lowtechmagazine.com",
    "http://wttr.in",
    "https://en.m.wikipedia.org"
};

// --- 渲染工具 (Floyd-Steinberg Dithering) ---
static int ns_d_err_r0[NS_DITHER_LINE_W], ns_d_err_g0[NS_DITHER_LINE_W], ns_d_err_b0[NS_DITHER_LINE_W];
static int ns_d_err_r1[NS_DITHER_LINE_W], ns_d_err_g1[NS_DITHER_LINE_W], ns_d_err_b1[NS_DITHER_LINE_W];

static void ns_draw_image_high_quality(struct ns_state *s, int x, int y, int dw, int dh, int clip_x0, int clip_y0, int clip_x1, int clip_y1) {
    if (!s->image_ready) return;
    if (dw < 1 || dh < 1) return;
    for (int py = 0; py < dh; py++) {
        int sy = py * s->image_h / dh;
        if (sy < 0) sy = 0;
        if (sy >= s->image_h) sy = s->image_h - 1;
        for (int px = 0; px < dw; px++) {
            int sx = px * s->image_w / dw;
            if (sx < 0) sx = 0;
            if (sx >= s->image_w) sx = s->image_w - 1;
            uint16_t c = s->image_pixels[sy * NS_MAX_IMAGE_W + sx];
            int r, g, b;
            rgb_from_rgb565(c, &r, &g, &b);
            int idx = palette_index_from_rgb((unsigned char)r, (unsigned char)g, (unsigned char)b);
            int dx = x + px;
            int dy = y + py;
            if (dx < clip_x0 || dx >= clip_x1 || dy < clip_y0 || dy >= clip_y1) continue;
            putpixel(dx, dy, idx);
        }
    }
}

static int ns_tag_eq(const char *tag, int len, const char *name) {
    int n = 0; while (name[n]) n++;
    return (len == n) && (strncmp(tag, name, (uint32_t)n) == 0);
}

static int ns_marker_is(const char *line, const char *marker) {
    if (!line || !marker || line[0] != '\x1d') return 0;
    return strncmp(line + 1, marker, (uint32_t)strlen(marker)) == 0;
}

static int ns_count_lines_until_marker(struct ns_state *s, int start_idx, const char *marker) {
    int count = 0;
    if (!s || start_idx < 0 || !marker) return 1;
    for (int i = start_idx + 1; i < s->line_count; i++) {
        if (ns_marker_is(s->lines[i], marker)) break;
        count++;
    }
    if (count < 1) count = 1;
    return count;
}

static void ns_clip_rect(int clip_x0, int clip_y0, int clip_x1, int clip_y1,
                         int *x, int *y, int *w, int *h) {
    int x1 = *x + *w;
    int y1 = *y + *h;
    if (*x < clip_x0) *x = clip_x0;
    if (*y < clip_y0) *y = clip_y0;
    if (x1 > clip_x1) x1 = clip_x1;
    if (y1 > clip_y1) y1 = clip_y1;
    *w = x1 - *x;
    *h = y1 - *y;
    if (*w < 0) *w = 0;
    if (*h < 0) *h = 0;
}

static void ns_draw_bevel_rect_clipped(int x, int y, int w, int h,
                                       int fill, int light, int dark,
                                       int clip_x0, int clip_y0, int clip_x1, int clip_y1) {
    ns_clip_rect(clip_x0, clip_y0, clip_x1, clip_y1, &x, &y, &w, &h);
    if (w <= 0 || h <= 0) return;
    draw_bevel_rect(x, y, w, h, fill, light, dark);
}

static int ns_is_wrp_control_url(const char *url);

static void ns_reset_parser_for_window(struct Window *w) {
    hubbub_parser *parser;
    hubbub_parser_optparams opt;
    if (!w || w->id < 0 || w->id >= MAX_WINDOWS) return;
    if (netsurf_parsers[w->id]) {
        hubbub_parser_destroy(netsurf_parsers[w->id]);
        netsurf_parsers[w->id] = 0;
    }
    if (hubbub_parser_create("UTF-8", false, &parser) != HUBBUB_OK) return;
    opt.token_handler.handler = token_handler;
    opt.token_handler.pw = w;
    hubbub_parser_setopt(parser, HUBBUB_PARSER_TOKEN_HANDLER, &opt);
    netsurf_parsers[w->id] = parser;
}

static void ns_reset_doc(struct ns_state *s, int keep_image) {
    if (!s) return;
    s->raw_len = 0; s->raw[0] = '\0';
    s->source_len = 0; s->source[0] = '\0';
    s->line_count = 0; s->max_line_len = 0;
    s->pending_break = 1; s->layout_width = 0; s->layout_scale = 1;
    s->layout_raw_len = 0;
    s->last_was_space = 1; s->in_script_tag = 0; s->in_style_tag = 0;
    s->in_pre_tag = 0; s->in_form_tag = 0; s->in_textarea_tag = 0;
    s->in_table_tag = 0; s->in_row_tag = 0; s->table_cell_count = 0;
    s->in_button_tag = 0; s->in_link_tag = 0; s->wrp_page = 0; s->has_run_test_button = 0;
    s->run_test_done = 0; s->has_image = 0; s->image_loading = 0;
    if (!keep_image) {
        s->image_ready = 0; s->image_w = 0; s->image_h = 0;
        s->image_url[0] = '\0'; s->image_alt[0] = '\0';
    }
    s->image_line_idx = -1;
    s->image_map_href[0] = '\0'; s->image_draw_x = 0; s->image_draw_y = 0;
    s->image_draw_w = 0; s->image_draw_h = 0;
    s->current_link_start_line = -1; s->wrp_ctrl_count = 0; s->click_count = 0;
    s->hover_click_idx = -1; s->active_ctrl_idx = -1;
    s->cur_line_idx = 0; s->cur_line_len = 0; s->current_link_href[0] = '\0';
    s->link_count = 0;
}

static int ns_starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static void ns_derive_filename_from_path(const char *path, char *out, int out_size) {
    const char *name = path;
    const char *p = path;
    if (out_size <= 0) return;
    while (*p) {
        if (*p == '/') name = p + 1;
        else if (*p == '?' || *p == '#') break;
        p++;
    }
    if (*name == '\0') {
        lib_strcpy(out, "index.html");
        return;
    }
    int i = 0;
    while (name < p && i < out_size - 1) out[i++] = *name++;
    out[i] = '\0';
}

void netsurf_begin_navigation(int win_id) {
    if (win_id < 0 || win_id >= MAX_WINDOWS) return;
    struct ns_state *s = &ns_states[win_id];
    int is_wrp = ns_is_wrp_control_url(wins[win_id].ns_url);
    int keep_image = is_wrp && s->wrp_page && s->image_ready;
    lib_printf("[NET] begin_nav win=%d url='%s'\n", win_id, wins[win_id].ns_url);
    ns_reset_doc(s, keep_image);
    s->wrp_page = is_wrp;
    if (keep_image) s->image_loading = 1;
    ns_reset_parser_for_window(&wins[win_id]);
    wins[win_id].v_offset = 0;
    wins[win_id].ns_h_offset = 0;
    wins[win_id].ns_input_active = 0;
    extern volatile int gui_redraw_needed;
    extern volatile int need_resched;
    gui_redraw_needed = 1;
    need_resched = 1;
}

void netsurf_invalidate_layout(int win_id) {
    if (win_id < 0 || win_id >= MAX_WINDOWS) return;
    struct ns_state *s = &ns_states[win_id];
    s->layout_width = 0;
    s->layout_scale = 0;
    s->layout_raw_len = 0;
    s->line_count = 0;
    s->max_line_len = 0;
}

static void ns_append_ch(struct ns_state *s, char ch) {
    if (!s || s->raw_len >= (int)sizeof(s->raw) - 1) return;
    s->raw[s->raw_len++] = ch;
    s->raw[s->raw_len] = '\0';
}

static int ns_decode_entity(const char *buf, int len, int *consumed, char *out) {
    if (!buf || len <= 1 || buf[0] != '&') return 0;
    if (len >= 4 && strncmp(buf, "&lt;", 4) == 0) { *consumed = 4; *out = '<'; return 1; }
    if (len >= 4 && strncmp(buf, "&gt;", 4) == 0) { *consumed = 4; *out = '>'; return 1; }
    if (len >= 5 && strncmp(buf, "&amp;", 5) == 0) { *consumed = 5; *out = '&'; return 1; }
    if (len >= 6 && strncmp(buf, "&quot;", 6) == 0) { *consumed = 6; *out = '"'; return 1; }
    if (len >= 6 && strncmp(buf, "&apos;", 6) == 0) { *consumed = 6; *out = '\''; return 1; }
    if (len >= 6 && strncmp(buf, "&nbsp;", 6) == 0) { *consumed = 6; *out = ' '; return 1; }
    if (len >= 5 && strncmp(buf, "&#39;", 5) == 0) { *consumed = 5; *out = '\''; return 1; }
    return 0;
}

static void ns_append_text(struct ns_state *s, const char *buf, int len) {
    if (!s || !buf || len <= 0) return;
    int last_space = s->last_was_space;
    for (int i = 0; i < len; i++) {
        char ch = buf[i];
        if (s->in_script_tag || s->in_style_tag) continue;
        if (ch == '\r') continue;
        if (!s->in_pre_tag && (ch == '\n' || ch == '\t')) ch = ' ';
        if (ch == '&') {
            int consumed = 0; char decoded = 0;
            if (ns_decode_entity(buf + i, len - i, &consumed, &decoded)) {
                ch = decoded; i += consumed - 1;
            }
        }
        if (s->in_pre_tag || s->in_textarea_tag) {
            if (s->pending_break) { ns_append_ch(s, '\n'); s->pending_break = 0; }
            ns_append_ch(s, ch);
            if (ch == '\n') last_space = 1; else if (ch == ' ' || ch == '\t') last_space = 1; else last_space = 0;
            continue;
        }
        if (s->pending_break) { ns_append_ch(s, '\n'); s->pending_break = 0; last_space = 1; }
        if (ch == ' ') { if (last_space || s->raw_len == 0) continue; ns_append_ch(s, ' '); last_space = 1; continue; }
        ns_append_ch(s, ch); last_space = 0;
    }
    s->last_was_space = last_space;
}

static void ns_append_break(struct ns_state *s) {
    if (!s) return;
    if (s->raw_len > 0 && s->raw[s->raw_len - 1] != '\n') ns_append_ch(s, '\n');
    s->pending_break = 1;
}

static void ns_append_marker_line(struct ns_state *s, const char *marker) {
    if (!s || !marker || !*marker) return;
    if (s->raw_len > 0 && s->raw[s->raw_len - 1] != '\n') {
        ns_append_ch(s, '\n');
    }
    ns_append_text(s, marker, strlen(marker));
    if (s->raw_len > 0 && s->raw[s->raw_len - 1] != '\n') {
        ns_append_ch(s, '\n');
    }
    s->pending_break = 0;
}

static int ns_copy_attr_value(const hubbub_token *token, const char *name, char *out, int out_max) {
    if (!token || !name || !out || out_max <= 0) return 0;
    for (uint32_t i = 0; i < token->data.tag.n_attributes; i++) {
        hubbub_attribute *attr = &token->data.tag.attributes[i];
        if (ns_tag_eq((const char *)attr->name.ptr, (int)attr->name.len, name) && attr->value.ptr && attr->value.len > 0) {
            int n = (int)attr->value.len; if (n >= out_max) n = out_max - 1;
            memcpy(out, attr->value.ptr, n); out[n] = '\0'; return 1;
        }
    }
    return 0;
}

static int ns_break_before_tag(const char *tag, int len) {
    return ns_tag_eq(tag, len, "br") || ns_tag_eq(tag, len, "hr") ||
           ns_tag_eq(tag, len, "p") || ns_tag_eq(tag, len, "div") ||
           ns_tag_eq(tag, len, "h1") || ns_tag_eq(tag, len, "h2") ||
           ns_tag_eq(tag, len, "h3") || ns_tag_eq(tag, len, "h4") ||
           ns_tag_eq(tag, len, "h5") || ns_tag_eq(tag, len, "h6") ||
           ns_tag_eq(tag, len, "li") || ns_tag_eq(tag, len, "tr") ||
           ns_tag_eq(tag, len, "pre") || ns_tag_eq(tag, len, "blockquote");
}

static int ns_break_after_tag(const char *tag, int len) {
    return ns_tag_eq(tag, len, "p") || ns_tag_eq(tag, len, "div") ||
           ns_tag_eq(tag, len, "h1") || ns_tag_eq(tag, len, "h2") ||
           ns_tag_eq(tag, len, "h3") || ns_tag_eq(tag, len, "h4") ||
           ns_tag_eq(tag, len, "h5") || ns_tag_eq(tag, len, "h6") ||
           ns_tag_eq(tag, len, "li") || ns_tag_eq(tag, len, "tr") ||
           ns_tag_eq(tag, len, "pre") || ns_tag_eq(tag, len, "blockquote") ||
           ns_tag_eq(tag, len, "table");
}

static int ns_str_starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) { if (*s != *prefix) return 0; s++; prefix++; }
    return 1;
}

static void ns_resolve_url_like(const char *base, const char *src, char *out, int out_max) {
    int j = 0; if (!out || out_max <= 0) return; out[0] = '\0'; if (!src || !*src) return;
    if (ns_str_starts_with(src, "http://") || ns_str_starts_with(src, "https://")) { lib_strcpy(out, src); return; }
    const char *pre = ns_str_starts_with(base, "https://") ? "https://" : (ns_str_starts_with(base, "http://") ? "http://" : 0);
    if (pre) {
        const char *p = base + strlen(pre);
        while (p[j] && p[j] != '/') j++;
        int prefix_len = strlen(pre) + j; if (prefix_len >= out_max) prefix_len = out_max - 1;
        memcpy(out, base, prefix_len); out[prefix_len] = '\0';
        if (src[0] == '/') { lib_strcat(out, src); return; }
        if (base[prefix_len - 1] != '/') lib_strcat(out, "/");
        lib_strcat(out, src); return;
    }
    lib_strcpy(out, src);
}

static void ns_enable_wrp_layout(struct ns_state *s) {
    if (!s || s->wrp_page) return;
    s->wrp_page = 1;
    s->raw_len = 0;
    s->raw[0] = '\0';
    s->line_count = 0;
    s->max_line_len = 0;
    s->pending_break = 1;
    s->layout_width = 0;
    s->layout_scale = 0;
    s->layout_raw_len = 0;
    s->last_was_space = 1;
    s->wrp_ctrl_count = 0;
    s->click_count = 0;
    s->has_image = 0;
    s->image_line_idx = -1;
    s->image_map_href[0] = '\0';
}

static int ns_is_wrp_input_name(const char *name) {
    if (!name || !*name) return 0;
    return strcmp(name, "url") == 0 ||
           strcmp(name, "Fn") == 0 ||
           strcmp(name, "w") == 0 ||
           strcmp(name, "h") == 0 ||
           strcmp(name, "z") == 0 ||
           strcmp(name, "m") == 0 ||
           strcmp(name, "t") == 0 ||
           strcmp(name, "k") == 0;
}

static int ns_find_wrp_img_ref(const char *raw, char *out, int out_max) {
    const char *p;
    const char *q;
    int n = 0;
    if (!raw || !out || out_max <= 0) return 0;
    out[0] = '\0';
    p = strstr(raw, "/img/");
    if (!p) return 0;
    q = p;
    while (*q && *q != '"' && *q != '\'' && *q != '>' && *q != ' ' && *q != '\r' && *q != '\n') q++;
    n = (int)(q - p);
    if (n <= 0) return 0;
    if (n >= out_max) n = out_max - 1;
    memcpy(out, p, (uint32_t)n);
    out[n] = '\0';
    return 1;
}

// --- 核心排版：具備 Word Wrap 的自動換行 ---
static void ns_layout(struct Window *w, int content_cols) {
    struct ns_state *s = &ns_states[w->id];
    int scale = terminal_font_scale(w);
    if (scale < 1) scale = 1;
    if (scale > 2) scale = 2;
    if (content_cols < 1) content_cols = 1;
    if (s->layout_width == content_cols && s->layout_scale == scale &&
        s->layout_raw_len == s->raw_len && s->line_count > 0) return;

    s->layout_width = content_cols; s->layout_scale = scale;
    s->layout_raw_len = s->raw_len;
    s->line_count = 0; s->max_line_len = 0;

    int raw_pos = 0;
    while (raw_pos < s->raw_len && s->line_count < 1024) {
        int line_pos = 0;
        while (raw_pos < s->raw_len) {
            char ch = s->raw[raw_pos++];
            if (ch == '\n') break;
            if (line_pos < 255) {
                s->lines[s->line_count][line_pos++] = ch;
            }
        }
        s->lines[s->line_count][line_pos] = '\0';
        if (line_pos > s->max_line_len) s->max_line_len = line_pos;
        s->line_count++;
    }
    if (s->line_count == 0) { s->lines[0][0] = '\0'; s->line_count = 1; }
}

static void ns_begin_link(struct ns_state *s, const char *href, int href_len) {
    s->in_link_tag = 1; if (href_len >= 120) href_len = 119;
    s->current_link_start_line = s->line_count;
    memcpy(s->current_link_href, href, href_len); s->current_link_href[href_len] = '\0';
}
static void ns_end_link(struct ns_state *s) {
    if (s->link_count < NS_MAX_LINKS && s->current_link_href[0]) {
        s->links[s->link_count].start_line = (s->current_link_start_line >= 0) ? s->current_link_start_line : s->line_count;
        s->links[s->link_count].end_line = s->line_count;
        lib_strcpy(s->links[s->link_count].href, s->current_link_href);
        s->link_count++;
    }
    s->in_link_tag = 0;
    s->current_link_start_line = -1;
}

static int ns_is_wrp_control_url(const char *url) {
    char wrp_base[160];
    char wrp_host[32], cur_host[32];
    char wrp_path[96], cur_path[160];
    uint16_t wrp_port = 0, cur_port = 0;

    if (!url || !*url) return 0;
    if (ssh_client_get_wrp_url(wrp_base, sizeof(wrp_base)) < 0 || wrp_base[0] == '\0') return 0;
    if (!parse_wget_url(wrp_base, wrp_host, &wrp_port, wrp_path)) return 0;
    if (!parse_wget_url(url, cur_host, &cur_port, cur_path)) return 0;
    if (strcmp(wrp_host, cur_host) != 0 || wrp_port != cur_port) return 0;
    if (strncmp(cur_path, "/img/", 5) == 0) return 0;
    return 1;
}

static struct ns_link *ns_find_link_by_line(struct ns_state *s, int line) {
    if (!s) return 0;
    for (int i = 0; i < s->link_count; i++) {
        if (line >= s->links[i].start_line && line <= s->links[i].end_line) return &s->links[i];
    }
    return 0;
}

static void ns_add_click_region(struct ns_state *s, int kind, int x, int y, int w, int h,
                                int line, const char *href,
                                const char *input_type, const char *input_value, const char *input_name) {
    struct ns_click *c;
    if (!s) return;
    if (w <= 0 || h <= 0) return;
    if (s->click_count >= (int)(sizeof(s->clicks) / sizeof(s->clicks[0]))) return;
    c = &s->clicks[s->click_count++];
    c->kind = kind;
    c->x = x;
    c->y = y;
    c->w = w;
    c->h = h;
    c->line = line;
    c->href[0] = '\0';
    c->input_type[0] = '\0';
    c->input_value[0] = '\0';
    c->input_name[0] = '\0';
    if (href && *href) lib_strcpy(c->href, href);
    if (input_type && *input_type) lib_strcpy(c->input_type, input_type);
    if (input_value && *input_value) lib_strcpy(c->input_value, input_value);
    if (input_name && *input_name) lib_strcpy(c->input_name, input_name);
}

static struct ns_click *ns_hit_click_region(struct ns_state *s, int x, int y) {
    if (!s) return 0;
    for (int i = s->click_count - 1; i >= 0; i--) {
        struct ns_click *c = &s->clicks[i];
        if (x >= c->x && x < c->x + c->w && y >= c->y && y < c->y + c->h) return c;
    }
    return 0;
}

static struct ns_wrp_control *ns_hit_wrp_control(struct ns_state *s, int x, int y) {
    if (!s || !s->wrp_page) return 0;
    for (int i = s->wrp_ctrl_count - 1; i >= 0; i--) {
        struct ns_wrp_control *c = &s->wrp_ctrls[i];
        if (x >= c->x && x < c->x + c->w && y >= c->y && y < c->y + c->h) return c;
    }
    return 0;
}

static void ns_add_wrp_control(struct ns_state *s, int kind, const char *type, const char *value, const char *name) {
    struct ns_wrp_control *c;
    if (!s) return;
    if (s->wrp_ctrl_count >= (int)(sizeof(s->wrp_ctrls) / sizeof(s->wrp_ctrls[0]))) return;
    c = &s->wrp_ctrls[s->wrp_ctrl_count++];
    c->kind = kind;
    c->x = c->y = c->w = c->h = 0;
    c->type[0] = c->value[0] = c->name[0] = '\0';
    if (type && *type) lib_strcpy(c->type, type);
    if (value && *value) lib_strcpy(c->value, value);
    if (name && *name) lib_strcpy(c->name, name);
}

static int ns_wrp_control_box_w(const struct ns_wrp_control *c, int scale) {
    int w;
    if (!c) return 0;
    if (c->kind == 1) {
        const char *text = c->value[0] ? c->value : (c->type[0] ? c->type : "go");
        w = 14 + (int)strlen(text) * (4 * scale);
        if (w < 24 * scale) w = 24 * scale;
        if (w > 56 * scale) w = 56 * scale;
        return w;
    }
    if (c->kind == 3) {
        const char *text = c->value;
        w = 20 + (int)strlen(text) * (4 * scale);
        if (w < 44 * scale) w = 44 * scale;
        if (w > 128 * scale) w = 128 * scale;
        return w;
    }
    if (c->kind == 0) {
        w = 42 * scale;
        if (c->value[0]) {
            w = 18 + (int)strlen(c->value) * (4 * scale);
            if (w < 36 * scale) w = 36 * scale;
            if (w > 80 * scale) w = 80 * scale;
        }
        return w;
    }
    w = 36 * scale;
    if (w < 28 * scale) w = 28 * scale;
    return w;
}

static const char *ns_wrp_control_label_for_index(int idx) {
    switch (idx) {
        case 7: return "W";
        case 8: return "H";
        case 9: return "Z";
        case 10: return "M";
        case 11: return "T";
        case 12: return "K";
        default: return "";
    }
}

static const char *ns_wrp_control_text_for_index(struct Window *w, int idx, const struct ns_wrp_control *c) {
    if (!c) return "";
    if (c->value[0]) return c->value;
    if (c->kind == 0) {
        if (idx == 9) return "1.0 x";
        if (idx == 10) return (w && strstr(w->ns_url, "m=html")) ? "html" : "ismap";
        if (idx == 11) return "gip";
    }
    if (c->kind == 1) return c->type[0] ? c->type : "submit";
    return "";
}

static int ns_draw_wrp_controls(struct Window *w, int content_x, int content_y0, int content_x1, int scale) {
    struct ns_state *s = &ns_states[w->id];
    int gap = 6;
    int clip_y1 = content_y0 + 220;
    int row_y[4];
    int row_h[4];
    int row_x[4];
    int toolbar_rows = 4;

    if (!s->wrp_page || s->wrp_ctrl_count <= 0) return 0;

    row_y[0] = content_y0;
    row_h[0] = row_h[1] = row_h[2] = row_h[3] = 0;
    row_x[0] = row_x[1] = row_x[2] = row_x[3] = content_x;
    for (int r = 1; r < toolbar_rows; r++) row_y[r] = row_y[r - 1] + 24 * scale;

    for (int i = 0; i < s->wrp_ctrl_count; i++) {
        struct ns_wrp_control *c = &s->wrp_ctrls[i];
        const char *label = ns_wrp_control_label_for_index(i);
        const char *text = ns_wrp_control_text_for_index(w, i, c);
        int row = 3;
        int label_w = (label && *label) ? (12 * scale) : 0;
        int box_h = (c->kind == 1) ? (20 * scale) : (22 * scale);
        int box_w = ns_wrp_control_box_w(c, scale);
        int x;
        int y;

        if (i <= 1) row = 0;
        else if (i <= 6) row = 1;
        else if (i <= 10) row = 2;
        else row = 3;

        if (row >= toolbar_rows) row = toolbar_rows - 1;
        y = row_y[row];
        x = row_x[row];

        if (row == 0 && i == 0) {
            int next_w = 0;
            if (s->wrp_ctrl_count > 1) {
                next_w = ns_wrp_control_box_w(&s->wrp_ctrls[1], scale) + gap;
            }
            box_w = content_x1 - x - next_w - 8;
            if (box_w < 88 * scale) box_w = 88 * scale;
        }

        if (x + label_w + box_w > content_x1 && i > 0) {
            row++;
            if (row >= toolbar_rows) row = toolbar_rows - 1;
            y = row_y[row];
            x = row_x[row];
        }

        c->x = x + label_w;
        c->y = y;
        c->w = box_w;
        c->h = box_h;

        if (i == s->active_ctrl_idx) {
            ns_draw_bevel_rect_clipped(c->x - 1, c->y - 1, c->w + 2, c->h + 2,
                                       UI_C_PANEL_ACTIVE, UI_C_TEXT, UI_C_PANEL_DEEP,
                                       content_x, content_y0, content_x1, clip_y1);
        }

        if (label && *label) {
            draw_text_scaled_clipped(x, y + 4, label, UI_C_TEXT_DIM, scale,
                                     content_x, content_y0, content_x1, clip_y1);
        }

        if (c->kind == 1) {
            ns_draw_bevel_rect_clipped(c->x, c->y, c->w, c->h,
                                       UI_C_PANEL_ACTIVE, UI_C_TEXT, UI_C_PANEL_DEEP,
                                       content_x, content_y0, content_x1, clip_y1);
            draw_text_scaled_clipped(c->x + 6, c->y + 3, text,
                                     UI_C_TEXT, scale, content_x, content_y0, content_x1, clip_y1);
        } else if (c->kind == 3) {
            ns_draw_bevel_rect_clipped(c->x, c->y, c->w, c->h,
                                       UI_C_PANEL_LIGHT, UI_C_TEXT_DIM, UI_C_BORDER,
                                       content_x, content_y0, content_x1, clip_y1);
            draw_text_scaled_clipped(c->x + 6, c->y + 3, text,
                                     UI_C_TEXT_DIM, scale, content_x, content_y0, content_x1, clip_y1);
        } else {
            ns_draw_bevel_rect_clipped(c->x, c->y, c->w, c->h,
                                       UI_C_PANEL, UI_C_TEXT_MUTED, UI_C_PANEL_DEEP,
                                       content_x, content_y0, content_x1, clip_y1);
            draw_text_scaled_clipped(c->x + 6, c->y + 3, text,
                                     UI_C_TEXT_DIM, scale, content_x, content_y0, content_x1, clip_y1);
        }

        row_x[row] = c->x + c->w + gap;
        if (box_h > row_h[row]) row_h[row] = box_h;
    }

    {
        int total_h = 0;
        for (int r = 0; r < toolbar_rows; r++) {
            int h = row_h[r] > 0 ? row_h[r] : (20 * scale);
            total_h += h;
            if (r != toolbar_rows - 1) total_h += gap;
        }
        return total_h + 8;
    }
}

static int ns_parse_input_marker(const char *line, char *type, int type_max,
                                 char *value, int value_max,
                                 char *name, int name_max) {
    const char *p;
    int i;

    if (!line || strncmp(line, "\x1dINPUT|", 7) != 0) return 0;
    p = line + 7;
    for (i = 0; i < type_max - 1 && p[i] && p[i] != '|'; i++) type[i] = p[i];
    type[i] = '\0';
    p += i;
    if (*p != '|') return 0;
    p++;
    for (i = 0; i < value_max - 1 && p[i] && p[i] != '|'; i++) value[i] = p[i];
    value[i] = '\0';
    p += i;
    if (*p != '|') return 0;
    p++;
    for (i = 0; i < name_max - 1 && p[i] && p[i] != '|'; i++) name[i] = p[i];
    name[i] = '\0';
    return 1;
}

static void ns_trim_wrp_url_for_action(const char *src, char *dst, int dst_max) {
    int i = 0;
    int n = 0;
    if (!dst || dst_max <= 0) return;
    dst[0] = '\0';
    if (!src) return;
    while (src[n]) n++;
    while (n > 0 && src[n - 1] == '\n' && n > 0) n--;
    while (src[i] && i < dst_max - 1 && i < n) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void ns_clear_queued_actions(struct ns_state *s) {
    if (!s) return;
    s->action_head = 0;
    s->action_tail = 0;
    s->action_count = 0;
    s->waiting_for_id = 0;
}

static void ns_enqueue_action(struct ns_state *s, int mx, int my, const char *k, int is_click, const char *target) {
    if (s->action_count >= 32) return;
    struct ns_queued_action *a = &s->action_queue[s->action_tail];
    a->mx = mx; a->my = my; a->is_click = is_click;
    if (k) lib_strcpy(a->kstr, k); else a->kstr[0] = '\0';
    if (target) lib_strcpy(a->target, target); else a->target[0] = '\0';
    s->action_tail = (s->action_tail + 1) % 32;
    s->action_count++;
}

static int ns_url_unreserved(char ch) {
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '-' || ch == '_' || ch == '.' || ch == '~';
}

static void ns_append_encoded_value(char *url, int url_max, const char *value) {
    static const char hex[] = "0123456789ABCDEF";
    int pos;
    if (!url || !value || url_max <= 0) return;
    pos = (int)strlen(url);
    for (int i = 0; value[i] && pos < url_max - 1; i++) {
        unsigned char ch = (unsigned char)value[i];
        if (ns_url_unreserved((char)ch)) {
            url[pos++] = (char)ch;
        } else {
            if (pos + 3 >= url_max) break;
            url[pos++] = '%';
            url[pos++] = hex[(ch >> 4) & 0xF];
            url[pos++] = hex[ch & 0xF];
        }
    }
    url[pos] = '\0';
}

static void ns_process_queued_actions(struct Window *w) {
    struct ns_state *s = &ns_states[w->id];
    
    // 嚴格閘門：如果正在等待新 ID 或隊列為空，則絕不執行
    if (s->waiting_for_id || s->action_count == 0) return;
    if (s->image_loading) return;

    struct ns_queued_action *a = &s->action_queue[s->action_head];
    char map_url[512];
    char num[16];
    int have_map = 0;

    if (!s->image_ready && !s->image_url[0] &&
        !(s->image_map_href[0] && strstr(s->image_map_href, ".map")) &&
        !(a->target[0] && strstr(a->target, ".map")) &&
        !(w->ns_url[0] && strstr(w->ns_url, ".map"))) {
        return;
    }

    // 優先使用解析到的 Map URL
    if (s->image_map_href[0] && strstr(s->image_map_href, ".map")) {
        lib_strcpy(map_url, s->image_map_href);
        have_map = 1;
    } else if (a->target[0] && strstr(a->target, ".map")) {
        lib_strcpy(map_url, a->target);
        have_map = 1;
    } else if (w->ns_url[0] && strstr(w->ns_url, ".map")) {
        lib_strcpy(map_url, w->ns_url);
        have_map = 1;
    } else {
        // Fallback: 轉化目前網址
        lib_strcpy(map_url, s->image_url[0] ? s->image_url : w->ns_url);
        char *img_pos = strstr(map_url, "/img/");
        if (img_pos) {
            char rest[256]; lib_strcpy(rest, img_pos + 5); 
            char *dot = strrchr(rest, '.'); if (dot) *dot = '\0';
            lib_strcpy(img_pos, "/map/"); lib_strcat(map_url, rest); lib_strcat(map_url, ".map");
            have_map = 1;
        }
    }
    if (!have_map || !strstr(map_url, ".map")) return;
    
    char *q = strchr(map_url, '?'); if (q) *q = '\0'; 
    lib_strcat(map_url, "?");
    
    int rx = 0, ry = 0;
    if (a->is_click) {
        rx = a->mx - s->image_draw_x;
        ry = a->my - s->image_draw_y;
        if (rx < 0) rx = 0; if (ry < 0) ry = 0;
        if (s->image_draw_w > 0) rx = rx * s->image_w / s->image_draw_w;
        if (s->image_draw_h > 0) ry = ry * s->image_h / s->image_draw_h;
    }
    
    lib_itoa((uint32_t)rx, num); lib_strcat(map_url, num); lib_strcat(map_url, ",");
    lib_itoa((uint32_t)ry, num); lib_strcat(map_url, num);

    if (a->kstr[0]) {
        lib_strcat(map_url, "&k=");
        ns_append_encoded_value(map_url, sizeof(map_url), a->kstr);
    }

    lib_printf("[NET] Q-EXEC win=%d target='%s'\n", w->id, map_url);
    
    // 標記為等待新 ID，鎖定隊列
    s->waiting_for_id = 1;

    s->action_head = (s->action_head + 1) % 32;
    s->action_count--;

    ns_start_navigation(w, map_url);
}

static void ns_effective_viewport(struct Window *w, int *vw, int *vh) {
    if (!w) {
        if (vw) *vw = 400;
        if (vh) *vh = 420;
        return;
    }
    int raw_w = w->maximized ? WIDTH : w->w;
    int raw_h = w->maximized ? DESKTOP_H : w->h;
    
    // 寬度：扣除捲軸空間 (14px) 和左邊距 (44px) 
    if (vw) *vw = raw_w - 60; 
    // 高度：扣除頂部控制列 (64px) 和邊距
    if (vh) *vh = raw_h - 80;

    if (vw && *vw < 100) *vw = 100;
    if (vh && *vh < 100) *vh = 100;
}

static void ns_append_query_param(char *url, int url_max, const char *key, const char *value) {
    int len;
    if (!url || !key || !value || url_max <= 0) return;
    len = strlen(url);
    if (len >= url_max - 1) return;
    if (strchr(url, '?')) lib_strcat(url, "&");
    else lib_strcat(url, "?");
    lib_strcat(url, key);
    lib_strcat(url, "=");
    lib_strcat(url, value);
}

static int ns_start_document_navigation(struct Window *w, const char *url) {
    char launch_url[512];
    char clean_url[256];
    int vw = 0, vh = 0;
    struct ns_state *s;

    if (!w || !url || !*url) return -1;
    netsurf_normalize_target_url(url, clean_url, sizeof(clean_url));
    if (clean_url[0] == '\0') {
        lib_printf("[NET] DOC NAV ERR: empty target\n");
        return -1;
    }

    ns_effective_viewport(w, &vw, &vh);
    if (netsurf_prepare_launch_url(clean_url, vw, vh, launch_url, sizeof(launch_url)) < 0) {
        lib_printf("[NET] DOC NAV ERR: prepare fail '%s'\n", clean_url);
        return -1;
    }
    if (netsurf_queue_wrapped_request(clean_url, vw, vh, w->id) < 0) {
        lib_printf("[NET] DOC NAV ERR: queue fail '%s'\n", clean_url);
        return -1;
    }

    s = &ns_states[w->id];
    ns_clear_queued_actions(s);
    s->fav_active = 0;
    s->active_ctrl_idx = -1;
    lib_strcpy(w->ns_target_url, clean_url);
    lib_strcpy(w->ns_url, launch_url);
    lib_strcpy(w->ns_history[0], launch_url);
    netsurf_begin_navigation(w->id);

    extern volatile int gui_redraw_needed;
    extern volatile int need_resched;
    gui_redraw_needed = 1;
    need_resched = 1;
    w->mailbox = 1;
    return 0;
}

static void ns_start_navigation(struct Window *w, const char *url) {
    char clean_url[256];
    char full_url[512];
    struct ns_state *s;
    if (!w || !url || !*url) return;

    ns_resolve_url_like(w->ns_url, url, full_url, sizeof(full_url));
    int is_ismap = (strstr(full_url, ".map") != NULL || strchr(full_url, ',') != NULL);

    if (!is_ismap) {
        ns_start_document_navigation(w, full_url);
        return;
    }

    lib_strcpy(clean_url, full_url);
    
    if (clean_url[0] == '\0') {
        lib_printf("[NET] NAV ERR: empty target\n");
        s = &ns_states[w->id];
        s->waiting_for_id = 0;
        return;
    }
    lib_printf("[NET] NAV START: target='%s' ismap=%d\n", clean_url, is_ismap);

    lib_strcpy(w->ns_url, clean_url);

    lib_strcpy(w->ns_history[0], w->ns_url);
    
    s = &ns_states[w->id];
    
    // 延遲重置：不立即清空舊數據，先標記狀態
    s->image_loading = 1; 
    s->nav_reset_pending = 1; 
    s->image_map_href[0] = '\0'; // 立即清空，強制隊列等待新 ID
    
    ns_reset_parser_for_window(w);

    char host[32], path[WGET_PATH_MAX]; uint16_t port;
    if (parse_wget_url(w->ns_url, host, &port, path)) {
        const char *fname = "";
        lib_printf("[NET] NAV QUEUED: host=%s path=%s fname='%s'\n", host, path, fname);
        wget_queue_request_ex(host, path, fname, port, 0, 0, 0, w->id);
    } else {
        lib_printf("[NET] NAV ERR: parse fail '%s'\n", w->ns_url);
        s->waiting_for_id = 0;
    }

    extern volatile int gui_redraw_needed;
    extern volatile int need_resched;
    gui_redraw_needed = 1;
    need_resched = 1;
    w->mailbox = 1;
}

static void ns_follow_submit(struct Window *w, const struct ns_click *c) {
    char launch_url[512];
    char base_url[256];
    char clean_url[256];
    int vw = 0, vh = 0;
    if (!w || !c || !c->input_value[0]) return;
    if (w->ns_target_url[0]) lib_strcpy(base_url, w->ns_target_url);
    else ns_trim_wrp_url_for_action(w->ns_url, base_url, sizeof(base_url));
    netsurf_normalize_target_url(base_url, clean_url, sizeof(clean_url));
    if (clean_url[0] == '\0') return;
    lib_strcpy(base_url, clean_url);
    if (base_url[0] == '\0') return;
    lib_strcpy(w->ns_target_url, base_url);
    ns_effective_viewport(w, &vw, &vh);
    if (netsurf_prepare_launch_url(base_url, vw, vh, launch_url, sizeof(launch_url)) < 0) return;
    if (c->input_name[0]) ns_append_query_param(launch_url, sizeof(launch_url), c->input_name, c->input_value);
    else ns_append_query_param(launch_url, sizeof(launch_url), "Fn", c->input_value);
    lib_strcpy(w->ns_url, launch_url);
    lib_strcpy(w->ns_history[0], launch_url);
    netsurf_begin_navigation(w->id);
    if (netsurf_queue_wrapped_request(w->ns_target_url, vw, vh, w->id) < 0) return;
    extern volatile int gui_redraw_needed;
    extern volatile int need_resched;
    gui_redraw_needed = 1;
    need_resched = 1;
    w->mailbox = 1;
}

int netsurf_refresh_current_view(int win_id) {
    struct Window *w;
    char launch_url[512];
    char clean_url[256];
    int vw = 0, vh = 0;

    if (win_id < 0 || win_id >= MAX_WINDOWS) return -1;
    w = &wins[win_id];
    if (!w->active || w->kind != WINDOW_KIND_NETSURF) return -1;
    if (w->ns_target_url[0] == '\0') return -1;
    netsurf_normalize_target_url(w->ns_target_url, clean_url, sizeof(clean_url));
    if (clean_url[0] == '\0') return -1;
    ns_effective_viewport(w, &vw, &vh);
    if (netsurf_prepare_launch_url(clean_url, vw, vh, launch_url, sizeof(launch_url)) < 0) return -1;
    lib_strcpy(w->ns_target_url, clean_url);
    lib_strcpy(w->ns_url, launch_url);
    lib_strcpy(w->ns_history[0], launch_url);
    netsurf_begin_navigation(win_id);
    if (netsurf_queue_wrapped_request(clean_url, vw, vh, win_id) < 0) return -1;
    extern volatile int gui_redraw_needed;
    extern volatile int need_resched;
    gui_redraw_needed = 1;
    need_resched = 1;
    w->mailbox = 1;
    return 0;
}

static hubbub_error token_handler(const hubbub_token *token, void *pw) {
    struct Window *w = (struct Window *)pw; struct ns_state *s = &ns_states[w->id];
    switch (token->type) {
        case HUBBUB_TOKEN_START_TAG: {
            const char *tag = (const char *)token->data.tag.name.ptr;
            int len = token->data.tag.name.len;
            if (!s->wrp_page && ns_tag_eq(tag, len, "form")) {
                char action[64];
                action[0] = '\0';
                ns_copy_attr_value(token, "action", action, sizeof(action));
                if (strcmp(action, "/") == 0 || strcmp(action, "") == 0) {
                    ns_enable_wrp_layout(s);
                }
            }
            if (s->wrp_page) {
                if (ns_tag_eq(tag, len, "script")) s->in_script_tag = 1;
                else if (ns_tag_eq(tag, len, "style")) s->in_style_tag = 1;
                else if (ns_tag_eq(tag, len, "pre")) s->in_pre_tag = 1;
                else if (ns_tag_eq(tag, len, "form")) {
                    s->in_form_tag = 1;
                }
                else if (ns_tag_eq(tag, len, "textarea")) {
                    s->in_textarea_tag = 1;
                }
                else if (ns_tag_eq(tag, len, "a")) {
                    char href[128], full[180];
                    if (ns_copy_attr_value(token, "href", href, sizeof(href))) {
                        ns_resolve_url_like(w->ns_url, href, full, sizeof(full));
                        ns_begin_link(s, full[0] ? full : href, strlen(full[0] ? full : href));
                    }
                }
                else if (ns_tag_eq(tag, len, "body")) {
                    s->bg_color = 0;
                    s->text_color = 10;
                }
                else if (ns_tag_eq(tag, len, "select")) {
                    ns_add_wrp_control(s, 0, "select", "", "");
                }
                else if (ns_tag_eq(tag, len, "input")) {
                    char type[32], value[128], name[64];
                    type[0] = value[0] = name[0] = '\0';
                    ns_copy_attr_value(token, "type", type, sizeof(type));
                    ns_copy_attr_value(token, "value", value, sizeof(value));
                    ns_copy_attr_value(token, "name", name, sizeof(name));
                    if (type[0] == '\0') lib_strcpy(type, "text");
                    int kind = 2;
                    if (strcmp(type, "submit") == 0 || strcmp(type, "button") == 0) kind = 1;
                    else if (strcmp(type, "text") == 0 || strcmp(type, "search") == 0) kind = 3;
                    ns_add_wrp_control(s, kind, type, value, name);
                }
                else if (ns_tag_eq(tag, len, "img")) {
                    char alt[80], src[160], full[180];
                    alt[0] = src[0] = full[0] = '\0';
                    if (!ns_copy_attr_value(token, "alt", alt, sizeof(alt))) lib_strcpy(alt, "image");
                    ns_copy_attr_value(token, "src", src, sizeof(src));
                    if (src[0]) ns_resolve_url_like(w->ns_url, src, full, sizeof(full));
                    if (src[0] && !s->has_image) {
                        s->has_image = 1;
                        s->image_loading = 1;
                        s->image_line_idx = -1;
                        lib_strcpy(s->image_url, full[0] ? full : src);
                        lib_strcpy(s->image_alt, alt);
                        if (s->in_link_tag && s->current_link_href[0]) {
                            lib_strcpy(s->image_map_href, s->current_link_href);
                            if (strstr(s->image_map_href, ".map")) {
                                s->waiting_for_id = 0;
                            }
                        }
                    }
                    ns_append_break(s);
                    ns_append_ch(s, '\x1f');
                    ns_append_break(s);
                }
                else if (ns_tag_eq(tag, len, "button")) s->in_button_tag = 1;
                else if (ns_tag_eq(tag, len, "table")) {
                    /* WRP pages should not fall back to block/table layout. */
                }
                else if (ns_tag_eq(tag, len, "tr") || ns_tag_eq(tag, len, "td") || ns_tag_eq(tag, len, "th")) {
                    /* Ignore table structure on WRP pages. */
                }
                break;
            }
            if (ns_tag_eq(tag, len, "script")) s->in_script_tag = 1;
            else if (ns_tag_eq(tag, len, "style")) s->in_style_tag = 1;
            else if (ns_tag_eq(tag, len, "pre")) s->in_pre_tag = 1;
            else if (ns_tag_eq(tag, len, "form")) {
                s->in_form_tag = 1;
            }
            else if (ns_tag_eq(tag, len, "textarea")) {
                s->in_textarea_tag = 1;
                ns_append_text(s, "[textarea]", 10);
            }
            else if (ns_tag_eq(tag, len, "table")) {
                s->in_table_tag = 1;
                s->table_cell_count = 0;
                ns_append_marker_line(s, "\x1dTABLE_BEGIN");
            }
            else if (ns_tag_eq(tag, len, "tr")) {
                s->in_row_tag = 1;
                s->table_cell_count = 0;
                ns_append_break(s);
            }
            else if (ns_tag_eq(tag, len, "td") || ns_tag_eq(tag, len, "th")) {
                if (s->in_table_tag && s->in_row_tag) {
                    if (s->table_cell_count > 0) ns_append_text(s, " | ", 3);
                    s->table_cell_count++;
                }
            }
            else if (ns_tag_eq(tag, len, "button")) s->in_button_tag = 1;
            else if (ns_tag_eq(tag, len, "select")) {
                if (s->wrp_page) {
                    ns_add_wrp_control(s, 0, "select", "", "");
                    break;
                }
            }
            else if (ns_tag_eq(tag, len, "a")) {
                char href[128], full[180];
                if (ns_copy_attr_value(token, "href", href, sizeof(href))) {
                    ns_resolve_url_like(w->ns_url, href, full, sizeof(full));
                    ns_begin_link(s, full[0] ? full : href, strlen(full[0] ? full : href));
                }
            }
            else if (ns_tag_eq(tag, len, "body")) { s->bg_color = 0; s->text_color = 10; }
            else if (ns_tag_eq(tag, len, "input")) {
                char type[32], value[128], name[64];
                type[0] = value[0] = name[0] = '\0';
                ns_copy_attr_value(token, "type", type, sizeof(type));
                ns_copy_attr_value(token, "value", value, sizeof(value));
                ns_copy_attr_value(token, "name", name, sizeof(name));
                if (type[0] == '\0') lib_strcpy(type, "text");
                if (!s->wrp_page && ns_is_wrp_input_name(name)) {
                    ns_enable_wrp_layout(s);
                    int kind = 2;
                    if (strcmp(type, "submit") == 0 || strcmp(type, "button") == 0) kind = 1;
                    else if (strcmp(type, "text") == 0 || strcmp(type, "search") == 0) kind = 3;
                    ns_add_wrp_control(s, kind, type, value, name);
                    break;
                }
                if (s->wrp_page) {
                    int kind = 2;
                    if (strcmp(type, "submit") == 0 || strcmp(type, "button") == 0) kind = 1;
                    else if (strcmp(type, "text") == 0 || strcmp(type, "search") == 0) kind = 3;
                    ns_add_wrp_control(s, kind, type, value, name);
                    break;
                }
                if (strcmp(type, "submit") == 0 || strcmp(type, "button") == 0) {
                    ns_append_text(s, "[", 1);
                    ns_append_text(s, value[0] ? value : "button", strlen(value[0] ? value : "button"));
                    ns_append_text(s, "]", 1);
                } else {
                    ns_append_text(s, "[", 1);
                    if (name[0]) {
                        ns_append_text(s, name, strlen(name));
                        ns_append_text(s, ": ", 2);
                    }
                    ns_append_text(s, value[0] ? value : "", strlen(value[0] ? value : ""));
                    ns_append_text(s, "]", 1);
                }
            }
            else if (ns_tag_eq(tag, len, "img")) {
                char alt[80], src[160], full[180];
                alt[0] = src[0] = full[0] = '\0';
                if (!ns_copy_attr_value(token, "alt", alt, sizeof(alt))) lib_strcpy(alt, "image");
                ns_copy_attr_value(token, "src", src, sizeof(src));
                if (!s->wrp_page) {
                    char ismap[16];
                    ismap[0] = '\0';
                    ns_copy_attr_value(token, "ismap", ismap, sizeof(ismap));
                    if ((src[0] && ns_str_starts_with(src, "/img/")) || ismap[0] != '\0') {
                        ns_enable_wrp_layout(s);
                    }
                }
                if (src[0]) ns_resolve_url_like(w->ns_url, src, full, sizeof(full));
                if (src[0] && !s->has_image) {
                    s->has_image = 1;
                    s->image_loading = 1;
                    s->image_line_idx = -1;
                    lib_strcpy(s->image_url, full[0] ? full : src);
                    lib_strcpy(s->image_alt, alt);
                    if (s->in_link_tag && s->current_link_href[0]) {
                        lib_strcpy(s->image_map_href, s->current_link_href);
                        // 關鍵：抓到新 Map ID 後，解除隊列鎖定
                        if (strstr(s->image_map_href, ".map")) {
                            s->waiting_for_id = 0;
                        }
                    }
                }
                /* Use a one-byte marker so word-wrap cannot split it. */
                ns_append_break(s);
                ns_append_ch(s, '\x1f');
                ns_append_break(s);
            }
            if (!s->wrp_page && ns_break_before_tag(tag, len)) ns_append_break(s);
            break;
        }
        case HUBBUB_TOKEN_CHARACTER:
            if (!s->wrp_page && !(s->wrp_page && s->in_form_tag)) ns_append_text(s, (const char *)token->data.character.ptr, token->data.character.len);
            break;
        case HUBBUB_TOKEN_END_TAG: {
            const char *tag = (const char *)token->data.tag.name.ptr;
            int len = token->data.tag.name.len;
            if (s->wrp_page) {
                if (ns_tag_eq(tag, len, "script")) s->in_script_tag = 0;
                else if (ns_tag_eq(tag, len, "style")) s->in_style_tag = 0;
                else if (ns_tag_eq(tag, len, "pre")) s->in_pre_tag = 0;
                else if (ns_tag_eq(tag, len, "form")) s->in_form_tag = 0;
                else if (ns_tag_eq(tag, len, "textarea")) s->in_textarea_tag = 0;
                else if (ns_tag_eq(tag, len, "button")) s->in_button_tag = 0;
                else if (ns_tag_eq(tag, len, "a")) ns_end_link(s);
                break;
            }
            if (ns_tag_eq(tag, len, "script")) s->in_script_tag = 0;
            else if (ns_tag_eq(tag, len, "style")) s->in_style_tag = 0;
            else if (ns_tag_eq(tag, len, "pre")) s->in_pre_tag = 0;
            else if (ns_tag_eq(tag, len, "form")) {
                s->in_form_tag = 0;
            }
            else if (ns_tag_eq(tag, len, "textarea")) {
                s->in_textarea_tag = 0;
            }
            else if (ns_tag_eq(tag, len, "table")) {
                s->in_table_tag = 0;
                ns_append_marker_line(s, "\x1dTABLE_END");
            }
            else if (ns_tag_eq(tag, len, "tr")) {
                s->in_row_tag = 0;
                ns_append_break(s);
            }
            else if (ns_tag_eq(tag, len, "td") || ns_tag_eq(tag, len, "th")) {
                /* separator handled on next cell */
            }
            else if (ns_tag_eq(tag, len, "button")) s->in_button_tag = 0;
            else if (ns_tag_eq(tag, len, "a")) ns_end_link(s);
            if (ns_break_after_tag(tag, len)) ns_append_break(s);
            break;
        }
        default: break;
    }
    return HUBBUB_OK;
}

void netsurf_init_engine(struct Window *w) {
    if (!w || w->id < 0 || w->id >= MAX_WINDOWS) return;
    struct ns_state *s = &ns_states[w->id];
    
    // 顯示緩衝區：按需分配
    if (!s->image_pixels) {
        s->image_pixels = (uint16_t *)malloc(NS_MAX_IMAGE_W * NS_MAX_IMAGE_H * sizeof(uint16_t));
        if (s->image_pixels) memset(s->image_pixels, 0xFF, NS_MAX_IMAGE_W * NS_MAX_IMAGE_H * sizeof(uint16_t));
    }
    
    // 全域共用後備緩衝區：保證解碼安全且節省空間
    if (!shared_back_buffer) {
        shared_back_buffer = (uint16_t *)malloc(NS_MAX_IMAGE_W * NS_MAX_IMAGE_H * sizeof(uint16_t));
    }
    
    ns_reset_doc(s, 0); 
    s->bg_color = 15; s->text_color = 0;
    w->v_offset = 0; w->ns_h_offset = 0; w->ns_input_active = 0;
    ns_clear_queued_actions(s);
    s->mouse_down = 0;
    if (netsurf_init_attempted[w->id]) return;
    netsurf_init_attempted[w->id] = 1;
    hubbub_parser *parser; hubbub_parser_create("UTF-8", false, &parser);
    hubbub_parser_optparams opt; opt.token_handler.handler = token_handler; opt.token_handler.pw = w;
    hubbub_parser_setopt(parser, HUBBUB_PARSER_TOKEN_HANDLER, &opt);
    netsurf_parsers[w->id] = parser;
}

void netsurf_render_frame(struct Window *w, int x, int y, int ww, int wh) {
    if (w->id < 0 || w->id >= MAX_WINDOWS) return;
    struct ns_state *s = &ns_states[w->id];

    // 關鍵：嘗試執行隊列中的動作 (Pump)
    ns_process_queued_actions(w);

    int cy = y + 64, ch = wh - 64; if (ch <= 0) return;
    int scale = terminal_font_scale(w);
    if (scale < 1) scale = 1;
    if (scale > 2) scale = 2;
    int content_x = x + 44;
    int content_x1 = x + ww - 14;
    int content_y0 = cy + 10;
    int content_y1 = y + wh - 10;
    int content_cols = (content_x1 - content_x) / (8 * scale);
    if (content_cols < 1) content_cols = 1;
    s->click_count = 0;

    static uint32_t last_log_ms[MAX_WINDOWS];
    static int last_line_count[MAX_WINDOWS];
    static int last_v_offset[MAX_WINDOWS];
    static int last_h_offset[MAX_WINDOWS];
    {
        uint32_t now = sys_now();
        if (now - last_log_ms[w->id] >= 2000 || s->line_count != last_line_count[w->id] || w->v_offset != last_v_offset[w->id] || w->ns_h_offset != last_h_offset[w->id]) {
            lib_printf("[NET] render win=%d max=%d geom=%d,%d %dx%d scale=%d cols=%d lines=%d v=%d h=%d\n", w->id, w->maximized, w->x, w->y, w->w, w->h, scale, content_cols, s->line_count, w->v_offset, w->ns_h_offset);
            last_log_ms[w->id] = now;
            last_line_count[w->id] = s->line_count;
            last_v_offset[w->id] = w->v_offset;
            last_h_offset[w->id] = w->ns_h_offset;
        }
    }
    // --- 繪製頂部工具列擴充 ---
    // [Fav] 按鈕位置：x+10, y+38
    int fav_btn_x = x + 10, fav_btn_y = y + 38, fav_btn_w = 45, fav_btn_h = 20;
    draw_bevel_rect(fav_btn_x, fav_btn_y, fav_btn_w, fav_btn_h, 
                    s->fav_active ? UI_C_PANEL_ACTIVE : UI_C_PANEL, 
                    UI_C_TEXT, UI_C_PANEL_DEEP);
    draw_text_scaled_clipped(fav_btn_x + 8, fav_btn_y + 4, "Fav", UI_C_TEXT, 1, fav_btn_x, fav_btn_y, fav_btn_x + fav_btn_w, fav_btn_y + fav_btn_h);

    if (s->fav_active) {
        content_x += 180; // 內容區域向右推，避免重疊
        content_cols = (content_x1 - content_x) / (8 * scale);
        if (content_cols < 1) content_cols = 1;
    }

    // 重新佈局（關鍵：確保側邊欄不遮擋文字）
    ns_layout(w, content_cols);
    if (w->v_offset < 0) w->v_offset = 0;
    if (w->ns_h_offset < 0) w->ns_h_offset = 0;

    // 關鍵修正：只要有圖 (image_ready)，就不清除背景，防止閃爍。
    // 如果沒圖且 line_count 也是 0，才畫背景和 Loading...
    if (!s->image_ready) {
        draw_rect_fill(x, cy, ww, ch, s->bg_color);
    }
    
    // 進度條處理...
    if (s->image_loading) {
        extern struct wget_state wget_job;
        int pct = 0;
        int is_active = (wget_job.owner_win_id == w->id && wget_job.active);
        if (is_active && wget_job.has_content_len && wget_job.content_len > 0) {
            pct = (int)((wget_job.body_len * 100) / wget_job.content_len);
            if (pct > 100) pct = 100;
        } else {
            pct = (int)((sys_now() / 10) % 100);
        }
        int bar_w = (ww * pct) / 100;
        if (bar_w < 10) bar_w = 10;
        draw_rect_fill(x, cy, ww, 3, UI_C_PANEL_DEEP); 
        draw_rect_fill(x, cy, bar_w, 3, UI_C_PANEL_ACTIVE); 
    }
    
    if (s->line_count <= 0 && !s->image_ready) {
        draw_text_scaled_clipped(content_x, content_y0, "Loading...", s->text_color, scale, content_x, content_y0, content_x1, content_y1);
        return;
    }

    if (s->line_count > 0) {
        if (w->v_offset > s->line_count - 1) w->v_offset = s->line_count - 1;
        if (w->ns_h_offset > s->max_line_len) w->ns_h_offset = s->max_line_len;
    }

    int vis = ch / (18 * scale), cur_y = cy + 10;
    if (vis < 1) vis = 1;
    int h_off = w->ns_h_offset;

    // --- 強制圖片預繪 (消除閃爍) ---
    if (s->image_ready) {
        int draw_x = content_x;
        int draw_y = cur_y + 4;
        int max_w = content_x1 - content_x;
        int max_h = content_y1 - draw_y;
        int draw_w = max_w;
        int draw_h = max_h;
        if (draw_w < 1) draw_w = 1;
        if (draw_h < 1) draw_h = 1;
        s->image_draw_x = draw_x;
        s->image_draw_y = draw_y;
        s->image_draw_w = draw_w;
        s->image_draw_h = draw_h;
        ns_add_click_region(s, 2, draw_x, draw_y, draw_w, draw_h, -1, s->image_map_href, 0, 0, 0);
        ns_draw_image_high_quality(s, draw_x, draw_y, draw_w, draw_h, content_x, content_y0, content_x1, content_y1);
        
        if (s->hover_click_idx >= 0 && s->hover_click_idx < s->click_count) {
            struct ns_click *hc = &s->clicks[s->hover_click_idx];
            if (hc->kind == 2) {
                draw_rect_fill(hc->x, hc->y, hc->w, 1, UI_C_PANEL_ACTIVE);
                draw_rect_fill(hc->x, hc->y + hc->h - 1, hc->w, 1, UI_C_PANEL_ACTIVE);
                draw_rect_fill(hc->x, hc->y, 1, hc->h, UI_C_PANEL_ACTIVE);
                draw_rect_fill(hc->x + hc->w - 1, hc->y, 1, hc->h, UI_C_PANEL_ACTIVE);
            }
        }
    }

    int form_inline_active = 0;
    int form_inline_x = content_x;
    int form_inline_y = cur_y;
    int form_inline_h = 0;

    if (s->wrp_page) {
        goto netsurf_render_scrollbars;
    }

    for (int i = 0; i < vis; i++) {
        int idx = w->v_offset + i; if (idx >= s->line_count) break;
        char *line = s->lines[idx];
        if (line[0] == '\0' && form_inline_active) continue;
        if (line[0] == '\x1f' && line[1] == '\0') {
            if (s->image_ready) {
                int draw_x = x + 15 - h_off * 8;
                int draw_y = cur_y;
                int draw_w = s->image_w * scale;
                int draw_h = s->image_h * scale;
                s->image_draw_x = draw_x;
                s->image_draw_y = draw_y;
                s->image_draw_w = draw_w;
                s->image_draw_h = draw_h;
                ns_add_click_region(s, 2, draw_x, draw_y, draw_w, draw_h, idx, s->image_map_href, 0, 0, 0);
                ns_draw_image_high_quality(s, draw_x, draw_y, draw_w, draw_h, content_x, content_y0, content_x1, content_y1);
                cur_y += (s->image_h * scale) + 10;
            } else {
                draw_bevel_rect(x + 12, cur_y, 96 * scale, 18 * scale, UI_C_PANEL, UI_C_TEXT_MUTED, UI_C_PANEL_DEEP);
                draw_text_scaled_clipped(x + 16, cur_y + 2, s->image_alt[0] ? s->image_alt : "image loading", UI_C_TEXT, scale, content_x, content_y0, content_x1, content_y1);
                cur_y += 20 * scale;
            }
        } else if (line[0] == '\x1d' && strncmp(line + 1, "TABLE_BEGIN", 11) == 0) {
            int end = idx + 1;
            int inner_lines = 0;
            while (end < s->line_count) {
                if (s->lines[end][0] == '\x1d' && strncmp(s->lines[end] + 1, "TABLE_END", 9) == 0) break;
                inner_lines++;
                end++;
            }
            int box_h = (inner_lines > 0 ? inner_lines : 1) * (18 * scale) + 12;
            ns_draw_bevel_rect_clipped(content_x, cur_y, content_x1 - content_x - 6, box_h,
                                       UI_C_PANEL, UI_C_TEXT_MUTED, UI_C_PANEL_DEEP,
                                       content_x, content_y0, content_x1, content_y1);
            int row_y = cur_y + 6;
            for (int j = idx + 1; j < end; j++) {
                char *row = s->lines[j];
                if (row[0] == '\x1d') continue;
                draw_text_scaled_clipped(content_x + 8, row_y, row, s->text_color, scale, content_x + 8, content_y0, content_x1 - 8, content_y1);
                row_y += 18 * scale;
            }
            cur_y += box_h + 8;
            i = end;
        } else if (line[0] == '\x1d' && strncmp(line + 1, "TABLE_END", 9) == 0) {
            continue;
        } else if (line[0] == '\x1d' && strncmp(line + 1, "INPUT|", 6) == 0) {
            if (s->wrp_page) {
                continue;
            }
            char tmp[240];
            char type[32];
            char value[128];
            char name[64];
            const char *display = 0;
            int display_len = 0;
            int box_w = 0;
            int box_h = 22 * scale;
            int draw_x = content_x;
            int draw_y = cur_y;
            type[0] = value[0] = name[0] = '\0';
            if (!ns_parse_input_marker(line, type, sizeof(type), value, sizeof(value), name, sizeof(name))) {
                cur_y += 22 * scale;
                continue;
            }
            tmp[0] = '\0';
            if (strcmp(type, "submit") == 0 || strcmp(type, "button") == 0) {
                display = (value[0] ? value : type);
            } else if (strcmp(type, "text") == 0 || strcmp(type, "search") == 0) {
                display = (value[0] ? value : "");
            } else {
                display = (value[0] ? value : type);
            }
            while (display[display_len]) display_len++;
            box_w = 18 + display_len * (4 * scale);
            if (box_w < 28 * scale) box_w = 28 * scale;
            if (strcmp(type, "submit") == 0 || strcmp(type, "button") == 0) {
                if (box_w > 72 * scale) box_w = 72 * scale;
            } else if (strcmp(type, "text") == 0 || strcmp(type, "search") == 0) {
                if (box_w > 132 * scale) box_w = 132 * scale;
            } else {
                if (box_w > 96 * scale) box_w = 96 * scale;
            }
            if (form_inline_active) {
                draw_y = form_inline_y;
                draw_x = form_inline_x;
            }
            ns_draw_bevel_rect_clipped(draw_x, draw_y, box_w, box_h,
                                       UI_C_PANEL, UI_C_TEXT_MUTED, UI_C_PANEL_DEEP,
                                       content_x, content_y0, content_x1, content_y1);
            ns_add_click_region(s, 3, draw_x, draw_y, box_w, box_h, idx, 0, type, value, name);
            draw_text_scaled_clipped(draw_x + 8, draw_y + 4, display, UI_C_TEXT, scale, content_x + 4, content_y0, content_x1 - 4, content_y1);
            if (form_inline_active) {
                form_inline_x = draw_x + box_w + (6 * scale);
                if (box_h > form_inline_h) form_inline_h = box_h;
            } else {
                cur_y += box_h + 8;
            }
        } else if (ns_marker_is(line, "FORM_BEGIN")) {
            if (s->wrp_page) continue;
            form_inline_active = 1;
            form_inline_x = content_x;
            form_inline_y = cur_y;
            form_inline_h = 0;
            continue;
        } else if (ns_marker_is(line, "FORM_END")) {
            if (form_inline_active) {
                cur_y = form_inline_y + form_inline_h + 8;
                form_inline_active = 0;
            }
            continue;
        } else if (ns_marker_is(line, "TEXTAREA_BEGIN")) {
            if (s->wrp_page) {
                continue;
            }
            int inner_lines = ns_count_lines_until_marker(s, idx, "TEXTAREA_END");
            int box_h = inner_lines * (18 * scale) + 14;
            int box_w = 96 * scale;
            if (box_h < 20 * scale) box_h = 20 * scale;
            ns_draw_bevel_rect_clipped(content_x, cur_y, box_w, box_h,
                                       UI_C_PANEL, UI_C_TEXT_MUTED, UI_C_PANEL_DEEP,
                                       content_x, content_y0, content_x1, content_y1);
            cur_y += 4;
        } else if (ns_marker_is(line, "TEXTAREA_END")) {
            continue;
        } else {
            if (h_off < (int)strlen(line)) {
                int text_w = (int)strlen(line + h_off) * (8 * scale);
                if (text_w > (content_x1 - content_x)) text_w = content_x1 - content_x;
                if (text_w < 8) text_w = 8;
                {
                    struct ns_link *lnk = ns_find_link_by_line(s, idx);
                    if (lnk) ns_add_click_region(s, 1, content_x, cur_y, text_w, 18 * scale, idx, lnk->href, 0, 0, 0);
                }
                draw_text_scaled_clipped(content_x, cur_y, line + h_off, s->text_color, scale, content_x, content_y0, content_x1, content_y1);
            }
            cur_y += 18 * scale;
        }
    }

netsurf_render_scrollbars:
    // 垂直捲軸 (右側)
    if (s->line_count > vis) {
        int th = (ch * vis) / s->line_count; if (th < 15) th = 15;
        int ty = cy + (w->v_offset * (ch - th)) / (s->line_count - vis);
        draw_rect_fill(x + ww - 6, cy, 4, ch, UI_C_PANEL_DEEP);
        draw_rect_fill(x + ww - 6, ty, 4, th, UI_C_SCROLL_THUMB);
    }
    // 水平捲軸 (底部)
    int max_h = s->max_line_len - content_cols;
    if (max_h > 0) {
        int tw = (ww * content_cols) / s->max_line_len; if (tw < 20) tw = 20;
        int tx = x + (w->ns_h_offset * (ww - tw)) / max_h;
        draw_rect_fill(x, y + wh - 6, ww, 4, UI_C_PANEL_DEEP);
        draw_rect_fill(tx, y + wh - 6, tw, 4, UI_C_SCROLL_THUMB);
    }

    if (s->fav_active) {
        int side_w = 180, side_h = ch - 20;
        int side_x = x + 10, side_y = cy + 10;
        int item_h = 22;
        draw_rect_fill(side_x, side_y, side_w, side_h, UI_C_PANEL_DARK);
        draw_rect_fill(side_x, side_y, side_w, 1, UI_C_BORDER);
        draw_rect_fill(side_x, side_y + side_h - 1, side_w, 1, UI_C_BORDER);
        draw_rect_fill(side_x, side_y, 1, side_h, UI_C_BORDER);
        draw_rect_fill(side_x + side_w - 1, side_y, 1, side_h, UI_C_BORDER);

        for (int i = 0; i < NS_FAV_COUNT; i++) {
            int ix = side_x + 10, iy = side_y + 10 + i * item_h;
            int is_hover = (gui_mx >= ix && gui_mx < side_x + side_w - 10 &&
                            gui_my >= iy && gui_my < iy + item_h);
            draw_text_scaled_clipped(ix, iy, ns_fav_titles[i],
                                     is_hover ? UI_C_PANEL_ACTIVE : UI_C_TEXT,
                                     1, side_x, side_y, side_x + side_w, side_y + side_h);
            if (is_hover) draw_rect_fill(ix, iy + 14, strlen(ns_fav_titles[i]) * 8, 1, UI_C_PANEL_ACTIVE);
            ns_add_click_region(s, 1, ix, iy, side_w - 20, item_h, -1, ns_fav_urls[i], 0, 0, 0);
        }
    }
}

void netsurf_handle_input(struct Window *w, int mx, int my, int clicked) {
    struct ns_state *s = &ns_states[w->id];
    int scale = terminal_font_scale(w);
    int ch = (w->maximized ? DESKTOP_H : w->h) - 64;
    int vis = ch / (18 * scale);
    int click_once = clicked && !s->mouse_down;
    s->mouse_down = clicked ? 1 : 0;

    // Hover detection
    s->hover_click_idx = -1;
    struct ns_click *hc = ns_hit_click_region(s, mx, my);
    if (hc) {
        for (int i = 0; i < s->click_count; i++) {
            if (&s->clicks[i] == hc) { s->hover_click_idx = i; break; }
        }
    } else {
        struct ns_wrp_control *hwc = ns_hit_wrp_control(s, mx, my);
        if (hwc) {
            // Use high bit or offset for WRP control hover if needed, 
            // but for now just focus on click regions
        }
    }

    if (gui_wheel != 0) {
        if (gui_ctrl_pressed) {
            w->ns_h_offset += gui_wheel * 2;
            int max_h = s->max_line_len - ((w->maximized?WIDTH:w->w)-40)/(8*scale);
            if (w->ns_h_offset > max_h) w->ns_h_offset = (max_h > 0 ? max_h : 0);
            if (w->ns_h_offset < 0) w->ns_h_offset = 0;
            gui_wheel = 0;
        } else if (s->wrp_page && s->image_ready) {
            // --- WRP 遠端捲動實作 (入隊) ---
            char kstr[32];
            lib_strcpy(kstr, (gui_wheel > 0) ? "PageUp" : "PageDown");
            lib_printf("[NET] ENQUEUE SCROLL: %s\n", kstr);
            ns_enqueue_action(s, mx, my, kstr, 0, 0);
            ns_process_queued_actions(w);
            gui_wheel = 0;
            return;
        } else {
            w->v_offset -= gui_wheel * 2;
            if (w->v_offset > s->line_count - vis) w->v_offset = (s->line_count > vis ? s->line_count - vis : 0);
            if (w->v_offset < 0) w->v_offset = 0;
            gui_wheel = 0;
        }
        extern volatile int gui_redraw_needed; gui_redraw_needed = 1;
    }
    if (gui_ctrl_pressed && gui_key != 0) {
        if (gui_key == '+' || gui_key == '=') { w->term_font_scale++; if(w->term_font_scale > 4) w->term_font_scale = 4; gui_key = 0; }
        if (gui_key == '-' || gui_key == '_') { w->term_font_scale--; if(w->term_font_scale < 1) w->term_font_scale = 1; gui_key = 0; }
        extern volatile int gui_redraw_needed; gui_redraw_needed = 1;
    }
    if (click_once) {
        int rx = mx - (w->maximized ? 0 : w->x), ry = my - (w->maximized ? 0 : w->y);
        
        // 1. [Fav] 按鈕點擊 (優先處理)
        if (rx >= 10 && rx <= 55 && ry >= 38 && ry <= 58) {
            s->fav_active = !s->fav_active;
            extern volatile int gui_redraw_needed; gui_redraw_needed = 1;
            return;
        }

        // 2. 如果最愛選單開啟中，攔截所有點擊
        if (s->fav_active) {
            int side_x = (w->maximized ? 0 : w->x) + 10;
            int side_y = (w->maximized ? 0 : w->y) + 64 + 10;
            int side_w = 180;
            int side_h = ch - 20;
            int item_h = 22;
            
            if (mx >= side_x && mx < side_x + side_w &&
                my >= side_y && my < side_y + side_h) {
                int rel_y = my - (side_y + 10);
                if (mx >= side_x + 10 && mx < side_x + side_w - 10 && rel_y >= 0) {
                    int idx = rel_y / item_h;
                    if (idx >= 0 && idx < NS_FAV_COUNT && rel_y < (idx + 1) * item_h) {
                        lib_printf("[NET] FAV JUMP: %s\n", ns_fav_urls[idx]);
                        s->fav_active = 0;
                        ns_start_document_navigation(w, ns_fav_urls[idx]);
                        return;
                    }
                }
                extern volatile int gui_redraw_needed;
                gui_redraw_needed = 1;
                return;
            }
            s->fav_active = 0;
            extern volatile int gui_redraw_needed; gui_redraw_needed = 1;
            return;
        }

        // 3. 地址列點擊
        if (ry >= 38 && ry <= 58 && rx >= 75) {
            w->ns_input_active = 1;
            s->active_ctrl_idx = -1;
            if (w->ns_target_url[0] == '\0') {
                netsurf_normalize_target_url(w->ns_url, w->ns_target_url, sizeof(w->ns_target_url));
            }
        } else {
            w->ns_input_active = 0;
            if (s->wrp_page) {
                struct ns_wrp_control *wc = ns_hit_wrp_control(s, mx, my);
                if (wc) {
                    s->active_ctrl_idx = (int)(wc - s->wrp_ctrls);
                    if (wc->kind == 1 && wc->value[0]) {
                        struct ns_click fake;
                        memset(&fake, 0, sizeof(fake));
                        lib_strcpy(fake.input_value, wc->value);
                        lib_strcpy(fake.input_name, wc->name);
                        ns_follow_submit(w, &fake);
                    }
                    gui_key = 0;
                    extern volatile int gui_redraw_needed; gui_redraw_needed = 1;
                    return;
                }
            }
            // 內容區域點擊：直接使用實際繪圖區域進行判定
            struct ns_click *c = ns_hit_click_region(s, mx, my);
            if (!c && s->image_ready) {
                // 如果沒有命中文字連結，但在圖片區域內，則視為命中圖片 (ISMAP)
                if (mx >= s->image_draw_x && mx < s->image_draw_x + s->image_draw_w &&
                    my >= s->image_draw_y && my < s->image_draw_y + s->image_draw_h) {
                    
                    // 尋找對應的 Image Map 連結 (通常是 kind 2)
                    for (int i = 0; i < s->click_count; i++) {
                        if (s->clicks[i].kind == 2) {
                            c = &s->clicks[i];
                            break;
                        }
                    }
                }
            }

            if (c) {
                if (c->kind == 2 && s->image_ready) {
                    char map_url[256];
                    char num[16];
                    int relx = mx - s->image_draw_x;
                    int rely = my - s->image_draw_y;
                    if (relx < 0) relx = 0;
                    if (rely < 0) rely = 0;
                    if (s->image_draw_w > 0) relx = relx * s->image_w / s->image_draw_w;
                    if (s->image_draw_h > 0) rely = rely * s->image_h / s->image_draw_h;
                    if (relx >= s->image_w) relx = s->image_w - 1;
                    if (rely >= s->image_h) rely = s->image_h - 1;

                    // 決定導覽目標 (ISMAP 需要發送到 .map 結尾的 URL)
                    if (c->href[0] && strstr(c->href, ".map")) {
                        lib_strcpy(map_url, c->href);
                    } else if (s->image_map_href[0] && strstr(s->image_map_href, ".map")) {
                        lib_strcpy(map_url, s->image_map_href);
                    } else if (s->image_url[0]) {
                        // 強大回退：從「目前顯示的圖片」URL 派生 Map URL
                        lib_strcpy(map_url, s->image_url);
                        char *img_pos = strstr(map_url, "/img/");
                        if (img_pos) {
                            char rest[256];
                            lib_strcpy(rest, img_pos + 5); 
                            char *dot = strrchr(rest, '.'); // 找最後一個點
                            if (dot) *dot = '\0';
                            lib_strcpy(img_pos, "/map/");
                            lib_strcat(map_url, rest);
                            lib_strcat(map_url, ".map");
                        }
                    } else {
                        lib_strcpy(map_url, w->ns_url);
                    }

                    // 清除舊參數並附加座標
                    char *q = strchr(map_url, '?');
                    if (q) *q = '\0'; 

                    lib_strcat(map_url, "?");
                    lib_itoa((uint32_t)relx, num);
                    lib_strcat(map_url, num);
                    lib_strcat(map_url, ",");
                    lib_itoa((uint32_t)rely, num);
                    lib_strcat(map_url, num);

                    lib_printf("[NET] ENQUEUE CLICK target='%s'\n", map_url);
                    ns_enqueue_action(s, mx, my, 0, 1, map_url);
                    ns_process_queued_actions(w); // 立即觸發處理
                    extern volatile int gui_redraw_needed; gui_redraw_needed = 1;
                    return;
                } else if (c->kind == 1 && c->href[0]) {
                    ns_start_navigation(w, c->href);
                    extern volatile int gui_redraw_needed; gui_redraw_needed = 1;
                    return;
                }
            }
        }
    }

    // Keyboard input for WRP (Queueing)
    if (gui_key != 0 && !w->ns_input_active && s->wrp_page && s->image_ready) {
        char kstr[32];
        kstr[0] = '\0';

        if (gui_key == 8) lib_strcpy(kstr, "Backspace");
        else if (gui_key == '\n' || gui_key == '\r') lib_strcpy(kstr, "Return");
        else if (gui_key == '\t') lib_strcpy(kstr, "Tab");
        else if (gui_key == 27) lib_strcpy(kstr, "Escape");
        else if (gui_key == 0x10) lib_strcpy(kstr, "Up");
        else if (gui_key == 0x11) lib_strcpy(kstr, "Down");
        else if (gui_key == 0x12) lib_strcpy(kstr, "Left");
        else if (gui_key == 0x13) lib_strcpy(kstr, "Right");
        else if (gui_key == 0x14) lib_strcpy(kstr, "Delete");
        else if (gui_key == 0x15) lib_strcpy(kstr, "Home");
        else if (gui_key == 0x16) lib_strcpy(kstr, "End");
        else if (gui_key >= 32 && gui_key <= 126) {
            kstr[0] = gui_key;
            kstr[1] = '\0';
        }

        if (kstr[0]) {
            lib_printf("[NET] ENQUEUE KEY k='%s'\n", kstr);
            ns_enqueue_action(s, mx, my, kstr, 0, 0);
            ns_process_queued_actions(w); // 立即觸發處理
            extern volatile int gui_redraw_needed; gui_redraw_needed = 1;
            gui_key = 0;
            return;
        }
    }

    if (w->ns_input_active && gui_key != 0) {
        char k = gui_key; int len = strlen(w->ns_target_url);
        if (k == '\n' || k == '\r') {
            lib_printf("[NET] enter win=%d url='%s'\n", w->id, w->ns_target_url);
            w->ns_input_active = 0;
            ns_start_document_navigation(w, w->ns_target_url);
        } else if (k == 8 && len > 0) {
            w->ns_target_url[len-1] = '\0';
        } else if (len < 250 && k >= 32 && k <= 126) {
            // 嚴格過濾：只允許標準 ASCII 可見字元，防止掃描碼或亂碼進入網址
            w->ns_target_url[len] = k;
            w->ns_target_url[len+1] = '\0';
        }
        gui_key = 0;
    }
}

void netsurf_feed_data(int win_id, const uint8_t *data, size_t len) {
    if (win_id >= 0 && win_id < MAX_WINDOWS && netsurf_parsers[win_id]) {
        struct ns_state *s = &ns_states[win_id];
        
        // 如果有掛起的重置，在此時（收到新數據時）執行
        if (s->nav_reset_pending) {
            ns_reset_doc(s, 1); // 保留舊圖
            s->nav_reset_pending = 0;
            s->wrp_page = ns_is_wrp_control_url(wins[win_id].ns_url);
        }
        
        lib_printf("[NET] feed win=%d len=%u\n", win_id, (unsigned)len);
        if (data && len > 0 && s->source_len < (int)sizeof(s->source) - 1) {
            int copy_len = (int)len;
            int remain = (int)sizeof(s->source) - 1 - s->source_len;
            if (copy_len > remain) copy_len = remain;
            if (copy_len > 0) {
                memcpy(s->source + s->source_len, data, (uint32_t)copy_len);
                s->source_len += copy_len;
                s->source[s->source_len] = '\0';
            }
        }
        hubbub_parser_parse_chunk(netsurf_parsers[win_id], data, len);
        extern volatile int gui_redraw_needed;
        extern volatile int need_resched;
        gui_redraw_needed = 1;
        need_resched = 1;
    }
}

void netsurf_complete_data(int win_id) {
    if (win_id >= 0 && win_id < MAX_WINDOWS && netsurf_parsers[win_id]) {
        struct ns_state *s = &ns_states[win_id];
        char img_fallback[160];
        lib_printf("[NET] complete win=%d lines=%d raw=%d image=%d ready=%d\n",
                   win_id, s->line_count, s->raw_len, s->has_image, s->image_ready);
        hubbub_parser_completed(netsurf_parsers[win_id]);
        if (s->wrp_page && !s->has_image && ns_find_wrp_img_ref(s->source, img_fallback, sizeof(img_fallback))) {
            char wrp_base[160];
            lib_printf("[NET] image_fallback win=%d ref='%s'\n", win_id, img_fallback);
            if (ssh_client_get_wrp_url(wrp_base, sizeof(wrp_base)) == 0) {
                ns_resolve_url_like(wrp_base, img_fallback, s->image_url, sizeof(s->image_url));
                s->has_image = 1;
                s->image_loading = 1;
                s->image_line_idx = -1;
                if (s->raw_len == 0 || s->raw[s->raw_len - 1] != '\n') ns_append_ch(s, '\n');
                ns_append_ch(s, '\x1f');
                if (s->raw_len == 0 || s->raw[s->raw_len - 1] != '\n') ns_append_ch(s, '\n');
                netsurf_invalidate_layout(win_id);
            }
        }
        if (s->has_image && s->image_url[0]) {
            char host[32], path[96]; uint16_t port = 0;
            lib_printf("[NET] image_ref win=%d url='%s'\n", win_id, s->image_url);
            if (parse_wget_url(s->image_url, host, &port, path)) {
                lib_printf("[NET] image_queue win=%d host=%s port=%u path=%s\n",
                           win_id, host, (unsigned)port, path);
                s->image_loading = 1;
                wget_queue_request_ex(host, path, "__ns.bmp", port, (strncmp(s->image_url,"https",5)==0), 0, 1, win_id);
            } else {
                char wrp_base[160];
                const char *img_path = strstr(s->image_url, "/img/");
                uint16_t wrp_port = 0;
                char wrp_host[32];
                char wrp_path[96];
                lib_printf("[NET] image_url_parse_failed win=%d url='%s'\n", win_id, s->image_url);
                if (img_path &&
                    ssh_client_get_wrp_url(wrp_base, sizeof(wrp_base)) == 0 &&
                    parse_wget_url(wrp_base, wrp_host, &wrp_port, wrp_path)) {
                    lib_printf("[NET] image_retry win=%d host=%s port=%u path=%s\n",
                               win_id, wrp_host, (unsigned)wrp_port, img_path);
                    s->image_loading = 1;
                    wget_queue_request_ex(wrp_host, img_path, "__ns.bmp", wrp_port, 0, 0, 1, win_id);
                }
            }
        }
        extern volatile int gui_redraw_needed;
        extern volatile int need_resched;
        gui_redraw_needed = 1;
        need_resched = 1;

        if (s->waiting_for_id && s->wrp_page &&
            (!s->image_map_href[0] || !strstr(s->image_map_href, ".map"))) {
            lib_printf("[NET] Q-CLEAR no fresh map after response\n");
            ns_clear_queued_actions(s);
        }

        if (!s->has_image || !s->image_url[0]) {
            ns_process_queued_actions(&wins[win_id]);
        }
    }
}

void netsurf_receive_image(int win_id, const char *url, const uint8_t *data, size_t len) {
    if (win_id < 0 || win_id >= MAX_WINDOWS) return;
    struct ns_state *s = &ns_states[win_id];
    if (!s->image_pixels || !shared_back_buffer) return;
    lib_printf("[NET] image win=%d bytes=%u\n", win_id, (unsigned)len);
    int img_w = 0, img_h = 0;

    // 1. 解碼到共用的後備緩衝區 (安全解碼空間)
    if (decode_image_to_rgb565(data, (uint32_t)len, shared_back_buffer, NS_MAX_IMAGE_W, NS_MAX_IMAGE_H, &img_w, &img_h) != 0) {
        lib_printf("[NET] image decode failed win=%d\n", win_id);
        s->image_loading = 0;
        return;
    }

    // 2. 解碼成功，拷貝到該視窗的專屬顯示緩衝區
    memcpy(s->image_pixels, shared_back_buffer, NS_MAX_IMAGE_W * NS_MAX_IMAGE_H * sizeof(uint16_t));

    s->image_w = img_w;
    s->image_h = img_h;
    s->image_ready = 1;
    s->image_loading = 0;
    
    // 關鍵：解碼完成後主動觸發隊列噴發
    ns_process_queued_actions(&wins[win_id]);

    extern volatile int gui_redraw_needed;
    extern volatile int need_resched;
    gui_redraw_needed = 1;
    need_resched = 1;
}

void netsurf_release_window(int win_id) {
    if (win_id >= 0 && win_id < MAX_WINDOWS) {
        if (netsurf_parsers[win_id]) { hubbub_parser_destroy(netsurf_parsers[win_id]); netsurf_parsers[win_id] = 0; }
        netsurf_init_attempted[win_id] = 0;
    }
}
