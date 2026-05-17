#ifndef NETSURF_APP_H
#define NETSURF_APP_H

#include "user.h"

void netsurf_init_engine(struct Window *w);
void netsurf_render_frame(struct Window *w, int x, int y, int ww, int wh);
void netsurf_handle_input(struct Window *w, int mx, int my, int clicked);
void netsurf_begin_navigation(int win_id);
void netsurf_invalidate_layout(int win_id);
int netsurf_refresh_current_view(int win_id);
void netsurf_receive_image(int win_id, const char *url, const uint8_t *data, size_t len);
void netsurf_feed_data(int win_id, const uint8_t *data, size_t len);
void netsurf_complete_data(int win_id);
void netsurf_release_window(int win_id);
int open_netsurf_window(void);

void netsurf_normalize_target_url(const char *src, char *dst, int dst_max);
int netsurf_prepare_launch_url(const char *url, int viewport_w, int viewport_h, char *out, int out_max);
int netsurf_queue_wrapped_request(const char *url, int viewport_w, int viewport_h, int owner_win_id);

#endif
