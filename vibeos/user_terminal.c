#include "user_terminal.h"
#include "user_internal.h"
#include "jit_debugger.h"

#define CTRLDBG_PRINTF(...) do { } while (0)
#define INPUTDBG_PRINTF(...) do { } while (0)

static int terminal_is_ssh_auth_request(const char *cmd);
static int terminal_is_ssh_boot_seal_request(const char *cmd);
static void terminal_begin_ssh_auth(struct Window *w);
static void terminal_begin_ssh_boot_seal(struct Window *w);
static void terminal_refresh_ssh_auth_prompt(struct Window *w);
static void terminal_cancel_ssh_auth(struct Window *w);
static void terminal_submit_ssh_auth(struct Window *w);
static int terminal_source_depth = 0;

void clear_window_input_queue(struct Window *w) {
    if (!w) return;
    w->input_head = w->input_tail;
    w->mailbox = 0;
}

void terminal_unlock_after_cancel(struct Window *w) {
    if (!w || w->kind != WINDOW_KIND_TERMINAL) return;
    w->executing_cmd = 0;
    w->waiting_wget = 0;
    w->cancel_requested = 0;
    w->submit_locked = 0;
    clear_window_input_queue(w);
    clear_prompt_input(w);
    terminal_append_prompt(w);
    terminal_scroll_to_bottom(w);
}

void terminal_finish_ctrl_c_cancel(struct Window *w, int killed) {
    char line[64];
    if (!w || w->kind != WINDOW_KIND_TERMINAL) return;
    if (killed > 0) {
        snprintf(line, sizeof(line), "JIT: killed %d job(s).", killed);
        append_terminal_line(w, line);
    }
    terminal_unlock_after_cancel(w);
}

void redraw_prompt_line(struct Window *w, int row) {
    if (row < 0 || row >= ROWS) return;
    lib_strcpy(w->lines[row], PROMPT);
    lib_strcat(w->lines[row], w->cmd_buf);
    w->cur_col = PROMPT_LEN + w->cursor_pos;
    request_gui_redraw();
}

void load_edit_buffer(struct Window *w, const char *src) {
    int i = 0;
    while (i < COLS - PROMPT_LEN - 1 && src[i]) {
        w->cmd_buf[i] = src[i];
        i++;
    }
    w->cmd_buf[i] = '\0';
    w->edit_len = i;
    w->cursor_pos = i;
}

void clear_prompt_input(struct Window *w) {
    w->cmd_buf[0] = '\0';
    w->edit_len = 0;
    w->cursor_pos = 0;
    w->has_saved_cmd = 0;
}

static int terminal_is_ssh_auth_request(const char *cmd) {
    const char *p;
    if (!cmd) return 0;
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    if (strncmp(cmd, "ssh auth", 8) != 0) return 0;
    p = cmd + 8;
    while (*p == ' ' || *p == '\t') p++;
    return (*p == '\0');
}

static int terminal_is_ssh_boot_seal_request(const char *cmd) {
    const char *p;
    if (!cmd) return 0;
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    if (strncmp(cmd, "ssh boot seal", 13) != 0) return 0;
    p = cmd + 13;
    while (*p == ' ' || *p == '\t') p++;
    return (*p == '\0');
}

static void terminal_refresh_ssh_auth_prompt(struct Window *w) {
    char display[COLS];
    const char *prefix;
    int prefix_len;
    int i;
    if (!w || !w->ssh_auth_mode) return;
    prefix = (w->ssh_auth_mode == 2) ? "ssh boot seal " : "ssh auth ";
    prefix_len = (int)strlen(prefix);
    lib_strcpy(display, prefix);
    for (i = 0; i < w->ssh_auth_len && (prefix_len + i) < COLS - 1; i++) {
        display[prefix_len + i] = '*';
    }
    display[prefix_len + i] = '\0';
    lib_strcpy(w->cmd_buf, display);
    w->edit_len = prefix_len + i;
    w->cursor_pos = w->edit_len;
    if (w->total_rows > 0) redraw_prompt_line(w, w->total_rows - 1);
}

static void terminal_begin_ssh_secret(struct Window *w, int mode) {
    if (!w || w->kind != WINDOW_KIND_TERMINAL) return;
    w->ssh_auth_mode = mode;
    w->ssh_auth_len = 0;
    w->ssh_auth_buf[0] = '\0';
    w->submit_locked = 0;
    clear_prompt_input(w);
    terminal_refresh_ssh_auth_prompt(w);
    terminal_scroll_to_bottom(w);
}

static void terminal_begin_ssh_auth(struct Window *w) {
    terminal_begin_ssh_secret(w, 1);
}

static void terminal_begin_ssh_boot_seal(struct Window *w) {
    terminal_begin_ssh_secret(w, 2);
}

static void terminal_cancel_ssh_auth(struct Window *w) {
    if (!w || !w->ssh_auth_mode) return;
    w->ssh_auth_mode = 0;
    w->ssh_auth_len = 0;
    w->ssh_auth_buf[0] = '\0';
    w->submit_locked = 0;
    clear_prompt_input(w);
    if (w->total_rows > 0) redraw_prompt_line(w, w->total_rows - 1);
    terminal_scroll_to_bottom(w);
}

static void terminal_submit_ssh_auth(struct Window *w) {
    char msg[OUT_BUF_SIZE];
    if (!w || !w->ssh_auth_mode) return;
    msg[0] = '\0';
    if (w->ssh_auth_mode == 2) {
        char enc[160];
        char target[96];
        char tmpl[512];
        if (sshboot_password_obfuscate(w->ssh_auth_buf, enc, sizeof(enc)) == 0) {
            if (ssh_client_get_target(target, sizeof(target)) != 0 || target[0] == '\0') {
                lib_strcpy(target, "root@192.168.123.100:2221");
            }
            snprintf(tmpl, sizeof(tmpl),
                     "# ssh auto-login boot config\n"
                     "# password_obf is obfuscated, not strong encryption\n"
                     "target=%s\n"
                     "password_obf=%s\n"
                     "sftp=/root/trade_new/os/mini-riscv-os/08-BlockDeviceDriver/http_test\n",
                     target, enc);
            if (store_file_bytes(w, "~/.sshboot", (const unsigned char *)tmpl, (uint32_t)strlen(tmpl)) == 0) {
                terminal_load_sshboot(w);
                append_terminal_line(w, "OK: wrote obfuscated ~/.sshboot and loaded it.");
            } else {
                append_terminal_line(w, "ERR: cannot write ~/.sshboot.");
            }
        } else {
            append_terminal_line(w, "ERR: password too long.");
        }
    } else if (ssh_client_set_password(w->ssh_auth_buf, msg, OUT_BUF_SIZE) == 0) {
        append_terminal_line(w, msg[0] ? msg : "OK: ssh password stored.");
    } else {
        if (msg[0] == '\0') lib_strcpy(msg, "ERR: ssh auth <password>.");
        append_terminal_line(w, msg);
    }
    w->ssh_auth_mode = 0;
    w->ssh_auth_len = 0;
    w->ssh_auth_buf[0] = '\0';
    w->submit_locked = 0;
    clear_prompt_input(w);
    terminal_append_prompt(w);
    terminal_scroll_to_bottom(w);
}

static void terminal_history_set(struct Window *w, int idx, const char *s) {
    int i = 0;
    if (!w || idx < 0 || idx >= MAX_HIST) return;
    if (!s) s = "";
    while (s[i] && i < COLS - 1) {
        w->history[idx][i] = s[i];
        i++;
    }
    w->history[idx][i] = '\0';
}

void seed_terminal_history(struct Window *w) {
    if (!w || w->kind != WINDOW_KIND_TERMINAL) return;
    if (w->hist_count > 0) return;
    terminal_history_set(w, 0, "ssh probe 192.168.0.152:2221");
    terminal_history_set(w, 1, "ssh set root@192.168.0.152:2221");
    terminal_history_set(w, 2, "ssh auth");
    terminal_history_set(w, 3, "ssh exec uname -a");
    terminal_history_set(w, 4, "ssh diag");
    terminal_history_set(w, 5, "env");
    terminal_history_set(w, 6, "alias ll=ls -all");
    terminal_history_set(w, 7, "alias c=clear");
    terminal_history_set(w, 8, "source ~/.bashrc");
    terminal_history_set(w, 9, ". ~/.bashrc");
    terminal_history_set(w, 10, "sftp mount /root/trade_new/os/mini-riscv-os/08-BlockDeviceDriver/http_test");
    terminal_history_set(w, 11, "ls /sftp");
    terminal_history_set(w, 12, "cat /sftp/jit_memtest.c");
    terminal_history_set(w, 13, "jit /sftp/jit_memtest.c");
    terminal_history_set(w, 14, "jit shared reset");
    terminal_history_set(w, 15, "jit bg /sftp/jit_consumer.c");
    terminal_history_set(w, 16, "jit bg /sftp/jit_producer.c");
    terminal_history_set(w, 17, "jit ps");
    terminal_history_set(w, 18, "ssh boot init");
    terminal_history_set(w, 19, "vim ~/.sshboot");
    terminal_history_set(w, 20, "gbemu lez.gb");
    w->hist_count = 21;
    w->hist_idx = -1;
}

static void terminal_apply_bashrc_line(struct Window *w, const char *line) {
    char buf[COLS];
    char *p;
    char *eq;
    char name[ENV_NAME_LEN];
    char value[ENV_VALUE_LEN];
    char path[64];
    int n;

    if (!w || !line) return;
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0' || *line == '#') return;
    n = 0;
    while (line[n] && n < (int)sizeof(buf) - 1) {
        buf[n] = line[n];
        n++;
    }
    buf[n] = '\0';
    p = buf;
    while (p > buf && (p[-1] == '\r' || p[-1] == '\n' || p[-1] == ' ' || p[-1] == '\t')) {
        *--p = '\0';
    }
    if (buf[0] == '\0' || buf[0] == '#') return;
    if (strncmp(buf, "export ", 7) == 0) {
        p = buf + 7;
    } else {
        p = buf;
    }
    eq = strchr(p, '=');
    if (!eq) {
        if (strncmp(buf, "alias", 5) == 0 && (buf[5] == '\0' || buf[5] == ' ')) {
            char *spec = buf + 5;
            char *alias_eq;
            while (*spec == ' ') spec++;
            if (*spec) {
                alias_eq = strchr(spec, '=');
                if (alias_eq) {
                    *alias_eq++ = '\0';
                    while (*alias_eq == ' ' || *alias_eq == '\t') alias_eq++;
                    
                    // Strip quotes if they exist
                    char *val = alias_eq;
                    int vlen = (int)strlen(val);
                    if (vlen >= 2 && ((val[0] == '\'' && val[vlen-1] == '\'') || (val[0] == '"' && val[vlen-1] == '"'))) {
                        val[vlen-1] = '\0';
                        val++;
                    }
                    if (*spec && val[0]) terminal_alias_set(w, spec, val);
                }
            }
        } else if (strncmp(buf, "unalias ", 8) == 0) {
            char *namep = buf + 8;
            while (*namep == ' ') namep++;
            if (*namep) terminal_alias_unset(w, namep);
        } else if (strncmp(buf, "source ", 7) == 0) {
            char *src = buf + 7;
            while (*src == ' ') src++;
            if (*src) terminal_source_script(w, src);
        } else if (strcmp(buf, ".") == 0) {
            /* no-op for bare dot */
        } else if (strncmp(buf, ". ", 2) == 0) {
            char *src = buf + 2;
            while (*src == ' ') src++;
            if (*src) terminal_source_script(w, src);
        } else if (strncmp(buf, "cd ", 3) == 0) {
            char *dir = buf + 3;
            while (*dir == ' ') dir++;
            if (*dir == '\0' || strcmp(dir, "/root") == 0 || strcmp(dir, "/") == 0) {
                w->cwd_bno = 1;
                lib_strcpy(w->cwd, "/root");
                terminal_env_sync_pwd(w);
            } else {
                char expanded[64];
                terminal_expand_home_path(w, dir, expanded, sizeof(expanded));
                if (expanded[0]) {
                    uint32_t parent_bno = 0;
                    char parent_cwd[32];
                    char leaf[20];
                    extern struct Window fs_tmp_window;
                    struct Window *tmp = &fs_tmp_window;
                    struct dir_block *db;
                    int idx;
                    if (resolve_editor_target(w, expanded, &parent_bno, parent_cwd, leaf) == 0) {
                        if (leaf[0] == '\0' || strcmp(leaf, ".") == 0) {
                            w->cwd_bno = parent_bno ? parent_bno : 1;
                            lib_strcpy(w->cwd, parent_cwd[0] ? parent_cwd : "/root");
                            terminal_env_sync_pwd(w);
                            goto terminal_cd_done;
                        }
                        memset(tmp, 0, sizeof(*tmp));
                        tmp->cwd_bno = parent_bno ? parent_bno : 1;
                        lib_strcpy(tmp->cwd, parent_cwd[0] ? parent_cwd : "/root");
                        db = load_current_dir(tmp);
                        if (db) {
                            idx = find_entry_index(db, leaf, 1);
                            if (idx >= 0) {
                                w->cwd_bno = db->entries[idx].bno;
                                lib_strcpy(w->cwd, tmp->cwd[0] ? tmp->cwd : "/root");
                                path_set_child(w->cwd, db->entries[idx].name);
                                terminal_env_sync_pwd(w);
                            }
                        }
                    }
                }
            }
        }
terminal_cd_done:
        if (strncmp(buf, "unset ", 6) == 0) {
            char *namep = buf + 6;
            while (*namep == ' ') namep++;
            if (*namep) terminal_env_unset(w, namep);
        }
        return;
    }
    n = 0;
    while (p < eq && *p == ' ') p++;
    while (p < eq && n < ENV_NAME_LEN - 1) {
        char c = *p++;
        if (c == ' ' || c == '\t') break;
        name[n++] = c;
    }
    name[n] = '\0';
    n = 0;
    eq++;
    while (*eq == ' ') eq++;
    while (eq[n] && n < ENV_VALUE_LEN - 1) {
        value[n] = eq[n];
        n++;
    }
    value[n] = '\0';
    if (name[0]) terminal_env_set(w, name, value);
}

void terminal_source_script(struct Window *w, const char *path) {
    unsigned char *buf = 0;
    uint32_t size = 0;
    char expanded[64];
    if (!w || !path || !path[0]) return;
    if (terminal_source_depth >= 4) {
        lib_printf("[bashrc] skip recursive source '%s'\n", path);
        return;
    }
    terminal_source_depth++;
    terminal_expand_home_path(w, path, expanded, sizeof(expanded));
    if (load_file_bytes_alloc(w, expanded, &buf, &size) != 0 || !buf) {
        if (buf) free(buf);
        terminal_source_depth--;
        return;
    }
    {
        uint32_t i = 0;
        uint32_t start = 0;
        while (i <= size) {
            if (i == size || buf[i] == '\n') {
                char line[COLS];
                uint32_t len = i - start;
                if (len >= sizeof(line)) len = sizeof(line) - 1;
                memcpy(line, buf + start, len);
                line[len] = '\0';
                terminal_apply_bashrc_line(w, line);
                start = i + 1;
            }
            i++;
        }
    }
    free(buf);
    terminal_source_depth--;
}

static unsigned char sshboot_hex_val(char c) {
    if (c >= '0' && c <= '9') return (unsigned char)(c - '0');
    if (c >= 'a' && c <= 'f') return (unsigned char)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (unsigned char)(c - 'A' + 10);
    return 255;
}

static unsigned char sshboot_keystream_next(uint32_t *state) {
    *state ^= (*state << 13);
    *state ^= (*state >> 17);
    *state ^= (*state << 5);
    return (unsigned char)((*state >> 24) & 0xff);
}

int sshboot_password_obfuscate(const char *password, char *out, int out_max) {
    static const char hex[] = "0123456789abcdef";
    uint32_t state = 0x6d2b79f5u;
    int len;
    int pos = 3;
    if (!password || !out || out_max < 8) return -1;
    len = (int)strlen(password);
    if (len <= 0 || pos + len * 2 + 1 > out_max) return -1;
    out[0] = 'v';
    out[1] = '1';
    out[2] = ':';
    for (int i = 0; i < len; i++) {
        unsigned char b = (unsigned char)password[i] ^ sshboot_keystream_next(&state);
        out[pos++] = hex[(b >> 4) & 15];
        out[pos++] = hex[b & 15];
    }
    out[pos] = '\0';
    return 0;
}

static int sshboot_password_deobfuscate(const char *encoded, char *out, int out_max) {
    uint32_t state = 0x6d2b79f5u;
    int len;
    int pos = 0;
    if (!encoded || !out || out_max <= 0) return -1;
    if (strncmp(encoded, "v1:", 3) == 0) encoded += 3;
    len = (int)strlen(encoded);
    if (len <= 0 || (len & 1) != 0 || len / 2 >= out_max) return -1;
    for (int i = 0; i < len; i += 2) {
        unsigned char hi = sshboot_hex_val(encoded[i]);
        unsigned char lo = sshboot_hex_val(encoded[i + 1]);
        if (hi > 15 || lo > 15) return -1;
        out[pos++] = (char)(((hi << 4) | lo) ^ sshboot_keystream_next(&state));
    }
    out[pos] = '\0';
    return 0;
}

static void terminal_apply_sshboot_line(struct Window *w, const char *line) {
    char buf[COLS];
    char tmp[OUT_BUF_SIZE];
    char *p;
    char *eq;
    int n = 0;

    (void)w;
    if (!line) return;
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0' || *line == '#') return;
    while (line[n] && n < (int)sizeof(buf) - 1) {
        buf[n] = line[n];
        n++;
    }
    buf[n] = '\0';
    while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == ' ' || buf[n - 1] == '\t')) {
        buf[--n] = '\0';
    }
    if (buf[0] == '\0') return;

    tmp[0] = '\0';
    if (strncmp(buf, "ssh set ", 8) == 0) {
        p = buf + 8;
        while (*p == ' ') p++;
        ssh_client_set_target(p, tmp, sizeof(tmp));
        return;
    }
    if (strncmp(buf, "ssh auth ", 9) == 0) {
        p = buf + 9;
        while (*p == ' ') p++;
        ssh_client_set_password(p, tmp, sizeof(tmp));
        return;
    }
    if (strncmp(buf, "sftp mount ", 11) == 0) {
        p = buf + 11;
        while (*p == ' ') p++;
        ssh_client_sftp_mount(*p ? p : ".", tmp, sizeof(tmp));
        return;
    }
    if (strncmp(buf, "wrp set ", 8) == 0) {
        p = buf + 8;
        while (*p == ' ') p++;
        ssh_client_set_wrp_url(p, tmp, sizeof(tmp));
        return;
    }

    eq = strchr(buf, '=');
    if (!eq) return;
    *eq++ = '\0';
    p = eq;
    while (*p == ' ' || *p == '\t') p++;
    if (strcmp(buf, "target") == 0 || strcmp(buf, "ssh") == 0) {
        ssh_client_set_target(p, tmp, sizeof(tmp));
    } else if (strcmp(buf, "password") == 0 || strcmp(buf, "auth") == 0) {
        ssh_client_set_password(p, tmp, sizeof(tmp));
    } else if (strcmp(buf, "password_obf") == 0 || strcmp(buf, "auth_obf") == 0) {
        char plain[64];
        if (sshboot_password_deobfuscate(p, plain, sizeof(plain)) == 0) {
            ssh_client_set_password(plain, tmp, sizeof(tmp));
            memset(plain, 0, sizeof(plain));
        }
    } else if (strcmp(buf, "sftp") == 0 || strcmp(buf, "sftp_root") == 0) {
        ssh_client_sftp_mount(*p ? p : ".", tmp, sizeof(tmp));
    } else if (strcmp(buf, "wrp") == 0) {
        ssh_client_set_wrp_url(p, tmp, sizeof(tmp));
    }
}

void terminal_load_sshboot(struct Window *w) {
    unsigned char *buf = 0;
    uint32_t size = 0;
    if (!w || w->kind != WINDOW_KIND_TERMINAL) return;
    if (load_file_bytes_alloc(w, "~/.sshboot", &buf, &size) != 0 || !buf) {
        const char *example =
            "# optional ssh auto-login config\n"
            "# copy this file to ~/.sshboot and edit the password locally\n"
            "# or run: ssh boot seal <password>\n"
            "# target=root@192.168.123.100:2221\n"
            "# password=your-password\n"
            "# password_obf=v1:...\n"
            "# sftp=/root/trade_new/os/mini-riscv-os/08-BlockDeviceDriver/http_test\n";
        store_file_bytes(w, "~/.sshboot.example", (const unsigned char *)example, (uint32_t)strlen(example));
        if (buf) free(buf);
        return;
    }
    {
        uint32_t i = 0;
        uint32_t start = 0;
        while (i <= size) {
            if (i == size || buf[i] == '\n') {
                char line[COLS];
                uint32_t len = i - start;
                if (len >= sizeof(line)) len = sizeof(line) - 1;
                memcpy(line, buf + start, len);
                line[len] = '\0';
                terminal_apply_sshboot_line(w, line);
                start = i + 1;
            }
            i++;
        }
    }
    free(buf);
}

void terminal_load_bashrc(struct Window *w) {
    unsigned char *probe_buf = 0;
    uint32_t probe_size = 0;
    if (!w || w->kind != WINDOW_KIND_TERMINAL) return;
    
    // Always use home-relative path for consistency
    if (load_file_bytes_alloc(w, "~/.bashrc", &probe_buf, &probe_size) == 0 && probe_buf) {
        free(probe_buf);
    } else {
        const char *def =
            "# matrix shell defaults\n"
            "alias ll='ls -all'\n"
            "alias l='ls'\n"
            "alias c='clear'\n"
            "alias n='netsurf'\n"
            "alias wrpopen='wrp open'\n"
            "alias ..='cd ..'\n";
        store_file_bytes(w, "~/.bashrc", (const unsigned char *)def, (uint32_t)strlen(def));
    }
    terminal_source_script(w, "~/.bashrc");
    terminal_load_sshboot(w);
}

static int terminal_env_find(struct Window *w, const char *name) {
    if (!w || !name || !name[0]) return -1;
    for (int i = 0; i < w->env_count; i++) {
        if (strcmp(w->env_names[i], name) == 0) return i;
    }
    return -1;
}

static int terminal_env_name_ok(const char *name) {
    if (!name || !name[0]) return 0;
    if (!((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z') || name[0] == '_')) return 0;
    for (int i = 1; name[i]; i++) {
        char c = name[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return 0;
    }
    return 1;
}

static void terminal_env_copy_value(char *dst, const char *src) {
    int i = 0;
    if (!dst) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] && i < ENV_VALUE_LEN - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int terminal_alias_find(struct Window *w, const char *name) {
    if (!w || !name || !name[0]) return -1;
    for (int i = 0; i < w->alias_count; i++) {
        if (strcmp(w->alias_names[i], name) == 0) return i;
    }
    return -1;
}

static int terminal_alias_name_ok(const char *name) {
    return terminal_env_name_ok(name);
}

static void terminal_alias_copy_value(char *dst, const char *src) {
    terminal_env_copy_value(dst, src);
}

static void terminal_alias_reset(struct Window *w) {
    if (!w) return;
    w->alias_count = 0;
    for (int i = 0; i < MAX_ALIAS_VARS; i++) {
        w->alias_names[i][0] = '\0';
        w->alias_values[i][0] = '\0';
    }
}

int terminal_alias_set(struct Window *w, const char *name, const char *value) {
    int idx;
    if (!w || !terminal_alias_name_ok(name)) return -1;
    idx = terminal_alias_find(w, name);
    if (idx < 0) {
        if (w->alias_count >= MAX_ALIAS_VARS) return -1;
        idx = w->alias_count++;
    }
    copy_name20(w->alias_names[idx], name);
    terminal_alias_copy_value(w->alias_values[idx], value ? value : "");
    return 0;
}

int terminal_alias_unset(struct Window *w, const char *name) {
    int idx = terminal_alias_find(w, name);
    if (idx < 0) return -1;
    for (int i = idx; i < w->alias_count - 1; i++) {
        copy_name20(w->alias_names[i], w->alias_names[i + 1]);
        terminal_alias_copy_value(w->alias_values[i], w->alias_values[i + 1]);
    }
    if (w->alias_count > 0) {
        w->alias_count--;
        w->alias_names[w->alias_count][0] = '\0';
        w->alias_values[w->alias_count][0] = '\0';
    }
    return 0;
}

const char *terminal_alias_get(struct Window *w, const char *name) {
    int idx = terminal_alias_find(w, name);
    if (idx < 0) return "";
    return w->alias_values[idx];
}

void terminal_expand_home_path(struct Window *w, const char *src, char *dst, int dst_size) {
    const char *home;
    int pos = 0;
    if (!dst || dst_size <= 0) return;
    dst[0] = '\0';
    if (!src) return;
    home = terminal_env_get(w, "HOME");
    if (!home || !home[0]) home = "/root";
    if (src[0] != '~') {
        int i = 0;
        while (src[i] && i < dst_size - 1) {
            dst[i] = src[i];
            i++;
        }
        dst[i] = '\0';
        return;
    }
    if (src[1] != '\0' && src[1] != '/') {
        int i = 0;
        while (src[i] && i < dst_size - 1) {
            dst[i] = src[i];
            i++;
        }
        dst[i] = '\0';
        return;
    }
    while (home[pos] && pos < dst_size - 1) {
        dst[pos] = home[pos];
        pos++;
    }
    src++;
    if (*src == '/') src++;
    if (pos > 0 && pos < dst_size - 1 && dst[pos - 1] == '/' && *src == '/') src++;
    if (pos > 0 && pos < dst_size - 1 && dst[pos - 1] != '/') {
        dst[pos++] = '/';
    }
    while (*src && pos < dst_size - 1) {
        dst[pos++] = *src++;
    }
    dst[pos] = '\0';
}

void terminal_expand_alias_command(struct Window *w, const char *src, char *dst, int dst_size) {
    char token[ENV_NAME_LEN];
    int i = 0;
    int token_len = 0;
    const char *alias;
    if (!dst || dst_size <= 0) return;
    dst[0] = '\0';
    if (!src) return;
    while (src[i] == ' ' || src[i] == '\t') i++;
    while (src[i] && src[i] != ' ' && src[i] != '\t' && token_len < ENV_NAME_LEN - 1) {
        token[token_len++] = src[i++];
    }
    token[token_len] = '\0';
    alias = terminal_alias_get(w, token);
    if (!alias || !alias[0]) {
        int pos = 0;
        while (src[pos] && pos < dst_size - 1) {
            dst[pos] = src[pos];
            pos++;
        }
        dst[pos] = '\0';
        return;
    }
    int pos = 0;
    for (int j = 0; alias[j] && pos < dst_size - 1; j++) dst[pos++] = alias[j];
    while (src[i] == ' ' || src[i] == '\t') i++;
    if (src[i] && pos < dst_size - 1) dst[pos++] = ' ';
    while (src[i] && pos < dst_size - 1) dst[pos++] = src[i++];
    dst[pos] = '\0';
}

void terminal_env_reset(struct Window *w) {
    if (!w) return;
    w->env_count = 0;
    for (int i = 0; i < MAX_ENV_VARS; i++) {
        w->env_names[i][0] = '\0';
        w->env_values[i][0] = '\0';
    }
}

int terminal_env_set(struct Window *w, const char *name, const char *value) {
    int idx;
    if (!w || !terminal_env_name_ok(name)) return -1;
    idx = terminal_env_find(w, name);
    if (idx < 0) {
        if (w->env_count >= MAX_ENV_VARS) return -1;
        idx = w->env_count++;
    }
    copy_name20(w->env_names[idx], name);
    terminal_env_copy_value(w->env_values[idx], value ? value : "");
    return 0;
}

int terminal_env_unset(struct Window *w, const char *name) {
    int idx = terminal_env_find(w, name);
    if (idx < 0) return -1;
    for (int i = idx; i < w->env_count - 1; i++) {
        copy_name20(w->env_names[i], w->env_names[i + 1]);
        terminal_env_copy_value(w->env_values[i], w->env_values[i + 1]);
    }
    if (w->env_count > 0) {
        w->env_count--;
        w->env_names[w->env_count][0] = '\0';
        w->env_values[w->env_count][0] = '\0';
    }
    return 0;
}

const char *terminal_env_get(struct Window *w, const char *name) {
    int idx = terminal_env_find(w, name);
    if (idx < 0) return "";
    return w->env_values[idx];
}

void terminal_env_bootstrap(struct Window *w) {
    if (!w || w->kind != WINDOW_KIND_TERMINAL) return;
    terminal_env_reset(w);
    terminal_alias_reset(w);
    terminal_env_set(w, "HOME", "/root");
    terminal_env_set(w, "PWD", w->cwd[0] ? w->cwd : "/root");
    terminal_env_set(w, "TERM", "matrix");
    terminal_env_set(w, "SHELL", "matrix");
    terminal_env_set(w, "USER", "root");
    terminal_env_set(w, "PATH", "/root/bin:/bin:/usr/bin");
    terminal_alias_set(w, "ll", "ls -all");
    terminal_alias_set(w, "l", "ls");
    terminal_alias_set(w, "c", "clear");
    terminal_alias_set(w, "n", "netsurf");
    terminal_alias_set(w, "wrpopen", "wrp open");
}

void terminal_env_sync_pwd(struct Window *w) {
    if (!w || w->kind != WINDOW_KIND_TERMINAL) return;
    terminal_env_set(w, "PWD", w->cwd[0] ? w->cwd : "/root");
}

void terminal_env_expand_command(struct Window *w, const char *src, char *dst, int dst_size) {
    int pos = 0;
    if (!dst || dst_size <= 0) return;
    dst[0] = '\0';
    if (!src) return;
    while (*src && pos < dst_size - 1) {
        if (*src != '$') {
            dst[pos++] = *src++;
            continue;
        }
        src++;
        if (*src == '$') {
            dst[pos++] = '$';
            src++;
            continue;
        }
        char name[ENV_NAME_LEN];
        int n = 0;
        if (*src == '{') {
            src++;
            while (*src && *src != '}' && n < ENV_NAME_LEN - 1) {
                name[n++] = *src++;
            }
            if (*src == '}') src++;
        } else if ((*src >= 'A' && *src <= 'Z') || (*src >= 'a' && *src <= 'z') || *src == '_') {
            while ((*src >= 'A' && *src <= 'Z') || (*src >= 'a' && *src <= 'z') ||
                   (*src >= '0' && *src <= '9') || *src == '_') {
                if (n < ENV_NAME_LEN - 1) name[n++] = *src;
                src++;
            }
        } else {
            dst[pos++] = '$';
            continue;
        }
        name[n] = '\0';
        const char *value = terminal_env_get(w, name);
        if (!value) value = "";
        for (int i = 0; value[i] && pos < dst_size - 1; i++) {
            dst[pos++] = value[i];
        }
    }
    dst[pos] = '\0';
}

void set_shell_status(struct Window *w, const char *msg) {
    if (!w) return;
    if (!msg || !msg[0]) {
        w->shell_status[0] = '\0';
        return;
    }
    lib_strcpy(w->shell_status, msg);
}

int terminal_font_scale(struct Window *w) {
    if (!w) return 1;
    if (w->term_font_scale < TERM_FONT_SCALE_MIN) return TERM_FONT_SCALE_MIN;
    if (w->term_font_scale > TERM_FONT_SCALE_MAX) return TERM_FONT_SCALE_MAX;
    return w->term_font_scale;
}

int terminal_char_w(struct Window *w) {
    return 8 * terminal_font_scale(w);
}

int terminal_char_h(struct Window *w) {
    return 16 * terminal_font_scale(w);
}

int terminal_line_h(struct Window *w) {
    return terminal_char_h(w) + 2;
}

int window_input_empty(struct Window *w) {
    return w->input_head == w->input_tail;
}

void window_input_push(struct Window *w, char key) {
    int next = (w->input_tail + 1) % INPUT_MAILBOX_SIZE;
    if (next == w->input_head) {
        w->input_head = (w->input_head + 1) % INPUT_MAILBOX_SIZE;
    }
    w->input_q[w->input_tail] = key;
    w->input_tail = next;
}

int window_input_pop(struct Window *w, char *key) {
    if (window_input_empty(w)) return 0;
    *key = w->input_q[w->input_head];
    w->input_head = (w->input_head + 1) % INPUT_MAILBOX_SIZE;
    return 1;
}

void wake_terminal_worker_for_window(int win_idx) {
    if (win_idx < 0) return;
    int worker = win_idx % TERMINAL_WORKERS;
    if (worker >= 0 && worker < TERMINAL_WORKERS && terminal_worker_task_ids[worker] >= 0) {
        task_wake(terminal_worker_task_ids[worker]);
    }
}

void broadcast_ctrl_c(void) {
    if (active_win_idx < 0 || active_win_idx >= MAX_WINDOWS) return;
    struct Window *w = &wins[active_win_idx];
    if (!w->active || w->kind != WINDOW_KIND_TERMINAL) return;
    if (w->submit_locked || w->executing_cmd || w->waiting_wget || os_jit_owner_active(w->id)) {
        CTRLDBG_PRINTF("[CTRLDBG] broadcast win=%d exec=%d wget=%d jit=%d\n",
                       w->id, w->executing_cmd, w->waiting_wget,
                       os_jit_owner_active(w->id));
        w->cancel_requested = 1;
        int killed = os_jit_cancel_by_owner(w->id);
        CTRLDBG_PRINTF("[CTRLDBG] broadcast win=%d killed=%d\n", w->id, killed);
        if (killed > 0) {
            terminal_finish_ctrl_c_cancel(w, killed);
        }
    }
}

void append_terminal_line(struct Window *w, const char *text) {
    if (!w || !text || w->kind != WINDOW_KIND_TERMINAL) return;
    const char *p = text;
    char line_buf[COLS];
    int col = 0;
    while (*p) {
        if (*p == '\n') {
            line_buf[col] = '\0';
            if (w->total_rows >= ROWS) {
                for (int i = 1; i < ROWS; i++) lib_strcpy(w->lines[i - 1], w->lines[i]);
                w->total_rows = ROWS - 1;
                if (w->v_offset > 0) w->v_offset--;
            }
            lib_strcpy(w->lines[w->total_rows], line_buf);
            w->total_rows++;
            int rv = terminal_visible_rows(w);
            if (w->total_rows > rv) w->v_offset = w->total_rows - rv;
            col = 0; p++; continue;
        }
        if (col < COLS - 1) line_buf[col++] = *p;
        p++;
    }
    if (col > 0) {
        line_buf[col] = '\0';
        if (w->total_rows >= ROWS) {
            for (int i = 1; i < ROWS; i++) lib_strcpy(w->lines[i - 1], w->lines[i]);
            w->total_rows = ROWS - 1;
            if (w->v_offset > 0) w->v_offset--;
        }
        lib_strcpy(w->lines[w->total_rows], line_buf);
        w->total_rows++;
        int rv = terminal_visible_rows(w);
        if (w->total_rows > rv) w->v_offset = w->total_rows - rv;
    }
    request_gui_redraw();
}

void terminal_app_stdout_flush(struct Window *w) {
    if (!w || w->kind != WINDOW_KIND_TERMINAL) return;
    if (w->app_stdout_len <= 0) return;
    w->app_stdout[w->app_stdout_len] = '\0';
    append_terminal_line(w, w->app_stdout);
    w->app_stdout_len = 0;
    w->app_stdout[0] = '\0';
}

void terminal_app_stdout_flush_win(int win_id) {
    if (win_id < 0 || win_id >= MAX_WINDOWS) return;
    terminal_app_stdout_flush(&wins[win_id]);
}

void terminal_app_stdout_putc(int win_id, char ch) {
    if (win_id < 0 || win_id >= MAX_WINDOWS) return;
    struct Window *w = &wins[win_id];
    if (!w->active || w->kind != WINDOW_KIND_TERMINAL) return;
    if (ch == '\r') return;
    if (ch == '\n') {
        terminal_app_stdout_flush(w);
        return;
    }
    if (w->app_stdout_len >= (int)sizeof(w->app_stdout) - 1) {
        terminal_app_stdout_flush(w);
    }
    if (w->app_stdout_len < (int)sizeof(w->app_stdout) - 1) {
        w->app_stdout[w->app_stdout_len++] = ch;
        w->app_stdout[w->app_stdout_len] = '\0';
    }
}

void terminal_app_stdout_puts(int win_id, const char *s) {
    if (win_id < 0 || win_id >= MAX_WINDOWS || !s) return;
    while (*s) {
        terminal_app_stdout_putc(win_id, *s++);
    }
    if (win_id >= 0 && win_id < MAX_WINDOWS) {
        terminal_app_stdout_flush(&wins[win_id]);
    }
}

int terminal_visible_rows(struct Window *w) {
    int wh = w->maximized ? DESKTOP_H : w->h;
    int rv = (wh - 50) / terminal_line_h(w);
    if (rv < 1) rv = 1;
    return rv;
}

int terminal_visible_cols(struct Window *w) {
    int ww = w->maximized ? WIDTH : w->w;
    int has_scroll = (w->total_rows > terminal_visible_rows(w));
    int usable_w = ww - 20 - (has_scroll ? 12 : 0);
    int cols = usable_w / terminal_char_w(w);
    if (cols < 1) cols = 1;
    if (cols > COLS - 1) cols = COLS - 1;
    return cols;
}

void terminal_clamp_v_offset(struct Window *w) {
    int max_off = w->total_rows - terminal_visible_rows(w);
    if (max_off < 0) max_off = 0;
    if (w->v_offset < 0) w->v_offset = 0;
    if (w->v_offset > max_off) w->v_offset = max_off;
}

int terminal_scroll_max(struct Window *w) {
    int max_off = w->total_rows - terminal_visible_rows(w);
    if (max_off < 0) max_off = 0;
    return max_off;
}

void terminal_append_prompt(struct Window *w) {
    if (!w) return;
    if (w->total_rows >= ROWS) {
        for (int i = 1; i < ROWS; i++) {
            lib_strcpy(w->lines[i - 1], w->lines[i]);
        }
        w->total_rows = ROWS - 1;
        if (w->v_offset > 0) w->v_offset--;
    }
    if (w->total_rows < ROWS) {
        lib_strcpy(w->lines[w->total_rows], PROMPT);
        w->total_rows++;
        w->cur_col = PROMPT_LEN;
    }
    request_gui_redraw();
}

int terminal_is_at_bottom(struct Window *w) {
    return w->v_offset >= terminal_scroll_max(w);
}

void terminal_scroll_to_bottom(struct Window *w) {
    if (!w || w->kind != WINDOW_KIND_TERMINAL) return;
    w->v_offset = terminal_scroll_max(w);
}

int line_text_len(const char *s) {
    int n = 0;
    while (n < COLS - 1 && s[n]) n++;
    return n;
}

void terminal_clear_selection(struct Window *w) {
    if (!w) return;
    w->selecting = 0;
    w->has_selection = 0;
    w->sel_start_row = w->sel_start_col = 0;
    w->sel_end_row = w->sel_end_col = 0;
}

void terminal_selection_normalized(struct Window *w, int *sr, int *sc, int *er, int *ec) {
    *sr = w->sel_start_row;
    *sc = w->sel_start_col;
    *er = w->sel_end_row;
    *ec = w->sel_end_col;
    if (*sr > *er || (*sr == *er && *sc > *ec)) {
        int tr = *sr, tc = *sc;
        *sr = *er; *sc = *ec;
        *er = tr; *ec = tc;
    }
}

int terminal_has_nonempty_selection(struct Window *w) {
    int sr, sc, er, ec;
    if (!w || !w->has_selection) return 0;
    terminal_selection_normalized(w, &sr, &sc, &er, &ec);
    return (sr != er) || (sc != ec);
}

int terminal_mouse_to_cell(struct Window *w, int mx, int my, int *row, int *col) {
    if (!w || w->kind != WINDOW_KIND_TERMINAL) return 0;
    int x = w->maximized ? 0 : w->x;
    int y = w->maximized ? 0 : w->y;
    int ww = w->maximized ? WIDTH : w->w;
    int wh = w->maximized ? DESKTOP_H : w->h;
    int left = x + 10;
    int top = y + 40;
    int inner_h = wh - 50;
    if (mx < left || mx >= x + ww - 15 || my < top || my >= top + inner_h) return 0;
    int r = (my - top) / terminal_line_h(w);
    int c = (mx - left) / terminal_char_w(w);
    if (r < 0) r = 0;
    if (r >= terminal_visible_rows(w)) r = terminal_visible_rows(w) - 1;
    if (c < 0) c = 0;
    if (c > terminal_visible_cols(w)) c = terminal_visible_cols(w);
    r += w->v_offset;
    if (r < 0) r = 0;
    if (r >= w->total_rows) r = w->total_rows - 1;
    *row = r;
    *col = c;
    return 1;
}

void terminal_copy_selection(struct Window *w) {
    if (!w || !w->has_selection) return;
    if (!terminal_has_nonempty_selection(w)) return;
    int sr, sc, er, ec;
    int pos = 0;
    terminal_selection_normalized(w, &sr, &sc, &er, &ec);
    terminal_clipboard[0] = '\0';
    for (int r = sr; r <= er && pos < (int)sizeof(terminal_clipboard) - 1; r++) {
        int len = line_text_len(w->lines[r]);
        int c0 = (r == sr) ? sc : 0;
        int c1 = (r == er) ? ec : len;
        if (c0 < 0) c0 = 0;
        if (c0 > len) c0 = len;
        if (c1 < c0) c1 = c0;
        if (c1 > len) c1 = len;
        for (int c = c0; c < c1 && pos < (int)sizeof(terminal_clipboard) - 1; c++) {
            terminal_clipboard[pos++] = w->lines[r][c];
        }
        if (r != er && pos < (int)sizeof(terminal_clipboard) - 1) terminal_clipboard[pos++] = '\n';
    }
    terminal_clipboard[pos] = '\0';
}

void terminal_paste_clipboard(struct Window *w) {
    if (!w || !terminal_clipboard[0]) return;
    if (w->kind != WINDOW_KIND_TERMINAL && w->kind != WINDOW_KIND_EDITOR) return;
    for (int i = 0; terminal_clipboard[i]; i++) {
        char ch = terminal_clipboard[i];
        if (ch == '\n') ch = 10;
        window_input_push(w, ch);
    }
    w->mailbox = 1;
    wake_terminal_worker_for_window(w->id);
}

const char *terminal_clipboard_text(void) {
    return terminal_clipboard;
}

void resize_terminal_font(struct Window *w, int new_scale) {
    if (!w || w->kind != WINDOW_KIND_TERMINAL) return;
    if (new_scale < TERM_FONT_SCALE_MIN) new_scale = TERM_FONT_SCALE_MIN;
    if (new_scale > TERM_FONT_SCALE_MAX) new_scale = TERM_FONT_SCALE_MAX;
    if (new_scale == w->term_font_scale) return;
    w->term_font_scale = new_scale;
    terminal_clamp_v_offset(w);
}

void run_command(struct Window *w) {
    char *full_cmd = w->cmd_buf;
    w->shell_status[0] = '\0';

    // 捲動邏輯：滿了就往上推，而不是直接清空
    if (w->total_rows >= ROWS) {
        for (int i = 1; i < ROWS; i++) lib_strcpy(w->lines[i-1], w->lines[i]);
        w->total_rows = ROWS - 1;
    }

    w->executing_cmd = 1;

    if (terminal_is_ssh_auth_request(full_cmd)) {
        w->executing_cmd = 0;
        w->submit_locked = 0;
        terminal_begin_ssh_auth(w);
        return;
    }
    if (terminal_is_ssh_boot_seal_request(full_cmd)) {
        w->executing_cmd = 0;
        w->submit_locked = 0;
        terminal_begin_ssh_boot_seal(w);
        return;
    }

    // 指令紀錄與清除處理
    if (w->edit_len > 0) {
        if (w->hist_count == 0 || strcmp(w->history[0], full_cmd) != 0) {
            for (int i = MAX_HIST - 1; i > 0; i--) terminal_history_set(w, i, w->history[i - 1]);
            terminal_history_set(w, 0, full_cmd);
            if(w->hist_count < MAX_HIST) w->hist_count++;
        }
    }
    if (strncmp(full_cmd, "clear", 5) == 0) {
        w->total_rows = 1; w->v_offset = 0; 
        lib_strcpy(w->lines[0], PROMPT); w->cur_col = PROMPT_LEN;
        w->executing_cmd = 0; w->submit_locked = 0;
        return;
    }

    if (w->cancel_requested) {
        append_terminal_line(w, "Command cancelled.");
        terminal_unlock_after_cancel(w);
        return;
    }

    // 執行指令
    exec_single_cmd(w, full_cmd);

    // 安全地將輸出印到螢幕 (嚴格邊界檢查)
    char *res = w->out_buf;
    int len = strlen(res);
    int pos = 0;
    int max_c = terminal_visible_cols(w);

    while(pos < len) {
        int t = 0;
        while(pos+t < len && res[pos+t] != '\n' && t < max_c) t++;
        
        if (w->total_rows >= ROWS) {
            // Shift lines up to scroll
            for (int i = 1; i < ROWS; i++) lib_strcpy(w->lines[i - 1], w->lines[i]);
            w->total_rows = ROWS - 1;
            if (w->v_offset > 0) w->v_offset--;
        }

        if (w->total_rows < ROWS) {
            for(int i=0; i<t; i++) w->lines[w->total_rows][i] = res[pos+i];
            w->lines[w->total_rows][t] = '\0';
            w->total_rows++;
        }
        pos += (pos + t < len && res[pos+t] == '\n') ? (t + 1) : t;
    }

    terminal_scroll_to_bottom(w);
    if (!app_running) { 
        w->executing_cmd = 0; 
        w->submit_locked = 0; 
        w->cancel_requested = 0;
    }
}

void handle_window_mailbox(struct Window *w) {
    char key;
    if (!window_input_pop(w, &key)) {
        w->mailbox = 0;
        return;
    }

    // SANITY CHECK: Fix the '1.6 billion' memory corruption bug
    if (w->edit_len < 0 || w->edit_len >= COLS) {
        lib_printf("[FIX] Detected corrupted edit_len=%d, resetting to 0\n", w->edit_len);
        w->edit_len = 0;
        w->cursor_pos = 0;
        w->cmd_buf[0] = '\0';
    }

    // DEBUG LOGGING
    if (w->kind == WINDOW_KIND_EDITOR) {
        editor_handle_key(w, key);
        return;
    }
    if (w->kind == WINDOW_KIND_IMAGE) {
        if (key == '+' || key == '=') resize_image_window(w, w->image_scale + 1);
        else if (key == '-') resize_image_window(w, w->image_scale - 1);
        return;
    }
    if (w->kind == WINDOW_KIND_TERMINAL && w->ssh_auth_mode) {
        if (key == 3 || key == 27) {
            terminal_cancel_ssh_auth(w);
            return;
        }
        if (key == 10 || key == '\r') {
            terminal_submit_ssh_auth(w);
            return;
        }
        if (key == 8 || key == 127) {
            if (w->ssh_auth_len > 0) {
                w->ssh_auth_len--;
                w->ssh_auth_buf[w->ssh_auth_len] = '\0';
                terminal_refresh_ssh_auth_prompt(w);
            }
            return;
        }
        if (key >= 32 && key < 127) {
            if (w->ssh_auth_len < (int)sizeof(w->ssh_auth_buf) - 1) {
                w->ssh_auth_buf[w->ssh_auth_len++] = key;
                w->ssh_auth_buf[w->ssh_auth_len] = '\0';
                terminal_refresh_ssh_auth_prompt(w);
            }
            return;
        }
        return;
    }
    if (key == 3) {
        CTRLDBG_PRINTF("[CTRLDBG] mailbox win=%d exec=%d wget=%d jit=%d\n",
                       w->id, w->executing_cmd, w->waiting_wget, os_jit_owner_active(w->id));
        if (w->submit_locked || w->executing_cmd || w->waiting_wget || os_jit_owner_active(w->id)) {
            char line[64];
            w->cancel_requested = 1;
            int killed = os_jit_cancel_by_owner(w->id);
            CTRLDBG_PRINTF("[CTRLDBG] mailbox win=%d killed=%d\n", w->id, killed);
            if (killed > 0) {
                snprintf(line, sizeof(line), "JIT: killed %d job(s).", killed);
                append_terminal_line(w, line);
                terminal_unlock_after_cancel(w);
            }
        } else {
            w->submit_locked = 0;
            w->cancel_requested = 0;
            clear_window_input_queue(w);
            clear_prompt_input(w);
            redraw_prompt_line(w, (w->total_rows > 0) ? (w->total_rows - 1) : 0);
        }
        return;
    }
    if (w->executing_cmd && !app_running) {
        w->executing_cmd = 0;
        w->cancel_requested = 0;
        if (key == 10) return;
    }
    if (key >= 32) {
        w->submit_locked = 0;
        if (w->shell_status[0]) {
            w->shell_status[0] = '\0';
            if (w->total_rows > 0) {
                redraw_prompt_line(w, w->total_rows - 1);
            }
        }
    }
    if (w->executing_cmd) {
        if (key == 3) w->cancel_requested = 1;
        return;
    }
    int r = (w->total_rows > 0) ? (w->total_rows - 1) : 0;
    if (r >= ROWS) r = ROWS - 1;

    if (key == 10) {
        if (w->waiting_wget) return;
        if (w->submit_locked) return;

        int q_len = (w->input_tail >= w->input_head) ? 
                    (w->input_tail - w->input_head) : 
                    (INPUT_MAILBOX_SIZE - w->input_head + w->input_tail);
        INPUTDBG_PRINTF("[DEBUG] Enter logic START, Q_len=%d, edit_len=%d\n", q_len, w->edit_len);

        int is_clear = (strncmp(w->cmd_buf, "clear", 5) == 0);
        int had_input = (w->edit_len > 0);
        
        if (had_input) {
            if (terminal_is_ssh_auth_request(w->cmd_buf)) {
                terminal_begin_ssh_auth(w);
                return;
            }
            if (terminal_is_ssh_boot_seal_request(w->cmd_buf)) {
                terminal_begin_ssh_boot_seal(w);
                return;
            }
            w->submit_locked = 1;
            run_command(w);
            CTRLDBG_PRINTF("[CTRLDBG] post-run win=%d exec=%d submit=%d cancel=%d rows=%d cur_col=%d out='%s'\n",
                           w->id, w->executing_cmd, w->submit_locked, w->cancel_requested,
                           w->total_rows, w->cur_col, w->out_buf);
        }
        
        if (w->waiting_wget) return;
        clear_prompt_input(w);
        if (!is_clear && !app_running) {
            if (w->total_rows >= ROWS) {
                // Shift lines up to scroll
                for (int i = 1; i < ROWS; i++) lib_strcpy(w->lines[i - 1], w->lines[i]);
                w->total_rows = ROWS - 1;
                if (w->v_offset > 0) w->v_offset--;
            }
            if (w->total_rows < ROWS) {
                w->total_rows++;
                lib_strcpy(w->lines[w->total_rows-1], PROMPT);
                w->cur_col = PROMPT_LEN;
            }
            CTRLDBG_PRINTF("[CTRLDBG] prompt-restored win=%d rows=%d cur_col=%d edit=%d submit=%d exec=%d\n",
                           w->id, w->total_rows, w->cur_col, w->edit_len,
                           w->submit_locked, w->executing_cmd);
        } else if (is_clear) {
            redraw_prompt_line(w, 0);
        }
        terminal_scroll_to_bottom(w);
    } else if (w->waiting_wget) {
        if (key == 3) w->cancel_requested = 1;
        return;
    } else if (key == 0x10) {
        w->submit_locked = 0;
        if (w->hist_idx < w->hist_count-1) {
            if (w->hist_idx == -1) {
                lib_strcpy(w->saved_cmd, w->cmd_buf);
                w->has_saved_cmd = 1;
            }
            w->hist_idx++;
            load_edit_buffer(w, w->history[w->hist_idx]);
            redraw_prompt_line(w, r);
        }
    } else if (key == 0x11) {
        w->submit_locked = 0;
        if (w->hist_idx > 0) {
            w->hist_idx--;
            load_edit_buffer(w, w->history[w->hist_idx]);
            redraw_prompt_line(w, r);
        } else if (w->hist_idx == 0) {
            w->hist_idx = -1;
            if (w->has_saved_cmd) load_edit_buffer(w, w->saved_cmd);
            else load_edit_buffer(w, "");
            w->has_saved_cmd = 0;
            redraw_prompt_line(w, r);
        }
    } else if (key == 0x12) {
        if (w->cursor_pos > 0) {
            w->cursor_pos--;
            redraw_prompt_line(w, r);
        }
    } else if (key == 0x13) {
        if (w->cursor_pos < w->edit_len) {
            w->cursor_pos++;
            redraw_prompt_line(w, r);
        }
    } else if (key == 0x15) {
        if (w->cursor_pos != 0) {
            w->cursor_pos = 0;
            redraw_prompt_line(w, r);
        }
    } else if (key == 0x16) {
        if (w->cursor_pos != w->edit_len) {
            w->cursor_pos = w->edit_len;
            redraw_prompt_line(w, r);
        }
    } else if (key == 0x14) {
        if (w->cursor_pos < w->edit_len) {
            for (int i = w->cursor_pos; i < w->edit_len; i++) {
                w->cmd_buf[i] = w->cmd_buf[i + 1];
            }
            w->edit_len--;
            redraw_prompt_line(w, r);
        }
    } else if (key == 8) {
        if (w->cursor_pos > 0 && w->edit_len > 0) {
            for (int i = w->cursor_pos - 1; i < w->edit_len; i++) {
                w->cmd_buf[i] = w->cmd_buf[i + 1];
            }
            w->edit_len--;
            w->cursor_pos--;
            redraw_prompt_line(w, r);
        }
    } else if (key == '\t') {
        try_tab_complete(w, r);
    } else if (key >= 32) {
        if (w->edit_len < COLS - PROMPT_LEN - 1) {
            INPUTDBG_PRINTF("[DEBUG] Storing char '%c'\n", key);
            for (int i = w->edit_len; i >= w->cursor_pos; i--) {
                w->cmd_buf[i + 1] = w->cmd_buf[i];
            }
            w->cmd_buf[w->cursor_pos] = key;
            w->edit_len++;
            w->cursor_pos++;
            redraw_prompt_line(w, r);
        }
    }

}

void terminal_worker_task(void) {
    static int worker_counter = 0;
    int worker_id = worker_counter++;
    while (1) {
        int did_work = 0;
        release_finished_app_commands();
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if ((i % TERMINAL_WORKERS) != worker_id) continue;
            struct Window *w = &wins[i];
            if (!w->active || window_input_empty(w)) continue;
            CTRLDBG_PRINTF("[CTRLDBG] worker=%d wake win=%d mailbox=%d exec=%d submit=%d q=%d\n",
                           worker_id, w->id, w->mailbox, w->executing_cmd, w->submit_locked,
                           (w->input_tail >= w->input_head) ? (w->input_tail - w->input_head) :
                           (INPUT_MAILBOX_SIZE - w->input_head + w->input_tail));
            do {
                handle_window_mailbox(w);
                did_work = 1;
            } while (!window_input_empty(w));
        }
        if (!did_work) task_sleep_current();
        else task_os();
    }
}
