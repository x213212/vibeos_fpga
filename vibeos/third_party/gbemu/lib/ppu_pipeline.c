#include "../include/common.h"

// 極速版像素 FIFO：使用環狀陣列取代鏈結串列
static u32 fifo_buffer[16];
static int fifo_head = 0;
static int fifo_tail = 0;
static int fifo_count = 0;

bool window_visible() {
    lcd_context *l = lcd_get_context();
    return LCDC_WIN_ENABLE && l->win_x >= 0 && l->win_x <= 166 && l->win_y >= 0 && l->win_y < YRES;
}

void pixel_fifo_push(u32 value) {
    fifo_buffer[fifo_tail] = value;
    fifo_tail = (fifo_tail + 1) & 15;
    fifo_count++;
}

u32 pixel_fifo_pop() {
    u32 val = fifo_buffer[fifo_head];
    fifo_head = (fifo_head + 1) & 15;
    fifo_count--;
    return val;
}

void pipeline_fifo_reset() {
    fifo_head = fifo_tail = fifo_count = 0;
    ppu_get_context()->pfc.pixel_fifo.size = 0;
}

u32 fetch_sprite_pixels(int bit, u32 color, u8 bg_color) {
    ppu_context *p = ppu_get_context();
    lcd_context *l = lcd_get_context();
    for (int i=0; i<p->fetched_entry_count; i++) {
        int sp_x = (p->fetched_entries[i].x - 8) + (l->scroll_x % 8);
        if (sp_x + 8 < p->pfc.fifo_x) continue;
        int offset = p->pfc.fifo_x - sp_x;
        if (offset < 0 || offset > 7) continue;
        bit = (7 - offset);
        if (p->fetched_entries[i].f_x_flip) bit = offset;
        u8 hi = !!(p->pfc.fetch_entry_data[i * 2] & (1 << bit));
        u8 lo = !!(p->pfc.fetch_entry_data[(i * 2) + 1] & (1 << bit)) << 1;
        if (!(hi|lo)) continue;
        if (!p->fetched_entries[i].f_bgp || bg_color == 0) {
            color = (p->fetched_entries[i].f_pn) ? l->sp2_colors[hi|lo] : l->sp1_colors[hi|lo];
            if (hi|lo) break;
        }
    }
    return color;
}

bool pipeline_fifo_add() {
    if (fifo_count > 8) return false;
    ppu_context *p = ppu_get_context();
    lcd_context *l = lcd_get_context();
    int x = p->pfc.fetch_x - (8 - (l->scroll_x % 8));
    for (int i=0; i<8; i++) {
        int bit = 7 - i;
        u8 hi = !!(p->pfc.bgw_fetch_data[1] & (1 << bit));
        u8 lo = !!(p->pfc.bgw_fetch_data[2] & (1 << bit)) << 1;
        u32 color = l->bg_colors[hi | lo];
        if (!LCDC_BGW_ENABLE) color = l->bg_colors[0];
        if (LCDC_OBJ_ENABLE) color = fetch_sprite_pixels(bit, color, hi | lo);
        if (x >= 0) {
            pixel_fifo_push(color);
            p->pfc.fifo_x++;
        }
    }
    p->pfc.pixel_fifo.size = fifo_count;
    return true;
}

void pipeline_load_sprite_tile() {
    ppu_context *p = ppu_get_context();
    oam_line_entry *le = p->line_sprites;
    while(le) {
        int sp_x = (le->entry.x - 8) + (lcd_get_context()->scroll_x % 8);
        if ((sp_x >= p->pfc.fetch_x && sp_x < p->pfc.fetch_x + 8) ||
            ((sp_x + 8) >= p->pfc.fetch_x && (sp_x + 8) < p->pfc.fetch_x + 8)) {
            p->fetched_entries[p->fetched_entry_count++] = le->entry;
        }
        le = le->next;
        if (!le || p->fetched_entry_count >= 3) break;
    }
}

void pipeline_load_sprite_data(u8 offset) {
    ppu_context *p = ppu_get_context();
    int cur_y = lcd_get_context()->ly;
    u8 sprite_height = LCDC_OBJ_HEIGHT;
    for (int i=0; i<p->fetched_entry_count; i++) {
        u8 ty = ((cur_y + 16) - p->fetched_entries[i].y) * 2;
        if (p->fetched_entries[i].f_y_flip) ty = ((sprite_height * 2) - 2) - ty;
        u8 tile_index = p->fetched_entries[i].tile;
        if (sprite_height == 16) tile_index &= ~(1);
        p->pfc.fetch_entry_data[(i * 2) + offset] = p->vram[(0x8000 + (tile_index * 16) + ty + offset) - 0x8000];
    }
}

void pipeline_load_window_tile() {
    ppu_context *p = ppu_get_context();
    lcd_context *l = lcd_get_context();
    if (!LCDC_WIN_ENABLE || l->win_x < 0 || l->win_x > 166 || l->win_y < 0 || l->win_y >= YRES) return;
    if (p->pfc.fetch_x + 7 >= l->win_x && p->pfc.fetch_x + 7 < l->win_x + YRES + 14) {
        if (l->ly >= l->win_y && l->ly < l->win_y + XRES) {
            u8 w_tile_y = p->window_line / 8;
            p->pfc.bgw_fetch_data[0] = p->vram[(LCDC_WIN_MAP_AREA + ((p->pfc.fetch_x + 7 - l->win_x) / 8) + (w_tile_y * 32)) - 0x8000];
            if (LCDC_BGW_DATA_AREA == 0x8800) p->pfc.bgw_fetch_data[0] += 128;
        }
    }
}

void pipeline_fetch() {
    ppu_context *p = ppu_get_context();
    switch(p->pfc.cur_fetch_state) {
        case FS_TILE: {
            p->fetched_entry_count = 0;
            if (LCDC_BGW_ENABLE) {
                p->pfc.bgw_fetch_data[0] = p->vram[(LCDC_BG_MAP_AREA + (p->pfc.map_x / 8) + (((p->pfc.map_y / 8)) * 32)) - 0x8000];
                if (LCDC_BGW_DATA_AREA == 0x8800) p->pfc.bgw_fetch_data[0] += 128;
                pipeline_load_window_tile();
            }
            if (LCDC_OBJ_ENABLE && p->line_sprites) pipeline_load_sprite_tile();
            p->pfc.cur_fetch_state = FS_DATA0;
            p->pfc.fetch_x += 8;
        } break;
        case FS_DATA0: {
            p->pfc.bgw_fetch_data[1] = p->vram[(LCDC_BGW_DATA_AREA + (p->pfc.bgw_fetch_data[0] * 16) + p->pfc.tile_y) - 0x8000];
            pipeline_load_sprite_data(0);
            p->pfc.cur_fetch_state = FS_DATA1;
        } break;
        case FS_DATA1: {
            p->pfc.bgw_fetch_data[2] = p->vram[(LCDC_BGW_DATA_AREA + (p->pfc.bgw_fetch_data[0] * 16) + p->pfc.tile_y + 1) - 0x8000];
            pipeline_load_sprite_data(1);
            p->pfc.cur_fetch_state = FS_IDLE;
        } break;
        case FS_IDLE: p->pfc.cur_fetch_state = FS_PUSH; break;
        case FS_PUSH: if (pipeline_fifo_add()) p->pfc.cur_fetch_state = FS_TILE; break;
    }
}

void pipeline_push_pixel() {
    ppu_context *p = ppu_get_context();
    lcd_context *l = lcd_get_context();
    if (fifo_count > 8) {
        u32 pixel_data = pixel_fifo_pop();
        if (p->pfc.line_x >= (l->scroll_x % 8)) {
            p->video_buffer[p->pfc.pushed_x + (l->ly * XRES)] = pixel_data;
            p->pfc.pushed_x++;
        }
        p->pfc.line_x++;
    }
    p->pfc.pixel_fifo.size = fifo_count;
}

void pipeline_process() {
    ppu_context *p = ppu_get_context();
    lcd_context *l = lcd_get_context();
    p->pfc.map_y = (l->ly + l->scroll_y);
    p->pfc.map_x = (p->pfc.fetch_x + l->scroll_x);
    p->pfc.tile_y = (p->pfc.map_y % 8) * 2;
    if (!(p->line_ticks & 1)) pipeline_fetch();
    pipeline_push_pixel();
}
