#ifndef USER_EDITOR_H
#define USER_EDITOR_H

#include "user.h"

void editor_set_status(struct Window *w, const char *msg);
void editor_clear(struct Window *w);
void editor_load_bytes(struct Window *w, const unsigned char *src, uint32_t size);
int editor_load_file_window(struct Window *w, uint32_t start_line);
void editor_handle_key(struct Window *w, char key);
void editor_render(struct Window *w, int x, int y, int ww, int wh);
int open_text_editor(struct Window *term, const char *name);

#endif
