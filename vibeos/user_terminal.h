#ifndef USER_TERMINAL_H
#define USER_TERMINAL_H

#include "user.h"

void terminal_worker_task(void);
void redraw_prompt_line(struct Window *w, int row);
void terminal_append_prompt(struct Window *w);
void clear_window_input_queue(struct Window *w);
void clear_prompt_input(struct Window *w);
void load_edit_buffer(struct Window *w, const char *src);
void seed_terminal_history(struct Window *w);
void run_command(struct Window *w);
void handle_window_mailbox(struct Window *w);
void terminal_env_bootstrap(struct Window *w);
void terminal_env_reset(struct Window *w);
int terminal_env_set(struct Window *w, const char *name, const char *value);
int terminal_env_unset(struct Window *w, const char *name);
const char *terminal_env_get(struct Window *w, const char *name);
void terminal_env_sync_pwd(struct Window *w);
void terminal_env_expand_command(struct Window *w, const char *src, char *dst, int dst_size);
void terminal_expand_home_path(struct Window *w, const char *src, char *dst, int dst_size);
int terminal_alias_set(struct Window *w, const char *name, const char *value);
int terminal_alias_unset(struct Window *w, const char *name);
const char *terminal_alias_get(struct Window *w, const char *name);
void terminal_source_script(struct Window *w, const char *path);
void terminal_load_bashrc(struct Window *w);
void terminal_load_sshboot(struct Window *w);
int sshboot_password_obfuscate(const char *password, char *out, int out_max);
void set_shell_status(struct Window *w, const char *msg);
void terminal_finish_ctrl_c_cancel(struct Window *w, int killed);
void terminal_app_stdout_flush(struct Window *w);
void terminal_app_stdout_putc(int win_id, char ch);
void terminal_app_stdout_puts(int win_id, const char *s);
void terminal_app_stdout_flush_win(int win_id);
int terminal_visible_rows(struct Window *w);
int terminal_visible_cols(struct Window *w);
int terminal_font_scale(struct Window *w);
int terminal_char_w(struct Window *w);
int terminal_char_h(struct Window *w);
int terminal_line_h(struct Window *w);
void terminal_clamp_v_offset(struct Window *w);
int terminal_scroll_max(struct Window *w);
int terminal_is_at_bottom(struct Window *w);
void terminal_scroll_to_bottom(struct Window *w);
int line_text_len(const char *s);
void terminal_selection_normalized(struct Window *w, int *sr, int *sc, int *er, int *ec);
int terminal_has_nonempty_selection(struct Window *w);
void resize_terminal_font(struct Window *w, int new_scale);
const char *terminal_clipboard_text(void);
void terminal_clear_selection(struct Window *w);
void terminal_paste_clipboard(struct Window *w);
void terminal_copy_selection(struct Window *w);
int terminal_mouse_to_cell(struct Window *w, int mx, int my, int *row, int *col);
int window_input_empty(struct Window *w);
void window_input_push(struct Window *w, char key);
int window_input_pop(struct Window *w, char *key);
void wake_terminal_worker_for_window(int win_idx);
void broadcast_ctrl_c(void);

#endif
