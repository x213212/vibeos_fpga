#include "user.h"
#include "user_graphics.h"

static int parse_i32(const char *s, int *used, int *out) {
    int i = 0, sign = 1, v = 0;
    if (s[i] == '-') { sign = -1; i++; }
    else if (s[i] == '+') { i++; }
    if (s[i] < '0' || s[i] > '9') return -1;
    while (s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (s[i] - '0');
        i++;
    }
    *used = i;
    *out = v * sign;
    return 0;
}

int parse_demo3d_points(char *spec, int pts[8][3], int *count) {
    int n = 0;
    while (*spec == ' ') spec++;
    while (*spec) {
        int used = 0;
        if (n >= 8) return -2;
        if (parse_i32(spec, &used, &pts[n][0]) != 0) return -1;
        spec += used;
        if (*spec != ',') return -1;
        spec++;
        if (parse_i32(spec, &used, &pts[n][1]) != 0) return -1;
        spec += used;
        if (*spec != ',') return -1;
        spec++;
        if (parse_i32(spec, &used, &pts[n][2]) != 0) return -1;
        spec += used;
        n++;
        while (*spec == ' ') spec++;
    }
    if (n < 3) return -1;
    *count = n;
    return 0;
}

int open_demo3d_window_points(int pts[8][3], int count) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (wins[i].active) continue;
        reset_window(&wins[i], i);
        wins[i].active = 1;
        wins[i].maximized = 0;
        wins[i].minimized = 0;
        wins[i].kind = WINDOW_KIND_DEMO3D;
        wins[i].w = 360;
        wins[i].h = 280;
        wins[i].x = (WIDTH - wins[i].w) / 2;
        wins[i].y = (DESKTOP_H - wins[i].h) / 2;
        wins[i].demo_angle = 0;
        wins[i].demo_pitch = 0;
        wins[i].demo_auto_spin = 1;
        wins[i].demo_dist = 220;
        wins[i].demo_shape = DEMO3D_SHAPE_POLY;
        wins[i].demo_point_count = count;
        for (int p = 0; p < count; p++) {
            wins[i].demo_points[p][0] = pts[p][0];
            wins[i].demo_points[p][1] = pts[p][1];
            wins[i].demo_points[p][2] = pts[p][2];
        }
        lib_strcpy(wins[i].title, "3D Demo");
        bring_to_front(i);
        return 0;
    }
    return -1;
}

int open_demo3d_cube_window(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (wins[i].active) continue;
        reset_window(&wins[i], i);
        wins[i].active = 1;
        wins[i].maximized = 0;
        wins[i].minimized = 0;
        wins[i].kind = WINDOW_KIND_DEMO3D;
        wins[i].w = 360;
        wins[i].h = 280;
        wins[i].x = (WIDTH - wins[i].w) / 2;
        wins[i].y = (DESKTOP_H - wins[i].h) / 2;
        wins[i].demo_angle = 0;
        wins[i].demo_pitch = 0;
        wins[i].demo_auto_spin = 1;
        wins[i].demo_dist = 240;
        wins[i].demo_shape = DEMO3D_SHAPE_CUBE;
        wins[i].demo_point_count = 0;
        lib_strcpy(wins[i].title, "3D Demo");
        bring_to_front(i);
        return 0;
    }
    return -1;
}

int open_demo3d_window(void) {
    return open_demo3d_cube_window();
}

static uint8_t fps_map[16][16] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,1},
    {1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
    {1,1,0,1,1,1,0,0,0,0,0,1,0,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,2,0,0,0,0,0,0,0,2,0,0,1},
    {1,1,1,0,1,1,1,0,0,1,1,1,0,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,2,0,0,0,0,0,0,0,0,0,0,2,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

int open_frankenstein_window(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (wins[i].active) continue;
        reset_window(&wins[i], i);
        wins[i].active = 1;
        wins[i].maximized = 1;
        wins[i].minimized = 0;
        wins[i].kind = WINDOW_KIND_FPS_GAME;
        wins[i].w = 320;
        wins[i].h = 240;
        wins[i].x = 0;
        wins[i].y = 0;
        wins[i].fps_x = 2 * 256 + 128;
        wins[i].fps_y = 2 * 256 + 128;
        wins[i].fps_dir = 45;
        lib_strcpy(wins[i].title, "Frankenstein");
        bring_to_front(i);
        return 0;
    }
    return -1;
}

void draw_fps_content(struct Window *w, int wx, int wy, int ww, int wh) {
    /* Use full width edge-to-edge below the title bar when maximized */
    int clip_x0, clip_y0, clip_w, clip_h;
    if (w->maximized) {
        clip_x0 = wx;
        clip_y0 = wy + 28;
        clip_w  = ww;
        clip_h  = wh - 28;
    } else {
        clip_x0 = wx + 10;
        clip_y0 = wy + 34;
        clip_w  = ww - 20;
        clip_h  = wh - 44;
    }
    if (clip_w <= 0 || clip_h <= 0) return;
    int clip_x1 = clip_x0 + clip_w;
    int clip_y1 = clip_y0 + clip_h;

    /* Ceiling (dark navy) and floor (black) */
    draw_rect_fill(clip_x0, clip_y0, clip_w, clip_h / 2, 1);
    draw_rect_fill(clip_x0, clip_y0 + clip_h / 2, clip_w, clip_h - clip_h / 2, 0);

    /* Player state (sub-tile units: 256 = 1 tile) */
    int fps_x = w->fps_x;
    int fps_y = w->fps_y;

    /* Forward direction vector (magnitude ~256) */
    int cx = cos_deg(w->fps_dir);
    int sy = sin_deg(w->fps_dir);

    /* Camera plane perpendicular to forward, 60 deg FOV: magnitude = tan(30)*256 ≈ 148
     * In Y-down coords, left-perp of (cx, sy) is (-sy, cx)                          */
    int plx = (-sy * 148) / 256;
    int ply = ( cx * 148) / 256;

    for (int col = 0; col < clip_w; col++) {
        /* Camera X: -256 (left edge) … +256 (right edge) */
        int cam_x = (2 * col * 256) / clip_w - 256;

        /* Ray direction (256-unit fixed-point scale) */
        int rdx = cx + (plx * cam_x) / 256;
        int rdy = sy + (ply * cam_x) / 256;
        if (rdx == 0) rdx = 1;
        if (rdy == 0) rdy = 1;
        int rdxa = rdx < 0 ? -rdx : rdx;
        int rdya = rdy < 0 ? -rdy : rdy;

        /* Current tile and fractional position within it [0..255] */
        int mx  = fps_x / 256,  my  = fps_y / 256;
        int sbx = fps_x & 255,  sby = fps_y & 255;

        /* DDA deltas: ray-t units (×256) to cross one full tile in X or Y */
        int ddx = rdxa > 0 ? (256 * 256 / rdxa) : 0x7FFF;
        int ddy = rdya > 0 ? (256 * 256 / rdya) : 0x7FFF;

        int stepx = rdx > 0 ? 1 : -1;
        int stepy = rdy > 0 ? 1 : -1;

        /* Initial side distances to first X and Y grid crossing */
        int sdx = rdx > 0 ? (256 - sbx) * 256 / rdxa : sbx * 256 / rdxa;
        int sdy = rdy > 0 ? (256 - sby) * 256 / rdya : sby * 256 / rdya;

        /* DDA traversal — step exactly along grid lines, never skips walls */
        int side = 0, wtype = 0, hit = 0;
        for (int i = 0; i < 64 && !hit; i++) {
            if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; }
            else           { sdy += ddy; my += stepy; side = 1; }
            if (mx < 0 || mx >= 16 || my < 0 || my >= 16) break;
            if (fps_map[my][mx]) { hit = 1; wtype = fps_map[my][mx]; }
        }

        if (hit) {
            /* Perpendicular distance (fish-eye corrected) */
            int perp = (side == 0) ? (sdx - ddx) : (sdy - ddy);
            if (perp < 1) perp = 1;

            int line_h = clip_h * 256 / perp;
            if (line_h > clip_h) line_h = clip_h;

            int ys = clip_y0 + (clip_h - line_h) / 2;
            int ye = ys + line_h;

            /* EW-face (side=0) is "lit", NS-face (side=1) is "shadow" → clear corners */
            int color;
            if (wtype == 2) color = (side == 0) ? 14 : 6;  /* yellow column */
            else            color = (side == 0) ?  7 : 8;  /* stone wall */

            draw_line_clipped(clip_x0 + col, ys, clip_x0 + col, ye,
                              color, clip_x0, clip_y0, clip_x1, clip_y1);
        }
    }
}

void draw_demo3d_content(struct Window *w, int x, int y, int ww, int wh) {
    int clip_x0 = x + 10;
    int clip_y0 = y + 34;
    int clip_x1 = x + ww - 10;
    int clip_y1 = y + wh - 10;
    int cx = (clip_x0 + clip_x1) / 2;
    int cy = (clip_y0 + clip_y1) / 2;
    int proj[8][2];
    draw_rect_fill(clip_x0, clip_y0, clip_x1 - clip_x0, clip_y1 - clip_y0, 0);
    if (w->demo_shape == DEMO3D_SHAPE_CUBE) {
        static const int cube[8][3] = {
            {-48,-48,-48}, { 48,-48,-48}, { 48, 48,-48}, {-48, 48,-48},
            {-48,-48, 48}, { 48,-48, 48}, { 48, 48, 48}, {-48, 48, 48}
        };
        static const unsigned char faces[6][4] = {
            {0,1,2,3}, {4,5,6,7}, {0,1,5,4},
            {2,3,7,6}, {1,2,6,5}, {0,3,7,4}
        };
        static const int face_colors[6] = {9, 10, 11, 12, 13, 14};
        int tv[8][3];
        int order[6];
        int depth[6];
        int ang_y = w->demo_angle;
        int ang_x = w->demo_pitch;
        int cyaw = cos_deg(ang_y), syaw = sin_deg(ang_y);
        int cpit = cos_deg(ang_x), spit = sin_deg(ang_x);
        for (int i = 0; i < 8; i++) {
            int x0 = cube[i][0], y0 = cube[i][1], z0 = cube[i][2];
            int x1 = (x0 * cyaw + z0 * syaw) / 256;
            int z1 = (-x0 * syaw + z0 * cyaw) / 256;
            int y1 = y0;
            int y2 = (y1 * cpit - z1 * spit) / 256;
            int z2 = (y1 * spit + z1 * cpit) / 256;
            int dist = w->demo_dist;
            int z = z2 + dist;
            if (z < 32) z = 32;
            tv[i][0] = x1;
            tv[i][1] = y2;
            tv[i][2] = z2;
            proj[i][0] = cx + (x1 * 160) / z;
            proj[i][1] = cy + (y2 * 160) / z;
        }
        for (int f = 0; f < 6; f++) {
            depth[f] = tv[faces[f][0]][2] + tv[faces[f][1]][2] + tv[faces[f][2]][2] + tv[faces[f][3]][2];
            order[f] = f;
        }
        for (int a = 0; a < 5; a++) {
            for (int b = a + 1; b < 6; b++) {
                if (depth[order[a]] < depth[order[b]]) {
                    int t = order[a];
                    order[a] = order[b];
                    order[b] = t;
                }
            }
        }
        for (int oi = 0; oi < 6; oi++) {
            int f = order[oi];
            int a = faces[f][0], b = faces[f][1], c = faces[f][2], d = faces[f][3];
            draw_triangle_filled_clipped(proj[a][0], proj[a][1], proj[b][0], proj[b][1], proj[c][0], proj[c][1],
                                         face_colors[f], clip_x0, clip_y0, clip_x1, clip_y1);
            draw_triangle_filled_clipped(proj[a][0], proj[a][1], proj[c][0], proj[c][1], proj[d][0], proj[d][1],
                                         face_colors[f], clip_x0, clip_y0, clip_x1, clip_y1);
        }
        static const unsigned char edges[12][2] = {
            {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},
            {6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
        };
        for (int e = 0; e < 12; e++) {
            int a = edges[e][0], b = edges[e][1];
            draw_line_clipped(proj[a][0], proj[a][1], proj[b][0], proj[b][1], 15,
                              clip_x0, clip_y0, clip_x1, clip_y1);
        }
    } else {
        int count = w->demo_point_count;
        if (count < 3) count = 0;
        int c_y = cos_deg(w->demo_angle);
        int s_y = sin_deg(w->demo_angle);
        int c_p = cos_deg(w->demo_pitch);
        int s_p = sin_deg(w->demo_pitch);
        for (int i = 0; i < count; i++) {
            int px = w->demo_points[i][0];
            int py = w->demo_points[i][1];
            int pz = w->demo_points[i][2];
            int rx = (px * c_y + pz * s_y) / 256;
            int z1 = (-px * s_y + pz * c_y) / 256;
            int ry = (py * c_p - z1 * s_p) / 256;
            int rz = (py * s_p + z1 * c_p) / 256;
            int dist = w->demo_dist;
            int z = rz + dist;
            if (z < 32) z = 32;
            proj[i][0] = cx + (rx * 140) / z;
            proj[i][1] = cy + (ry * 140) / z;
        }
        draw_triangle_filled_clipped(proj[0][0], proj[0][1], proj[1][0], proj[1][1], proj[2][0], proj[2][1],
                                     12, clip_x0, clip_y0, clip_x1, clip_y1);
        for (int i = 2; i + 1 < count; i++) {
            draw_triangle_filled_clipped(proj[0][0], proj[0][1], proj[i][0], proj[i][1], proj[i + 1][0], proj[i + 1][1],
                                         12, clip_x0, clip_y0, clip_x1, clip_y1);
        }
        for (int i = 0; i < count; i++) {
            int j = (i + 1) % count;
            draw_line_clipped(proj[i][0], proj[i][1], proj[j][0], proj[j][1], 15,
                              clip_x0, clip_y0, clip_x1, clip_y1);
        }
    }
}

void handle_demo3d_input(struct Window *aw, uint32_t *gui_key) {
    if (*gui_key == ' ') { 
        aw->demo_auto_spin ^= 1;
        *gui_key = 0;
    } else if (*gui_key == 0x10) { 
        aw->demo_pitch = (aw->demo_pitch + 5) % 360;
        *gui_key = 0;
    } else if (*gui_key == 0x11) { 
        aw->demo_pitch = (aw->demo_pitch + 355) % 360;
        *gui_key = 0;
    } else if (*gui_key == 0x12) { 
        aw->demo_angle = (aw->demo_angle + 5) % 360;
        *gui_key = 0;
    } else if (*gui_key == 0x13) { 
        aw->demo_angle = (aw->demo_angle + 355) % 360;
        *gui_key = 0;
    }
}

void handle_fps_input(struct Window *aw, uint32_t *gui_key, int *redraw_needed) {
    int c_dir = cos_deg(aw->fps_dir);
    int s_dir = sin_deg(aw->fps_dir);
    int speed = 128;
    /* Collision margin in sub-tile units (256 = 1 full tile) */
    int margin = 40;

    int dx = 0, dy = 0;
    if (*gui_key == 0x10) {        /* forward */
        dx = (c_dir * speed) / 256;
        dy = (s_dir * speed) / 256;
        *gui_key = 0; *redraw_needed = 1;
    } else if (*gui_key == 0x11) { /* backward */
        dx = -(c_dir * speed) / 256;
        dy = -(s_dir * speed) / 256;
        *gui_key = 0; *redraw_needed = 1;
    } else if (*gui_key == 0x12) { /* turn left */
        aw->fps_dir = (aw->fps_dir - 5 + 360) % 360;
        *gui_key = 0; *redraw_needed = 1;
        return;
    } else if (*gui_key == 0x13) { /* turn right */
        aw->fps_dir = (aw->fps_dir + 5) % 360;
        *gui_key = 0; *redraw_needed = 1;
        return;
    }

    if (dx == 0 && dy == 0) return;

    /* Check X axis independently */
    {
        int nx = aw->fps_x + dx;
        int probe_x = (dx > 0) ? (nx + margin) : (nx - margin);
        int probe_y_lo = aw->fps_y - margin + 1;
        int probe_y_hi = aw->fps_y + margin - 1;
        int mx0 = probe_x / 256, my0 = probe_y_lo / 256;
        int mx1 = probe_x / 256, my1 = probe_y_hi / 256;
        int blocked = 0;
        if (mx0 < 0 || mx0 >= 16 || my0 < 0 || my0 >= 16) blocked = 1;
        else if (fps_map[my0][mx0] != 0) blocked = 1;
        if (!blocked) {
            if (mx1 < 0 || mx1 >= 16 || my1 < 0 || my1 >= 16) blocked = 1;
            else if (fps_map[my1][mx1] != 0) blocked = 1;
        }
        if (!blocked) aw->fps_x = nx;
    }

    /* Check Y axis independently */
    {
        int ny = aw->fps_y + dy;
        int probe_x_lo = aw->fps_x - margin + 1;
        int probe_x_hi = aw->fps_x + margin - 1;
        int probe_y = (dy > 0) ? (ny + margin) : (ny - margin);
        int mx0 = probe_x_lo / 256, my0 = probe_y / 256;
        int mx1 = probe_x_hi / 256, my1 = probe_y / 256;
        int blocked = 0;
        if (mx0 < 0 || mx0 >= 16 || my0 < 0 || my0 >= 16) blocked = 1;
        else if (fps_map[my0][mx0] != 0) blocked = 1;
        if (!blocked) {
            if (mx1 < 0 || mx1 >= 16 || my1 < 0 || my1 >= 16) blocked = 1;
            else if (fps_map[my1][mx1] != 0) blocked = 1;
        }
        if (!blocked) aw->fps_y = ny;
    }
}
