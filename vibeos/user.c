#include "user.h"
#include "user_wget.h"
#include "jit_debugger.h"
#include "tcc_runtime.h"
#include "lwip/ip_addr.h"
#include "lwip/ip4_addr.h"
#include "time.h"

#define CTRLDBG_PRINTF(...) do { } while (0)
#define INPUTDBG_PRINTF(...) do { } while (0)

extern volatile int need_resched;
extern int gui_mx, gui_my, gui_clicked, gui_click_pending, gui_right_clicked, gui_right_click_pending, gui_wheel, gui_shortcut_new_task, gui_shortcut_close_task, gui_shortcut_switch_task, gui_ctrl_pressed;
extern char gui_key;
extern int gui_key_queued(void);
extern int os_debug;
extern unsigned int get_millisecond_timer(void);
extern uint32_t APP_START, APP_END, APP_SIZE;
#ifdef FPGA_MINIMAL
extern void virtio_mouse_poll_task(void);
extern int virtio_hid_ready(void);
#endif

#define TERMINAL_WORKERS 3
#define PROMPT "matrix:~$ > "
#define PROMPT_LEN 12
#define TASKBAR_ID 255
#define DEMO3D_SHAPE_POLY 0
#define DEMO3D_SHAPE_CUBE 1
#define RESIZE_NONE 0
#define RESIZE_LEFT 1
#define RESIZE_RIGHT 2
#define RESIZE_TOP 4
#define RESIZE_BOTTOM 8
#ifndef FS_MAX_BLOCKS
#define FS_MAX_BLOCKS 65536u
#endif
#define ASM_MAX_LINES 256
#define ASM_MAX_LABELS 64
#define ASM_LINE_LEN 192
#define RESIZE_MARGIN 10
#define MIN_WIN_W 220
#define MIN_WIN_H 140
#define TASKBAR_H 30
#define DESKTOP_H (HEIGHT - TASKBAR_H)
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

void format_hms(uint32_t total, char *out);
int app_owner_win_id = -1;
unsigned char file_io_buf[WGET_MAX_FILE_SIZE];
int gui_cursor_mode = CURSOR_NORMAL;
void (*loaded_app_entry)(void) = 0;
reg_t loaded_app_resume_pc = 0;
reg_t loaded_app_user_sp = 0;
reg_t loaded_app_heap_lo = 0;
reg_t loaded_app_heap_hi = 0;
reg_t loaded_app_heap_cur = 0;
reg_t loaded_app_satp = 0;
int loaded_app_exit_code = 0;
int app_task_id = -1;
int app_running = 0;
uint32_t app_root_pt[1024] __attribute__((aligned(4096), section(".apppt")));
uint32_t app_l2_pt[4][1024] __attribute__((aligned(4096), section(".apppt")));



struct Window wins[MAX_WINDOWS];
void redraw_prompt_line(struct Window *w, int row);
void terminal_append_prompt(struct Window *w);
static void release_app_owner_command_state(void) {
    if (app_owner_win_id < 0 || app_owner_win_id >= MAX_WINDOWS) {
        app_owner_win_id = -1;
        return;
    }
    struct Window *w = &wins[app_owner_win_id];
    if (w->active && w->kind == WINDOW_KIND_TERMINAL) {
        terminal_unlock_after_cancel(w);
    }
    app_owner_win_id = -1;
}
void release_finished_app_commands(void) {
    if (app_running) return;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!wins[i].active) continue;
        if (wins[i].kind != WINDOW_KIND_TERMINAL) continue;
        if (wins[i].executing_cmd) {
            wins[i].executing_cmd = 0;
            wins[i].cancel_requested = 0;
        }
    }
}

void app_exit_trampoline(void) {
    int exit_code = loaded_app_exit_code;
    int owner = app_owner_win_id;
    appfs_close_all();
    if (owner >= 0 && owner < MAX_WINDOWS) {
        terminal_app_stdout_flush(&wins[owner]);
    }
    release_app_owner_command_state();
    app_running = 0;
    loaded_app_entry = 0;
    loaded_app_resume_pc = 0;
    loaded_app_user_sp = 0;
    loaded_app_heap_lo = 0;
    loaded_app_heap_hi = 0;
    loaded_app_heap_cur = 0;
    loaded_app_satp = 0;
    loaded_app_exit_code = 0;
    app_owner_win_id = -1;
    memset(app_root_pt, 0, sizeof(app_root_pt));
    memset(app_l2_pt, 0, sizeof(app_l2_pt));
    if (owner >= 0 && owner < MAX_WINDOWS && wins[owner].active && wins[owner].kind == WINDOW_KIND_TERMINAL) {
        char num[16];
        char status[24];
        struct Window *w = &wins[owner];
        lib_strcpy(status, "exit=");
        lib_itoa((uint32_t)exit_code, num);
        lib_strcat(status, num);
        set_shell_status(w, status);
        clear_prompt_input(w);
        redraw_prompt_line(w, w->total_rows - 1);
    }
    for (;;) {
        task_sleep_current();
    }
}
char terminal_clipboard[2048];

void clipboard_set(const char *text) {
    if (!text) return;
    int len = (int)strlen(text);
    if (len >= (int)sizeof(terminal_clipboard)) len = (int)sizeof(terminal_clipboard) - 1;
    memcpy(terminal_clipboard, text, len);
    terminal_clipboard[len] = '\0';
}

const char *clipboard_get(void) {
    return terminal_clipboard;
}
int terminal_worker_task_ids[TERMINAL_WORKERS];
static int network_task_id = -1;
#ifdef FPGA_MINIMAL
static int mouse_poll_task_id = -1;
#endif

static void __attribute__((noinline)) set_terminal_worker_task_id(int idx, int task_id)
{
    if (idx < 0 || idx >= TERMINAL_WORKERS) return;
    terminal_worker_task_ids[idx] = task_id;
}












void wake_network_task(void) {
    if (network_task_id >= 0) task_wake(network_task_id);
}

void network_task_notify(void) {
    wake_network_task();
}



static uint32_t crc32_bytes(const unsigned char *buf, uint32_t len) {
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int k = 0; k < 8; k++) {
            if (crc & 1U) crc = (crc >> 1) ^ 0xEDB88320U;
            else crc >>= 1;
        }
    }
    return ~crc;
}








int str_starts_with(const char *s, const char *prefix);

int str_starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static int is_name_completion_cmd(const char *cmd) {
    return strcmp(cmd, "cd") == 0 ||
           strcmp(cmd, "ls") == 0 ||
           strcmp(cmd, "find") == 0 ||
           strcmp(cmd, "cat") == 0 ||
           strcmp(cmd, "env") == 0 ||
           strcmp(cmd, "echo") == 0 ||
           strcmp(cmd, "open") == 0 ||
           strcmp(cmd, "export") == 0 ||
           strcmp(cmd, "alias") == 0 ||
           strcmp(cmd, "unalias") == 0 ||
           strcmp(cmd, "source") == 0 ||
           strcmp(cmd, "run") == 0 ||
           strcmp(cmd, "jit") == 0 ||
           strcmp(cmd, "ssh") == 0 ||
           strcmp(cmd, "sftp") == 0 ||
           strcmp(cmd, "wget") == 0 ||
           strcmp(cmd, "unset") == 0 ||
           strcmp(cmd, "wrp") == 0 ||
           strcmp(cmd, "write") == 0 ||
           strcmp(cmd, "mkdir") == 0 ||
           strcmp(cmd, "gbemu") == 0 ||
           strcmp(cmd, "netsurf") == 0 ||
           strcmp(cmd, "rm") == 0 ||
           strcmp(cmd, "touch") == 0 ||
           strcmp(cmd, "vim") == 0 ||
           strcmp(cmd, "mv") == 0 ||
           strcmp(cmd, "rename") == 0;
}

int terminal_first_token(const char *line, char *out, int out_size) {
    int i = 0;
    int j = 0;
    if (!line || !out || out_size <= 0) return 0;
    while (line[i] == ' ') i++;
    while (line[i] && line[i] != ' ' && j < out_size - 1) {
        out[j++] = line[i++];
    }
    out[j] = '\0';
    return j > 0;
}

static int terminal_command_uses_path_completion(const char *cmd) {
    if (!cmd || !cmd[0]) return 0;
    return is_name_completion_cmd(cmd) ||
           strcmp(cmd, "vim") == 0 ||
           strcmp(cmd, "cat") == 0 ||
           strcmp(cmd, "cd") == 0 ||
           strcmp(cmd, "mkdir") == 0 ||
           strcmp(cmd, "rm") == 0 ||
           strcmp(cmd, "touch") == 0 ||
           strcmp(cmd, "write") == 0 ||
           strcmp(cmd, "open") == 0 ||
           strcmp(cmd, "run") == 0 ||
           strcmp(cmd, "jit") == 0 ||
           strcmp(cmd, "gbemu") == 0 ||
           strcmp(cmd, "netsurf") == 0 ||
           strcmp(cmd, "source") == 0;
}

static int fuzzy_match_ordered(const char *cand, const char *pat) {
    if (!pat || !pat[0]) return 0;
    while (*cand && *pat) {
        if (tolower((int)*cand) == tolower((int)*pat)) pat++;
        cand++;
    }
    return *pat == '\0';
}

static void apply_completion(struct Window *w, int row, int token_start, int token_end, const char *replacement, int add_space) {
    char newbuf[COLS];
    int pos = 0;
    (void)add_space;
    for (int i = 0; i < token_start && pos < COLS - 1; i++) newbuf[pos++] = w->cmd_buf[i];
    for (int i = 0; replacement[i] && pos < COLS - 1; i++) newbuf[pos++] = replacement[i];
    for (int i = token_end; w->cmd_buf[i] && pos < COLS - 1; i++) newbuf[pos++] = w->cmd_buf[i];
    newbuf[pos] = '\0';
    load_edit_buffer(w, newbuf);
    w->cursor_pos = token_start + strlen(replacement);
    redraw_prompt_line(w, row);
}

static int tab_complete_command(struct Window *w, int row, int token_start, int token_end) {
    static const char *cmds[] = {
        "pwd", "format", "cd", "ls", "mkdir", "rm", "touch", "write", "cat", "wget", "open", "run", "jit", "gbemu", "demo3d", "frankenstein", "help", "clear", "mv", "rename", "netsurf", "ssh", "sftp", "wrp", "find", "mem", "df", "du", "alias", "unalias", "export", "unset", "source"
    };
    char prefix[COLS];
    int plen = token_end - token_start;
    if (plen < 0) plen = 0;
    if (plen >= COLS) plen = COLS - 1;
    for (int i = 0; i < plen; i++) prefix[i] = w->cmd_buf[token_start + i];
    prefix[plen] = '\0';

    int matches = 0;
    int fuzzy_matches = 0;
    const char *best = 0;
    const char *fuzzy_best = 0;
    char common[COLS];
    common[0] = '\0';
    for (unsigned int i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        if (str_starts_with(cmds[i], prefix)) {
            if (matches == 0) {
                lib_strcpy(common, cmds[i]);
                best = cmds[i];
            } else {
                int j = 0;
                while (common[j] && cmds[i][j] && common[j] == cmds[i][j]) j++;
                common[j] = '\0';
            }
            matches++;
        } else if (fuzzy_match_ordered(cmds[i], prefix)) {
            if (!fuzzy_best || strlen(cmds[i]) < strlen(fuzzy_best)) {
                fuzzy_best = cmds[i];
            }
            fuzzy_matches++;
        }
    }
    if (matches > 0) {
        apply_completion(w, row, token_start, token_end, (matches == 1) ? best : common, matches == 1);
        return 1;
    }
    if (fuzzy_matches > 0 && fuzzy_best) {
        apply_completion(w, row, token_start, token_end, fuzzy_best, 1);
        return 1;
    }
    return 0;
}

static int tab_complete_sftp_name(struct Window *w, int row, int token_start, int token_end, const char *prefix) {
    char dir_part[128];
    char leaf_prefix[64];
    char remote_dir[128];
    char list_out[OUT_BUF_SIZE];
    char best[96];
    char common[96];
    int matches = 0;

    if (!w || !prefix) return 0;
    if (strcmp(prefix, "/sftp") == 0 || strcmp(prefix, "/sftpd") == 0) {
        char repl[16];
        lib_strcpy(repl, prefix);
        lib_strcat(repl, "/");
        apply_completion(w, row, token_start, token_end, repl, 0);
        return 1;
    }
    if (!path_is_sftp(prefix)) return 0;

    {
        const char *last_slash = strrchr(prefix, '/');
        if (!last_slash || last_slash == prefix) {
            lib_strcpy(dir_part, "/sftpd/");
            leaf_prefix[0] = '\0';
        } else {
            int dlen = (int)(last_slash - prefix + 1);
            if (dlen >= (int)sizeof(dir_part)) dlen = sizeof(dir_part) - 1;
            memcpy(dir_part, prefix, dlen);
            dir_part[dlen] = '\0';
            lib_strcpy(leaf_prefix, last_slash + 1);
        }
    }

    lib_strcpy(remote_dir, sftp_subpath(dir_part));
    if (ssh_client_sftp_ls(remote_dir, 0, list_out, sizeof(list_out)) != 0) {
        set_shell_status(w, list_out[0] ? list_out : "SFTP completion failed.");
        redraw_prompt_line(w, row);
        return 1;
    }
    if (strcmp(list_out, "(empty)") == 0) {
        set_shell_status(w, "SFTP: empty");
        redraw_prompt_line(w, row);
        return 1;
    }

    if (leaf_prefix[0] == '\0') {
        char shown[OUT_BUF_SIZE];
        shown[0] = '\0';
        lib_strcat(shown, "SFTP possibilities:\n");
        lib_strcat(shown, list_out);
        append_terminal_line(w, shown);
        terminal_append_prompt(w);
        terminal_scroll_to_bottom(w);
        redraw_prompt_line(w, (w->total_rows > 0) ? (w->total_rows - 1) : 0);
        w->mailbox = 1;
        request_gui_redraw();
        wake_terminal_worker_for_window(w->id);
        return 1;
    }

    common[0] = '\0';
    best[0] = '\0';
    {
        char *line = list_out;
        while (*line) {
            char type = line[0];
            char *name = line + 2;
            char *next = strchr(line, '\n');
            if (next) *next = '\0';
            if ((type == 'd' || type == 'f') && line[1] == ' ' && str_starts_with(name, leaf_prefix)) {
                if (matches == 0) {
                    lib_strcpy(common, name);
                    lib_strcpy(best, name);
                } else {
                    int j = 0;
                    while (common[j] && name[j] && common[j] == name[j]) j++;
                    common[j] = '\0';
                }
                matches++;
            }
            if (!next) break;
            *next = '\n';
            line = next + 1;
        }
    }

    if (matches > 1) {
        char shown[OUT_BUF_SIZE];
        shown[0] = '\0';
        lib_strcat(shown, "SFTP possibilities:\n");
        {
            char *line = list_out;
            while (*line) {
                char *name = line + 2;
                char *next = strchr(line, '\n');
                if (next) *next = '\0';
                if ((line[0] == 'd' || line[0] == 'f') && line[1] == ' ' && str_starts_with(name, leaf_prefix)) {
                    lib_strcat(shown, line);
                    lib_strcat(shown, "\n");
                }
                if (!next) break;
                *next = '\n';
                line = next + 1;
            }
        }
        append_terminal_line(w, shown);
        terminal_append_prompt(w);
        terminal_scroll_to_bottom(w);
        redraw_prompt_line(w, (w->total_rows > 0) ? (w->total_rows - 1) : 0);
        w->mailbox = 1;
        request_gui_redraw();
        wake_terminal_worker_for_window(w->id);
        return 1;
    }

    if (matches == 1) {
        char replacement[192];
        int is_dir = 0;
        char *line = list_out;
        while (*line) {
            char *name = line + 2;
            char *next = strchr(line, '\n');
            if (next) *next = '\0';
            if ((line[0] == 'd' || line[0] == 'f') && line[1] == ' ' && strcmp(name, best) == 0) {
                is_dir = (line[0] == 'd');
                if (next) *next = '\n';
                break;
            }
            if (!next) break;
            *next = '\n';
            line = next + 1;
        }
        lib_strcpy(replacement, dir_part);
        lib_strcat(replacement, best);
        if (is_dir) lib_strcat(replacement, "/");
        apply_completion(w, row, token_start, token_end, replacement, 0);
        return 1;
    }

    return 0;
}

static int tab_complete_name(struct Window *w, int row, int token_start, int token_end, int type_filter) {
    char prefix[COLS];
    int plen = token_end - token_start;
    if (plen < 0) plen = 0; if (plen >= COLS) plen = COLS - 1;
    for (int i = 0; i < plen; i++) prefix[i] = w->cmd_buf[token_start + i];
    prefix[plen] = '\0';

    if (path_is_sftp(prefix) || strcmp(prefix, "/sftp") == 0 || strcmp(prefix, "/sftpd") == 0) {
        if (tab_complete_sftp_name(w, row, token_start, token_end, prefix)) return 1;
    }

    uint32_t target_dir_bno = w->cwd_bno;
    char leaf_prefix[32]; leaf_prefix[0] = '\0';
    char dir_part[128]; dir_part[0] = '\0';

    if (strchr(prefix, '/') || prefix[0] == '~') {
        char *last_slash = strrchr(prefix, '/');
        if (last_slash) {
            int dlen = (int)(last_slash - prefix + 1);
            memcpy(dir_part, prefix, dlen); dir_part[dlen] = '\0';
            lib_strcpy(leaf_prefix, last_slash + 1);
        } else if (prefix[0] == '~') {
            lib_strcpy(dir_part, "~/"); lib_strcpy(leaf_prefix, prefix + 1);
        } else {
            lib_strcpy(dir_part, "./"); lib_strcpy(leaf_prefix, prefix);
        }

        uint32_t p_bno = 0; char p_cwd[128], leaf_unused[20];
        char expanded[128];
        terminal_expand_home_path(w, dir_part, expanded, sizeof(expanded));
        if (resolve_editor_target(w, expanded, &p_bno, p_cwd, leaf_unused) == 0) {
            target_dir_bno = p_bno;
        } else return 0;
    } else {
        lib_strcpy(leaf_prefix, prefix);
    }

    extern struct Window fs_tmp_window; struct Window *ctx = &fs_tmp_window;
    memset(ctx, 0, sizeof(*ctx)); ctx->cwd_bno = target_dir_bno;
    struct dir_block *db = load_current_dir(ctx);
    if (!db) return 0;

    // Smart listing: If leaf_prefix is empty (path ends in /) or exact dir match
    if (leaf_prefix[0] == '\0') {
        char list_out[OUT_BUF_SIZE];
        list_dir_contents(target_dir_bno, list_out);
        append_terminal_line(w, list_out);
        terminal_append_prompt(w);
        terminal_scroll_to_bottom(w);
        redraw_prompt_line(w, (w->total_rows > 0) ? (w->total_rows - 1) : 0);
        w->mailbox = 1;
        request_gui_redraw();
        extern void wake_terminal_worker_for_window(int id);
        wake_terminal_worker_for_window(w->id);
        return 1;
    }

    int matches = 0;
    char best[20], common[20];
    common[0] = '\0'; best[0] = '\0';
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (!db->entries[i].name[0]) continue;
        if (type_filter != -1 && db->entries[i].type != type_filter) continue;
        if (str_starts_with(db->entries[i].name, leaf_prefix)) {
            if (matches == 0) {
                lib_strcpy(common, db->entries[i].name); lib_strcpy(best, db->entries[i].name);
            } else {
                int j = 0; while (common[j] && db->entries[i].name[j] && common[j] == db->entries[i].name[j]) j++;
                common[j] = '\0';
            }
            matches++;
        }
    }
    if (target_dir_bno == 1 && (type_filter == -1 || type_filter == 1) && str_starts_with("sftpd", leaf_prefix)) {
        if (matches == 0) {
            lib_strcpy(common, "sftpd");
            lib_strcpy(best, "sftpd");
        } else {
            int j = 0;
            while (common[j] && "sftpd"[j] && common[j] == "sftpd"[j]) j++;
            common[j] = '\0';
        }
        matches++;
    }
    
    if (matches > 1) {
        char list_out[OUT_BUF_SIZE]; list_out[0] = '\0';
        append_dir_entries_sorted(db, "Possibilities:\n", leaf_prefix, type_filter, list_out);
        // 1. Append the file list
        append_terminal_line(w, list_out);

        // 2. Create the new prompt line and force it to be the visible input line
        terminal_append_prompt(w);
        terminal_scroll_to_bottom(w);
        redraw_prompt_line(w, (w->total_rows > 0) ? (w->total_rows - 1) : 0);
        // 3. Force a GUI redraw pulse for the new bottom prompt
        w->mailbox = 1;
        request_gui_redraw();
        extern void wake_terminal_worker_for_window(int id);
        wake_terminal_worker_for_window(w->id);
        return 1;
    }

    if (matches == 1) {
        char full_replacement[160];
        lib_strcpy(full_replacement, dir_part);
        lib_strcat(full_replacement, best);
        int idx = find_entry_index(db, (char *)best, 1);
        // Add slash if it's a directory to allow immediate recursion on next Tab
        if (idx >= 0 || (target_dir_bno == 1 && strcmp(best, "sftpd") == 0)) lib_strcat(full_replacement, "/");
        apply_completion(w, row, token_start, token_end, full_replacement, 0);
        return 1;
    }
    
    // If no matches but leaf_prefix itself IS a directory, add a slash
    int idx = find_entry_index(db, leaf_prefix, 1);
    if (idx >= 0) {
        char full_replacement[160];
        lib_strcpy(full_replacement, dir_part);
        lib_strcat(full_replacement, leaf_prefix);
        lib_strcat(full_replacement, "/");
        apply_completion(w, row, token_start, token_end, full_replacement, 0);
        return 1;
    }

    return 0;
}

void try_tab_complete(struct Window *w, int row) {
    if (w->cursor_pos < 0 || w->cursor_pos > w->edit_len) return;
    int token_start = w->cursor_pos;
    while (token_start > 0 && w->cmd_buf[token_start - 1] != ' ') token_start--;
    int token_end = w->cursor_pos;
    while (token_end < w->edit_len && w->cmd_buf[token_end] != ' ') token_end++;

    char prefix[COLS];
    int plen = token_end - token_start;
    if (plen < 0) plen = 0; if (plen >= COLS) plen = COLS - 1;
    for (int i = 0; i < plen; i++) prefix[i] = w->cmd_buf[token_start + i];
    prefix[plen] = '\0';

    char resolved_cmd[COLS];
    char expanded_line[COLS];
    terminal_expand_alias_command(w, w->cmd_buf, expanded_line, sizeof(expanded_line));
    if (!terminal_first_token(expanded_line, resolved_cmd, sizeof(resolved_cmd))) {
        resolved_cmd[0] = '\0';
    }
    set_shell_status(w, "");

    // If there's a space before and no prefix, or prefix has path chars, use path completion
    if (token_start > 0) {
        if (terminal_command_uses_path_completion(resolved_cmd) ||
            strchr(prefix, '/') || prefix[0] == '~' || prefix[0] == '.') {
            if (tab_complete_name(w, row, token_start, token_end, -1)) return;
        }
    }

    if (strchr(prefix, '/') || prefix[0] == '~' || prefix[0] == '.') {
        if (tab_complete_name(w, row, token_start, token_end, -1)) return;
    }

    // Default to command completion for the first token
    int first_end = 0;
    while (first_end < w->edit_len && w->cmd_buf[first_end] != ' ') first_end++;
    if (token_start < first_end || token_start == 0) {
        if (tab_complete_command(w, row, token_start, token_end)) return;
    }

    set_shell_status(w, "No completion");
    redraw_prompt_line(w, row);
}

int find_entry_index(struct dir_block *db, const char *name, int type_filter) {
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (!db->entries[i].name[0]) continue;
        if (type_filter != -1 && db->entries[i].type != type_filter) continue;
        if (strcmp(db->entries[i].name, name) == 0) return i;
    }
    return -1;
}

void reset_window(struct Window *w, int idx) {
    memset(w, 0, sizeof(*w));
    w->id = idx;
    w->cwd_bno = 1;
    w->term_font_scale = 1;
    lib_strcpy(w->cwd, "/root");
}





static void append_u2(char *out, unsigned int v) {
    out[0] = (char)('0' + ((v / 10U) % 10U));
    out[1] = (char)('0' + (v % 10U));
}

static void append_u4(char *out, unsigned int v) {
    out[0] = (char)('0' + ((v / 1000U) % 10U));
    out[1] = (char)('0' + ((v / 100U) % 10U));
    out[2] = (char)('0' + ((v / 10U) % 10U));
    out[3] = (char)('0' + (v % 10U));
}

static void format_ymd_hms(char *out, unsigned int year, unsigned int month, unsigned int day,
                           unsigned int hour, unsigned int min, unsigned int sec) {
    append_u4(out, year);
    out[4] = '-';
    append_u2(out + 5, month);
    out[7] = '-';
    append_u2(out + 8, day);
    out[10] = ' ';
    append_u2(out + 11, hour);
    out[13] = ':';
    append_u2(out + 14, min);
    out[16] = ':';
    append_u2(out + 17, sec);
    out[19] = '\0';
}

void format_clock(char *out) {
    time_t t = (time_t)get_wall_clock_seconds() + 8 * 3600;
    struct tm tmv;
    gmtime_r(&t, &tmv);
    format_ymd_hms(out,
                   (unsigned int)(tmv.tm_year + 1900),
                   (unsigned int)(tmv.tm_mon + 1),
                   (unsigned int)tmv.tm_mday,
                   (unsigned int)tmv.tm_hour,
                   (unsigned int)tmv.tm_min,
                   (unsigned int)tmv.tm_sec);
}

uint32_t current_fs_time(void) {
    return get_wall_clock_seconds();
}

void format_hms(uint32_t total, char *out) {
    time_t t = (time_t)total + 8 * 3600;
    struct tm tmv;
    gmtime_r(&t, &tmv);
    format_ymd_hms(out,
                   (unsigned int)(tmv.tm_year + 1900),
                   (unsigned int)(tmv.tm_mon + 1),
                   (unsigned int)tmv.tm_mday,
                   (unsigned int)tmv.tm_hour,
                   (unsigned int)tmv.tm_min,
                   (unsigned int)tmv.tm_sec);
}











void network_task(void) {
#ifdef FPGA_MINIMAL
    extern void ps_gem_init(void);
    extern void ps_gem_poll(void);
    extern int ps_gem_has_pending_irq(void);
    extern int ps_gem_has_rx_ready(void);
    extern int ps_gem_rx_pending_count(void);
    extern void vibe_time_sync_start(void);
    extern void vibe_time_sync_poll(void);
#else
    extern void virtio_net_rx_loop2();
    extern int virtio_net_has_pending_irq();
    extern int virtio_net_has_rx_ready();
    extern int virtio_net_rx_pending_count();
#endif
    lib_printf("[BOOT] network_task start\n");
#ifdef FPGA_MINIMAL
    uint32_t usb_wait_start = get_millisecond_timer();
    while (!virtio_hid_ready() &&
           (uint32_t)(get_millisecond_timer() - usb_wait_start) < 3000U) {
        task_os();
    }
    ps_gem_init();
    vibe_time_sync_start();
    uint32_t last_gem_idle_poll_ms = get_millisecond_timer();
#endif
    while (1) {
        int did_work = 0;
        int gui_busy = gui_clicked || gui_click_pending || gui_wheel != 0 || gui_key_queued();
#ifdef FPGA_MINIMAL
        int rx_pending = ps_gem_rx_pending_count();
#else
        int rx_pending = virtio_net_rx_pending_count();
#endif
        uint32_t save_available = wget_job.body_len - wget_save.written;
        int can_stream_save = wget_job.save_pending &&
                              wget_job.success &&
                              (wget_save.active ||
                               save_available >= FS_DATA_PAYLOAD ||
                               wget_job.done);
        if (wget_job.owner_win_id >= 0 && wget_job.owner_win_id < MAX_WINDOWS &&
            wins[wget_job.owner_win_id].waiting_wget && wins[wget_job.owner_win_id].cancel_requested &&
            (wget_job.request_pending || wget_job.active)) {
            wins[wget_job.owner_win_id].cancel_requested = 0;
            wget_close_pcb(1);
            wget_finish(0, "INTERRUPTED");
            did_work = 1;
        }
        if (!did_work && wget_job.request_pending && !wget_job.active) {
            lib_printf("[NET_TASK] request pending, kicking wget\n");
            wget_kick_if_needed();
            did_work = 1;
        }
        if (!did_work && wget_timeout_step()) {
            did_work = 1;
        }
        if (!did_work &&
#ifdef FPGA_MINIMAL
            (ps_gem_has_pending_irq() || rx_pending > 0)
#else
            (virtio_net_has_pending_irq() || rx_pending > 0)
#endif
        ) {
            int rx_budget = 1;
            if (!gui_busy) {
                if (rx_pending >= 8) rx_budget = 8;
                else if (rx_pending >= 4) rx_budget = 4;
                else if (rx_pending >= 2) rx_budget = 2;
            }
            while (rx_budget-- > 0 &&
#ifdef FPGA_MINIMAL
                   (ps_gem_has_pending_irq() || ps_gem_has_rx_ready())
#else
                   (virtio_net_has_pending_irq() || virtio_net_has_rx_ready())
#endif
            ) {
#ifdef FPGA_MINIMAL
                ps_gem_poll();
#else
                virtio_net_rx_loop2();
#endif
            }
            did_work = 1;
        }
        if (!did_work && can_stream_save && (wget_job.done || wget_save_turn) &&
#ifdef FPGA_MINIMAL
            !ps_gem_has_pending_irq() && !ps_gem_has_rx_ready()
#else
            !virtio_net_has_pending_irq() && !virtio_net_has_rx_ready()
#endif
        ) {
            if (wget_job.success) {
                int rc;
                int save_budget = 1;
                if (wget_job.done && !gui_busy) save_budget = 24;
                else if (!gui_busy && save_available >= FS_DATA_PAYLOAD * 4U) save_budget = 4;
                if (!wget_save.active) {
                    rc = wget_save_begin();
                    if (rc < 0) wget_save.failed = 1;
                }
                while (!wget_save.failed && wget_save.active && save_budget-- > 0) {
                    rc = wget_save_step();
                    if (rc < 0) {
                        wget_save.failed = 1;
                        break;
                    }
                    if (!wget_job.done) break;
                }
                if (wget_save.failed || !wget_save.active) {
                    wget_job.save_pending = 0;
                    if (wget_job.owner_win_id >= 0 && wget_job.owner_win_id < MAX_WINDOWS &&
                        wins[wget_job.owner_win_id].active && wins[wget_job.owner_win_id].kind == WINDOW_KIND_TERMINAL) {
                        struct Window *ow = &wins[wget_job.owner_win_id];
                        int follow_bottom = terminal_is_at_bottom(ow);
                        if (!wget_save.failed) append_terminal_line(ow, ">> Downloaded.");
                        else append_terminal_line(ow, "ERR: Save Failed.");
                        ow->waiting_wget = 0;
                        clear_prompt_input(ow);
                        terminal_append_prompt(ow);
                        if (follow_bottom) terminal_scroll_to_bottom(ow);
                    }
                }
            } else if (wget_job.owner_win_id >= 0 && wget_job.owner_win_id < MAX_WINDOWS &&
                       wins[wget_job.owner_win_id].active && wins[wget_job.owner_win_id].kind == WINDOW_KIND_TERMINAL) {
                wget_job.save_pending = 0;
                char line[COLS];
                struct Window *ow = &wins[wget_job.owner_win_id];
                int follow_bottom = terminal_is_at_bottom(ow);
                line[0] = '\0';
                if (strcmp(wget_job.err, "INTERRUPTED") == 0) {
                    lib_strcpy(line, "^C");
                } else {
                    lib_strcpy(line, "ERR: ");
                    lib_strcat(line, wget_job.err[0] ? wget_job.err : "wget fail");
                }
                append_terminal_line(ow, line);
                ow->waiting_wget = 0;
                clear_prompt_input(ow);
                terminal_append_prompt(ow);
                if (follow_bottom) terminal_scroll_to_bottom(ow);
            }
            did_work = 1;
        }
        if (!did_work && wget_job.done && wget_job.save_pending && !wget_job.success &&
            wget_job.owner_win_id >= 0 && wget_job.owner_win_id < MAX_WINDOWS &&
            wins[wget_job.owner_win_id].active && wins[wget_job.owner_win_id].kind == WINDOW_KIND_TERMINAL) {
            wget_job.save_pending = 0;
            char line[COLS];
            struct Window *ow = &wins[wget_job.owner_win_id];
            int follow_bottom = terminal_is_at_bottom(ow);
            line[0] = '\0';
            if (strcmp(wget_job.err, "INTERRUPTED") == 0) {
                lib_strcpy(line, "^C");
            } else {
                lib_strcpy(line, "ERR: ");
                lib_strcat(line, wget_job.err[0] ? wget_job.err : "wget fail");
            }
            append_terminal_line(ow, line);
            ow->waiting_wget = 0;
            clear_prompt_input(ow);
            terminal_append_prompt(ow);
            if (follow_bottom) terminal_scroll_to_bottom(ow);
            did_work = 1;
        }
        if (!wget_job.done) {
            if (can_stream_save) wget_save_turn ^= 1;
            else wget_save_turn = 1;
        } else {
            wget_save_turn = 1;
        }
        sys_check_timeouts();
#ifdef FPGA_MINIMAL
        vibe_time_sync_poll();
        if (!did_work) {
            uint32_t now_ms = get_millisecond_timer();
            if ((uint32_t)(now_ms - last_gem_idle_poll_ms) >= 8U) {
                ps_gem_poll();
                last_gem_idle_poll_ms = now_ms;
            }
        }
        task_os();
#else
        if (!did_work &&
            !wget_job.request_pending && !wget_job.active && !wget_job.save_pending &&
            !virtio_net_has_pending_irq() && !virtio_net_has_rx_ready()) {
            task_sleep_current();
        } else {
            task_os();
        }
#endif
    }
}
void user_init() {
#ifndef FPGA_MINIMAL
    os_jit_init();
#endif
    for (int i = 0; i < TERMINAL_WORKERS; i++) terminal_worker_task_ids[i] = -1;
    gui_task_id = task_create(&gui_task, 0, 1);
#ifdef FPGA_MINIMAL
    mouse_poll_task_id = task_create(&virtio_mouse_poll_task, 0, 1);
    for (int i = 0; i < TERMINAL_WORKERS; i++) {
        int task_id = task_create(&terminal_worker_task, 0, 1);
        set_terminal_worker_task_id(i, task_id);
    }
    network_task_id = task_create(&network_task, 1, 1);
    app_task_id = -1;
    return;
#endif
    for (int i = 0; i < TERMINAL_WORKERS; i++) {
        int task_id = task_create(&terminal_worker_task, 0, 1);
        set_terminal_worker_task_id(i, task_id);
    }
#ifndef FPGA_MINIMAL
    network_task_id = task_create(&network_task, 1, 1);
#endif
    app_task_id = task_create(&app_bootstrap, 0, 1);
}
