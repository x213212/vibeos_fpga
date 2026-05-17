#ifndef USER_GRAPHICS_H
#define USER_GRAPHICS_H

#include "user.h"

int open_image_file(struct Window *term, const char *name);
int palette_index_from_rgb(unsigned char r, unsigned char g, unsigned char b);
void quantize_rgb_to_palette(int r, int g, int b, int *idx, int *qr, int *qg, int *qb);
uint16_t rgb565_from_rgb(unsigned char r, unsigned char g, unsigned char b);
void rgb_from_rgb565(uint16_t c, int *r, int *g, int *b);
void draw_line_clipped(int x0, int y0, int x1, int y1, int color,
                       int clip_x0, int clip_y0, int clip_x1, int clip_y1);
void draw_triangle_filled_clipped(int x0, int y0, int x1, int y1, int x2, int y2, int color,
                                  int clip_x0, int clip_y0, int clip_x1, int clip_y1);
int sin_deg(int deg);
int cos_deg(int deg);

#endif
