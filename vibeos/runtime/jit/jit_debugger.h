#ifndef JIT_DEBUGGER_H
#define JIT_DEBUGGER_H

#include "user.h"

void draw_jit_debugger_content(struct Window *w, int x, int y, int ww, int wh);
int open_jit_debugger_window(struct Window *term, const char *path);
int jit_debugger_exec_cmd(struct Window *term, char *arg, char *out, int out_max);
void jit_debugger_handle_key(struct Window *w, char key);
int jit_debugger_handle_mouse_down(struct Window *w, int mx, int my, int x, int y, int ww, int wh);
void jit_debugger_handle_mouse_drag(struct Window *w, int mx, int my, int x, int y, int ww, int wh);
int jit_debugger_handle_wheel(struct Window *w, int mx, int my, int x, int y, int ww, int wh, int wheel, int horizontal);

#endif
