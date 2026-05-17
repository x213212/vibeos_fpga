#ifndef GBEMU_APP_H
#define GBEMU_APP_H

#include "user.h"

int open_gbemu_window(struct Window *term, const char *rom_path);
void draw_gbemu_content(struct Window *w, int x, int y, int ww, int wh);
void handle_gbemu_input(struct Window *w, char *gui_key);
int gbemu_has_active_window(void);
int gbemu_tick_active(int budget);
void gbemu_sync_gamepad(void);
void gbemu_audio_pump(void);
void gbemu_audio_render_pcm_s16_stereo(int16_t *out, int frames);
void gbemu_audio_enable_diag_beep(int frames);

#endif
