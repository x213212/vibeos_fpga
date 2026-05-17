#ifndef VGA_H

#include <stdint.h>

#ifdef FPGA_MINIMAL
#define WIDTH  512
#define HEIGHT 384
#else
#define WIDTH  1024
#define HEIGHT 768
#endif

#define CURSOR_NORMAL 0
#define CURSOR_SIZE_H 1
#define CURSOR_SIZE_V 2
#define CURSOR_SIZE_D1 3
#define CURSOR_SIZE_D2 4

#define UI_C_DESKTOP 0
#define UI_C_PANEL_DARK 1
#define UI_C_PANEL 2
#define UI_C_PANEL_LIGHT 3
#define UI_C_PANEL_ACTIVE 4
#define UI_C_PANEL_HOVER 5
#define UI_C_PANEL_DEEP 4
#define UI_C_BORDER 7
#define UI_C_SHADOW 0
#define UI_C_TEXT 15
#define UI_C_TEXT_DIM 13
#define UI_C_TEXT_MUTED 14
#define UI_C_SCROLL_TRACK 7
#define UI_C_SCROLL_THUMB 9
#define UI_C_SELECTION 8

void vga_init();
void vga_update();
void vga_update_rect(int x, int y, int w, int h);
void vga_present_cursor(int x, int y, int mode, int clip_h);
uint8_t *vga_get_vbuf(void);
void putpixel(int x, int y, int color);
void draw_char(int x, int y, char c, int color);
void draw_char_scaled(int x, int y, char c, int color, int scale);
void draw_char_scaled_clipped(int x, int y, char c, int color, int scale, int clip_x0, int clip_y0, int clip_x1, int clip_y1);
void draw_text(int x, int y, const char *s, int color);
void draw_text_scaled(int x, int y, const char *s, int color, int scale);
void draw_text_scaled_clipped(int x, int y, const char *s, int color, int scale, int clip_x0, int clip_y0, int clip_x1, int clip_y1);
void draw_cursor(int x, int y, int color, int mode);
void draw_rect_fill(int x, int y, int w, int h, int color);
void draw_hline(int x, int y, int w, int color);
void draw_vline(int x, int y, int h, int color);
void draw_vertical_gradient(int x, int y, int w, int h, int top_color, int bottom_color);
void draw_bevel_rect(int x, int y, int w, int h, int fill, int light, int dark);
void draw_round_rect_fill(int x, int y, int w, int h, int r, int color);
void draw_round_rect_wire(int x, int y, int w, int h, int r, int color);

#endif
