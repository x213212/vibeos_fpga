#ifndef USER_GUI_H
#define USER_GUI_H

#include "user.h"

extern int gui_task_id;
extern int active_win_idx;
extern int z_order[MAX_WINDOWS];
extern volatile int gui_redraw_needed;

void gui_task(void);
void request_gui_redraw(void);
void close_window(int idx);
void bring_to_front(int idx);
void cycle_active_window(void);
void create_new_task(void);
void draw_window(struct Window *w);
void draw_taskbar(void);
void resize_image_window(struct Window *w, int new_scale);
int clamp_int(int v, int lo, int hi);
int taskbar_button_x(int idx);
void set_window_title(struct Window *w, int idx);
int active_window_valid(void);
int cursor_mode_for_resize_dir(int dir);
int hit_resize_zone(struct Window *w, int mx, int my);
void begin_resize(struct Window *w, int dir, int mx, int my);
void apply_resize(struct Window *w, int mx, int my);

#endif
