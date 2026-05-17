#ifndef USER_H
#define USER_H

#include <stdint.h>
#include "os.h"
#include "vga.h"
#include "virtio.h"

#ifdef FPGA_MINIMAL
#define MAX_WINDOWS 4
#define FS_MAX_BLOCKS 512u
#else
#define MAX_WINDOWS 12
#define FS_MAX_BLOCKS 65536u
#endif
#define ROWS 64 
#define COLS 100 
#define MAX_HIST 512
#define NETSURF_URL_MAX 512
#define OUT_BUF_SIZE 2048
#define INPUT_MAILBOX_SIZE 256
#define EDITOR_MAX_LINES 128
#define EDITOR_LINE_LEN 64
#define EDITOR_STATUS_LEN 64
#define IMG_MAX_W 320
#define IMG_MAX_H 240
#define APP_STACK_SIZE (16 * 1024)
#define APP_HEAP_GUARD (4096)
#define APP_MAX_ARGS 8
#define MAX_ENV_VARS 12
#define MAX_ALIAS_VARS 12
#define ENV_NAME_LEN 20
#define ENV_VALUE_LEN 96
#ifdef FPGA_MINIMAL
#define GUI_VISIBLE_H 300
#else
#define GUI_VISIBLE_H HEIGHT
#endif
#define TASKBAR_H 30
#define DESKTOP_H (GUI_VISIBLE_H - TASKBAR_H)
#define DEMO3D_SHAPE_POLY 0
#define DEMO3D_SHAPE_CUBE 1
#define ELF32_MAG0 0x7f
#define ELF32_MAG1 'E'
#define ELF32_MAG2 'L'
#define ELF32_MAG3 'F'
#define ELF32_CLASS_32 1
#define ELF32_DATA_LSB 1
#define ELF32_VERSION_CURRENT 1
#define ELF32_ET_EXEC 2
#define ELF32_EM_RISCV 243
#define ELF32_PT_LOAD 1

#define FS_MAGIC 0x58504653
#define FS_DATA_MAGIC 0x31415444
#define FS_DATA_PAYLOAD (BSIZE - 8)

#define WINDOW_KIND_TERMINAL 0
#define WINDOW_KIND_IMAGE 1
#define WINDOW_KIND_DEMO3D 2
#define WINDOW_KIND_FPS_GAME 3
#define WINDOW_KIND_EDITOR 4
#define WINDOW_KIND_NETSURF 5
#define WINDOW_KIND_GBEMU 6
#define WINDOW_KIND_JIT_DEBUGGER 7

#define MAX_DIR_ENTRIES 120
struct file_entry { char name[20]; uint32_t bno, size, ctime, mtime; uint16_t mode, type; };
struct dir_block { uint32_t magic, parent_bno; struct file_entry entries[MAX_DIR_ENTRIES]; };
struct data_block { uint32_t magic, next_bno; unsigned char data[FS_DATA_PAYLOAD]; };
struct elf32_ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));
struct elf32_phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed));
struct bmp_file_header {
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t off_bits;
} __attribute__((packed));
struct bmp_info_header {
    uint32_t size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bit_count;
    uint32_t compression;
    uint32_t size_image;
    int32_t x_pels_per_meter;
    int32_t y_pels_per_meter;
    uint32_t clr_used;
    uint32_t clr_important;
} __attribute__((packed));

struct Window {
    int id, active, maximized, minimized, kind;
    int x, y, w, h, prev_x, prev_y, prev_w, prev_h;
    int dragging, scroll_dragging, drag_off_x, drag_off_y;
    int resizing, resize_dir;
    int resize_start_mx, resize_start_my, resize_start_x, resize_start_y, resize_start_w, resize_start_h;
    char lines[ROWS][COLS]; int total_rows, cur_col, v_offset;
    char mailbox, title[64], cmd_buf[COLS], cwd[128];
    char input_q[INPUT_MAILBOX_SIZE];
    int input_head, input_tail;
    uint32_t cwd_bno; char history[MAX_HIST][COLS]; int hist_count, hist_idx;
    int edit_len, cursor_pos, has_saved_cmd;
    int executing_cmd, cancel_requested, waiting_wget;
    int submit_locked;
    char shell_status[64];
    int taskbar_anim;
    int selecting, has_selection;
    int sel_start_row, sel_start_col, sel_end_row, sel_end_col;
    char saved_cmd[COLS];
    char ssh_auth_buf[64];
    int ssh_auth_mode, ssh_auth_len;
    char editor_name[20], editor_cwd[128], editor_status[EDITOR_STATUS_LEN];
    char editor_path[128];
    char debug_entry_path[128];
    char debug_source_path[128];
    int debug_split_x, debug_split_y, debug_drag_split;
    int debug_regs_scroll_x, debug_regs_scroll_y;
    int debug_mem_scroll_x, debug_mem_scroll_y;
    int debug_source_scroll_x, debug_source_scroll_y;
    int debug_console_scroll_x, debug_console_scroll_y;
    char editor_lines[EDITOR_MAX_LINES][EDITOR_LINE_LEN];
    char editor_cmd[COLS];
    int editor_mode, editor_line_count, editor_cursor_row, editor_cursor_col;
    int editor_scroll_row, editor_cmd_len, editor_cmd_cursor, editor_dirty, editor_pending_d, editor_readonly;
    uint32_t editor_file_bno, editor_file_size, editor_loaded_start_line;
    int editor_lazy, editor_loaded_complete;
    int ns_h_offset;
    int ns_input_active;
    int ns_resize_pending;
    uint32_t ns_resize_last_ms;
    int ns_history_count;
    int ns_history_pos;
    char ns_url[NETSURF_URL_MAX];
    char ns_target_url[256];
    char ns_history[MAX_HIST][NETSURF_URL_MAX];
    uint8_t *netsurf_buffer;
    struct blk local_b;
    char app_stdout[OUT_BUF_SIZE];
    int app_stdout_len;
    char out_buf[OUT_BUF_SIZE];
    int image_w, image_h, image_scale, term_font_scale;
    int demo_angle;
    int demo_pitch;
    int demo_auto_spin;
    int demo_dragging;
    int demo_drag_last_mx, demo_drag_last_my;
    int demo_dist;
    int demo_shape;
    int demo_point_count;
    int demo_points[8][3];
    int fps_x, fps_y, fps_dir;
    uint16_t image[IMG_MAX_W * IMG_MAX_H];
    
    // Alias storage
    char alias_names[MAX_ALIAS_VARS][ENV_NAME_LEN];
    char alias_values[MAX_ALIAS_VARS][ENV_VALUE_LEN];
    int alias_count;
    
    // Environment variables
    char env_names[MAX_ENV_VARS][ENV_NAME_LEN];
    char env_values[MAX_ENV_VARS][ENV_VALUE_LEN];
    int env_count;
};

extern struct Window wins[MAX_WINDOWS];
extern int active_win_idx;
extern int gui_task_id;
void gui_task(void);
extern struct Window fs_tmp_window;
extern unsigned char file_io_buf[];

// 共用函數宣告
struct dir_block *load_current_dir(struct Window *w);
int find_entry_index(struct dir_block *db, const char *name, int type_filter);
int find_free_entry_index(struct dir_block *db);
uint32_t current_fs_time(void);
void copy_name20(char *dst, const char *src);
void format_size_human(uint32_t bytes, char *out);
void append_out_str(char *out, int out_max, const char *s);
void append_out_pad(char *out, int out_max, const char *s, int width);
void append_dir_entries_sorted(struct dir_block *db, const char *title, const char *name_prefix, int type_filter, char *out);
void list_dir_contents(uint32_t bno, char *out);
const char *path_basename(const char *p);
void copy_last_path_segment(char *dst, const char *path, const char *fallback);
void path_set_child(char *cwd, const char *name);
void path_set_parent(char *cwd);
int path_is_sftp(const char *path);
const char *sftp_subpath(const char *path);
int copy_between_paths(struct Window *w, const char *src, const char *dst, char *out, int out_max);
int local_path_info(struct Window *w, const char *path, int *type_out, uint32_t *bno_out, char *name_out);
int copy_local_dir_recursive(struct Window *w, uint32_t src_bno, const char *dst_path, char *out, int out_max);
int copy_sftp_dir_to_local_recursive(struct Window *w, const char *src_remote, const char *dst_path, char *out, int out_max);
void reset_window(struct Window *w, int idx);
uint32_t balloc(void);
void bfree(uint32_t bno);
void fs_free_chain(uint32_t bno);
void fs_reset_window_cwd(struct Window *w);
void fs_format_root(struct Window *owner);
void fs_usage_info(uint32_t *total_blocks, uint32_t *used_blocks, uint32_t *free_blocks);
int remove_entry_named(struct Window *w, const char *name);
int move_entry_named(struct Window *w, const char *src, const char *dst);
int resolve_fs_target(struct Window *term, const char *input, uint32_t *dir_bno_out, char *cwd_out, char *leaf_out);
int load_file_bytes(struct Window *w, const char *name, unsigned char *dst, uint32_t max_size, uint32_t *out_size);
int load_file_bytes_alloc(struct Window *w, const char *name, unsigned char **dst, uint32_t *out_size);
int resolve_editor_target(struct Window *term, const char *input, uint32_t *dir_bno_out, char *cwd_out, char *leaf_out);
void build_editor_path(char *dst, const char *cwd, const char *name);
void shorten_path_for_title(char *dst, const char *src, int max_len);
void appfs_set_cwd(uint32_t cwd_bno, const char *cwd);
void redraw_prompt_line(struct Window *w, int row);
int terminal_font_scale(struct Window *w);
int terminal_char_w(struct Window *w);
int terminal_char_h(struct Window *w);
void draw_text_scaled(int x, int y, const char *s, int col, int scale);
uint32_t sys_now(void);
const char *terminal_clipboard_text(void);
void terminal_finish_ctrl_c_cancel(struct Window *w, int killed);
int store_file_bytes(struct Window *w, const char *name, const unsigned char *data, uint32_t size);
void close_window(int idx);
void bring_to_front(int idx);
void wake_terminal_worker_for_window(int win_idx);
void terminal_worker_task(void);
void editor_set_status(struct Window *w, const char *msg);
void editor_clear(struct Window *w);
void editor_load_bytes(struct Window *w, const unsigned char *src, uint32_t size);
int editor_load_file_window(struct Window *w, uint32_t start_line);
void editor_handle_key(struct Window *w, char key);
void editor_render(struct Window *w, int x, int y, int ww, int wh);
int open_text_editor(struct Window *term, const char *name);
int open_image_file(struct Window *term, const char *name);
int parse_demo3d_points(char *spec, int pts[8][3], int *count);
int open_demo3d_window_points(int pts[8][3], int count);
int open_demo3d_cube_window(void);
int open_demo3d_window(void);
int open_frankenstein_window(void);
int open_gbemu_window(struct Window *term, const char *rom_path);
void draw_fps_content(struct Window *w, int wx, int wy, int ww, int wh);
void draw_demo3d_content(struct Window *w, int x, int y, int ww, int wh);
void draw_gbemu_content(struct Window *w, int x, int y, int ww, int wh);
void handle_demo3d_input(struct Window *aw, uint32_t *gui_key);
void handle_fps_input(struct Window *aw, uint32_t *gui_key, int *redraw_needed);
void handle_gbemu_input(struct Window *aw, char *gui_key);
int run_asm_file(struct Window *term, const char *name);
void draw_line_clipped(int x0, int y0, int x1, int y1, int color, int clip_x0, int clip_y0, int clip_x1, int clip_y1);
void draw_triangle_filled_clipped(int x0, int y0, int x1, int y1, int x2, int y2, int color, int clip_x0, int clip_y0, int clip_x1, int clip_y1);
int sin_deg(int deg);
int cos_deg(int deg);

// 環境變數宣告
const char *terminal_env_get(struct Window *w, const char *name);
int terminal_env_set(struct Window *w, const char *name, const char *value);
int terminal_env_unset(struct Window *w, const char *name);
void terminal_load_bashrc(struct Window *w);
void terminal_load_sshboot(struct Window *w);
void terminal_source_script(struct Window *w, const char *path);
int sshboot_password_obfuscate(const char *password, char *out, int out_max);
int terminal_alias_set(struct Window *w, const char *name, const char *value);
int terminal_alias_unset(struct Window *w, const char *name);
const char *terminal_alias_get(struct Window *w, const char *name);
void terminal_app_stdout_flush(struct Window *w);
int terminal_has_nonempty_selection(struct Window *w);

extern void (*loaded_app_entry)(void);
extern reg_t loaded_app_resume_pc;
extern reg_t loaded_app_user_sp;
extern reg_t loaded_app_heap_lo;
extern reg_t loaded_app_heap_hi;
extern reg_t loaded_app_heap_cur;
extern reg_t loaded_app_satp;
extern int loaded_app_exit_code;
extern int app_task_id;
extern int app_owner_win_id;
extern int app_running;
extern uint32_t app_root_pt[1024];
extern uint32_t app_l2_pt[4][1024];

reg_t app_exit_stack_top(void);
void app_vm_reset(void);
reg_t app_heap_alloc(reg_t size);
void app_heap_reset(reg_t lo, reg_t hi);
reg_t app_exit_resume_pc(void);
void app_mark_exited(void);
void app_bootstrap(void);
int app_load_elf_image(const unsigned char *buf, uint32_t size, char *out);
int app_prepare_argv_stack(const char *app_name, const char *argstr, reg_t *user_sp_io);

#endif
