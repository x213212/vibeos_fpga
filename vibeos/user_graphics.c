#include "user_graphics.h"
#include "user_internal.h"

int palette_index_from_rgb(unsigned char r, unsigned char g, unsigned char b) {
    int ri = (r * 5) / 255;
    int gi = (g * 5) / 255;
    int bi = (b * 5) / 255;
    return 16 + (ri * 36) + (gi * 6) + bi;
}

void quantize_rgb_to_palette(int r, int g, int b, int *idx, int *qr, int *qg, int *qb) {
    int ri = (r * 5) / 255;
    int gi = (g * 5) / 255;
    int bi = (b * 5) / 255;
    if (ri < 0) ri = 0; if (ri > 5) ri = 5;
    if (gi < 0) gi = 0; if (gi > 5) gi = 5;
    if (bi < 0) bi = 0; if (bi > 5) bi = 5;
    *idx = 16 + (ri * 36) + (gi * 6) + bi;
    *qr = (ri * 255) / 5;
    *qg = (gi * 255) / 5;
    *qb = (bi * 255) / 5;
}

uint16_t rgb565_from_rgb(unsigned char r, unsigned char g, unsigned char b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void rgb_from_rgb565(uint16_t c, int *r, int *g, int *b) {
    *r = ((c >> 11) & 0x1F) * 255 / 31;
    *g = ((c >> 5) & 0x3F) * 255 / 63;
    *b = (c & 0x1F) * 255 / 31;
}













































void draw_line_clipped(int x0, int y0, int x1, int y1, int color,
                       int clip_x0, int clip_y0, int clip_x1, int clip_y1) {
    int dx = x1 - x0;
    int sx = dx >= 0 ? 1 : -1;
    if (dx < 0) dx = -dx;
    int dy = y1 - y0;
    int sy = dy >= 0 ? 1 : -1;
    if (dy < 0) dy = -dy;
    int err = (dx > dy ? dx : -dy) / 2;
    for (;;) {
        if (x0 >= clip_x0 && x0 < clip_x1 && y0 >= clip_y0 && y0 < clip_y1) {
            putpixel(x0, y0, color);
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = err;
        if (e2 > -dx) { err -= dy; x0 += sx; }
        if (e2 < dy) { err += dx; y0 += sy; }
    }
}

static int edge_fn(int ax, int ay, int bx, int by, int px, int py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

void draw_triangle_filled_clipped(int x0, int y0, int x1, int y1, int x2, int y2, int color,
                                  int clip_x0, int clip_y0, int clip_x1, int clip_y1) {
    int minx = x0, maxx = x0, miny = y0, maxy = y0;
    if (x1 < minx) minx = x1; if (x2 < minx) minx = x2;
    if (x1 > maxx) maxx = x1; if (x2 > maxx) maxx = x2;
    if (y1 < miny) miny = y1; if (y2 < miny) miny = y2;
    if (y1 > maxy) maxy = y1; if (y2 > maxy) maxy = y2;
    if (minx < clip_x0) minx = clip_x0;
    if (miny < clip_y0) miny = clip_y0;
    if (maxx >= clip_x1) maxx = clip_x1 - 1;
    if (maxy >= clip_y1) maxy = clip_y1 - 1;
    int area = edge_fn(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;
    int sign = (area > 0) ? 1 : -1;
    for (int py = miny; py <= maxy; py++) {
        for (int px = minx; px <= maxx; px++) {
            int w0 = sign * edge_fn(x1, y1, x2, y2, px, py);
            int w1 = sign * edge_fn(x2, y2, x0, y0, px, py);
            int w2 = sign * edge_fn(x0, y0, x1, y1, px, py);
            if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
                putpixel(px, py, color);
            }
        }
    }
}

int sin_deg(int deg) {
    static const unsigned short sin_q[91] = {
        0,4,9,13,18,22,27,31,36,40,44,49,53,58,62,66,71,75,79,83,88,92,96,100,104,108,112,116,120,124,
        128,132,135,139,143,147,150,154,157,161,164,167,171,174,177,181,184,187,190,193,195,198,201,204,
        206,209,211,214,216,218,221,223,225,227,229,231,232,234,236,237,239,240,242,243,244,245,246,247,
        248,249,250,251,251,252,253,253,254,254,254,255,255,255,255,255,256
    };
    while (deg < 0) deg += 360;
    deg %= 360;
    if (deg <= 90) return sin_q[deg];
    if (deg <= 180) return sin_q[180 - deg];
    if (deg <= 270) return -((int)sin_q[deg - 180]);
    return -((int)sin_q[360 - deg]);
}

int cos_deg(int deg) {
    return sin_deg(deg + 90);
}


int open_image_file(struct Window *term, const char *name) {
    uint32_t size = 0;
    if (load_file_bytes(term, name, file_io_buf, sizeof(file_io_buf), &size) != 0) return -1;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (wins[i].active) continue;
        reset_window(&wins[i], i);
        wins[i].active = 1;
        wins[i].maximized = 0;
        wins[i].minimized = 0;
        wins[i].x = 140 + (i * 28);
        wins[i].y = 90 + (i * 22);
        int img_w = 0, img_h = 0;
        if (decode_image_to_rgb565(file_io_buf, size, wins[i].image, IMG_MAX_W, IMG_MAX_H, &img_w, &img_h) != 0) {
            reset_window(&wins[i], i);
            return -2;
        }
        wins[i].kind = WINDOW_KIND_IMAGE;
        wins[i].image_w = img_w;
        wins[i].image_h = img_h;
        wins[i].image_scale = 1;
        wins[i].w = img_w + 20;
        if (wins[i].w < 220) wins[i].w = 220;
        wins[i].h = img_h + 46;
        if (wins[i].h < 140) wins[i].h = 140;
        lib_strcpy(wins[i].title, "Image: ");
        lib_strncat(wins[i].title, name, 15);
        wins[i].x = (WIDTH - wins[i].w) / 2;
        if (wins[i].x < 0) wins[i].x = 0;
        wins[i].y = (DESKTOP_H - wins[i].h) / 2;
        if (wins[i].y < 0) wins[i].y = 0;
        bring_to_front(i);
        return 0;
    }
    return -3;
}
