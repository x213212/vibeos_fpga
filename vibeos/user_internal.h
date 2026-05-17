#ifndef USER_INTERNAL_H
#define USER_INTERNAL_H
#include "user.h"
#include "user_wget.h"
#include "vga.h"
#include "os.h"
#include "lib.h"
#include "string.h"
#include "hubbub/hubbub.h"
#include "hubbub/types.h"
#include "ac97_audio.h"
#include "gbemu_app.h"
#include "netsurf_app.h"
#include "ssh_client.h"
#include "tcc_runtime.h"
#include "user_cmd.h"
#include "user_editor.h"
#include "user_fs_shell.h"
#include "user_graphics.h"
#include "user_gui.h"
#include "user_terminal.h"
#define TERMINAL_WORKERS 3
#define PROMPT "matrix:~$ > "
#define PROMPT_LEN 12
#define TASKBAR_ID 255
#define RESIZE_NONE 0
#define RESIZE_LEFT 1
#define RESIZE_RIGHT 2
#define RESIZE_TOP 4
#define RESIZE_BOTTOM 8
#define RESIZE_MARGIN 10
#define MIN_WIN_W 220
#define MIN_WIN_H 140
#ifdef FPGA_MINIMAL
#define GUI_VISIBLE_H 300
#else
#define GUI_VISIBLE_H HEIGHT
#endif
#define TASKBAR_H 30
#define DESKTOP_H (GUI_VISIBLE_H - TASKBAR_H)
#define TASKBAR_START_X 110
#define TASKBAR_BTN_W 64
#define TASKBAR_BTN_GAP 4
#define TERM_FONT_SCALE_MIN 1
#define TERM_FONT_SCALE_MAX 3
#define COL_DESKTOP UI_C_DESKTOP
#define COL_WIN_BG UI_C_PANEL_DARK
#define COL_WIN_BRD UI_C_BORDER
#define COL_TITLE_ACT UI_C_PANEL_ACTIVE
#define COL_TITLE_INACT UI_C_PANEL
#define COL_TEXT UI_C_TEXT
#define COL_DIR UI_C_TEXT_DIM
#define UI_RADIUS 10
#define WINDOW_KIND_NETSURF 5
#define WINDOW_KIND_GBEMU 6
extern struct Window wins[MAX_WINDOWS];
extern int active_win_idx, z_order[MAX_WINDOWS], app_owner_win_id, gui_cursor_mode, app_running, app_task_id;
extern uint8_t sheet_map[WIDTH * HEIGHT];
extern char terminal_clipboard[2048];
extern int terminal_worker_task_ids[TERMINAL_WORKERS];
extern unsigned char file_io_buf[WGET_MAX_FILE_SIZE];
extern uint32_t demo3d_fps_value, demo3d_fps_last_ms, demo3d_fps_frames, demo3d_last_frame_ms;
extern int dither_err_r0[WIDTH + 2], dither_err_g0[WIDTH + 2], dither_err_b0[WIDTH + 2];
extern int dither_err_r1[WIDTH + 2], dither_err_g1[WIDTH + 2], dither_err_b1[WIDTH + 2];
extern volatile int need_resched;
extern int gui_mx, gui_my, gui_clicked, gui_click_pending, gui_right_clicked, gui_right_click_pending, gui_wheel, gui_shortcut_new_task, gui_shortcut_close_task, gui_shortcut_switch_task, gui_ctrl_pressed;
extern char gui_key;
extern volatile int gui_redraw_needed;
int gui_key_pull_next(void);
int gui_key_queued(void);
extern uint32_t APP_START, APP_END, APP_SIZE;
void redraw_prompt_line(struct Window *w, int row);
void close_window(int idx);
void bring_to_front(int idx);
void cycle_active_window(void);
void create_new_task(void);
void draw_window(struct Window *w);
void draw_taskbar(void);
void gui_task(void);
void network_task(void);
void terminal_worker_task(void);
void user_init(void);
uint32_t current_fs_time(void);
int decode_image_to_rgb565(const unsigned char *buf, uint32_t size, uint16_t *dst, int dst_max_w, int dst_max_h, int *out_w, int *out_h);
void load_edit_buffer(struct Window *w, const char *src);
void clear_prompt_input(struct Window *w);
void seed_terminal_history(struct Window *w);
void terminal_load_bashrc(struct Window *w);
void terminal_load_sshboot(struct Window *w);
void terminal_source_script(struct Window *w, const char *path);
int sshboot_password_obfuscate(const char *password, char *out, int out_max);
void terminal_env_bootstrap(struct Window *w);
void terminal_env_reset(struct Window *w);
int terminal_env_set(struct Window *w, const char *name, const char *value);
int terminal_env_unset(struct Window *w, const char *name);
const char *terminal_env_get(struct Window *w, const char *name);
int terminal_alias_set(struct Window *w, const char *name, const char *value);
int terminal_alias_unset(struct Window *w, const char *name);
const char *terminal_alias_get(struct Window *w, const char *name);
void terminal_env_sync_pwd(struct Window *w);
void terminal_env_expand_command(struct Window *w, const char *src, char *dst, int dst_size);
void terminal_expand_home_path(struct Window *w, const char *src, char *dst, int dst_size);
void set_shell_status(struct Window *w, const char *msg);
void terminal_app_stdout_flush(struct Window *w);
void terminal_app_stdout_putc(int win_id, char ch);
void terminal_app_stdout_puts(int win_id, const char *s);
void terminal_app_stdout_flush_win(int win_id);
int terminal_visible_rows(struct Window *w);
void release_finished_app_commands(void);
void app_exit_trampoline(void);
void run_command(struct Window *w);
void handle_window_mailbox(struct Window *w);
int terminal_mouse_to_cell(struct Window *w, int mx, int my, int *row, int *col);
void terminal_clear_selection(struct Window *w);
void terminal_paste_clipboard(struct Window *w);
void terminal_copy_selection(struct Window *w);
void resize_image_window(struct Window *w, int new_scale);
int terminal_font_scale(struct Window *w);
int terminal_char_w(struct Window *w);
int terminal_char_h(struct Window *w);
int terminal_line_h(struct Window *w);
int window_input_empty(struct Window *w);
void window_input_push(struct Window *w, char key);
int window_input_pop(struct Window *w, char *key);
void wake_terminal_worker_for_window(int win_idx);
void wake_network_task(void);
void network_task_notify(void);
void broadcast_ctrl_c(void);
int terminal_visible_cols(struct Window *w);
void terminal_clamp_v_offset(struct Window *w);
int terminal_scroll_max(struct Window *w);
int terminal_is_at_bottom(struct Window *w);
void terminal_scroll_to_bottom(struct Window *w);
int line_text_len(const char *s);
void terminal_selection_normalized(struct Window *w, int *sr, int *sc, int *er, int *ec);
int terminal_has_nonempty_selection(struct Window *w);
void resize_terminal_font(struct Window *w, int new_scale);
const char *terminal_clipboard_text(void);
int clamp_int(int v, int lo, int hi);
void format_clock(char *out);
void format_hms(uint32_t total, char *out);
void format_size_human(uint32_t bytes, char *out);
uint16_t rgb565_from_rgb(unsigned char r, unsigned char g, unsigned char b);
void rgb_from_rgb565(uint16_t c, int *r, int *g, int *b);
int palette_index_from_rgb(unsigned char r, unsigned char g, unsigned char b);
void quantize_rgb_to_palette(int r, int g, int b, int *idx, int *qr, int *qg, int *qb);
void path_set_child(char *cwd, const char *name);
void path_set_parent(char *cwd);
void copy_name20(char *dst, const char *src);
void append_out_str(char *out, int out_max, const char *s);
void append_out_pad(char *out, int out_max, const char *s, int width);
void append_dir_entries_sorted(struct dir_block *db, const char *title, const char *name_prefix, int type_filter, char *out);
void list_dir_contents(uint32_t bno, char *out);
const char *path_basename(const char *p);
void copy_last_path_segment(char *dst, const char *path, const char *fallback);
int path_is_sftp(const char *path);
const char *sftp_subpath(const char *path);
int copy_between_paths(struct Window *w, const char *src, const char *dst, char *out, int out_max);
int local_path_info(struct Window *w, const char *path, int *type_out, uint32_t *bno_out, char *name_out);
int copy_local_dir_recursive(struct Window *w, uint32_t src_bno, const char *dst_path, char *out, int out_max);
int copy_sftp_dir_to_local_recursive(struct Window *w, const char *src_remote, const char *dst_path, char *out, int out_max);
int strncasecmp(const char *s1, const char *s2, uint32_t n);
void* memchr(const void *s, int c, uint32_t n);
char* strncpy(char *dest, const char *src, uint32_t n);
void* bsearch(const void *key, const void *base, uint32_t nmemb, uint32_t size, int (*compar)(const void *, const void *));
void append_hex32(char *dst, uint32_t v);
void append_hex8(char *dst, unsigned char v);
int is_mostly_text(const unsigned char *buf, uint32_t size);
void render_hex_dump(char *out, const unsigned char *buf, uint32_t size);
void terminal_append_prompt(struct Window *w);
void clear_window_input_queue(struct Window *w);
int str_starts_with(const char *s, const char *prefix);
void netsurf_init_engine(struct Window *w);
void netsurf_feed_data(int win_id, const uint8_t *data, size_t len);
void netsurf_complete_data(int win_id);
void netsurf_release_window(int win_id);
int taskbar_button_x(int idx);
void set_window_title(struct Window *w, int idx);
int active_window_valid(void);
int cursor_mode_for_resize_dir(int dir);
int hit_resize_zone(struct Window *w, int mx, int my);
void begin_resize(struct Window *w, int dir, int mx, int my);
void apply_resize(struct Window *w, int mx, int my);
#endif
