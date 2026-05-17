#include "user_cmd.h"
#include "user_internal.h"
#include "jit_debugger.h"
#include "tcc_runtime.h"
#include "drivers/sd/sd_fat.h"

extern void ps_gem_probe(void);
extern void ps_gem_init(void);
extern void ps_gem_get_debug(uint32_t out[8]);
extern void ps_gem_poll(void);
extern int ps_gem_is_ready(void);

static void copy_range_str(char *dst, int dst_size, const char *begin, const char *end) {
    int i = 0;
    if (dst_size <= 0) return;
    while (begin < end && i < dst_size - 1) dst[i++] = *begin++;
    dst[i] = '\0';
}

static void derive_filename_from_path(const char *path, char *out, int out_size) {
    const char *name = path;
    const char *p = path;
    if (out_size <= 0) return;
    while (*p) {
        if (*p == '/') name = p + 1;
        else if (*p == '?' || *p == '#') break;
        p++;
    }
    if (*name == '\0') {
        lib_strcpy(out, "index.html");
        return;
    }
    copy_range_str(out, out_size, name, p);
}







void append_dir_entries_sorted(struct dir_block *db, const char *title, const char *name_prefix, int type_filter, char *out) {
    int printed = 0;
    out[0] = '\0';
    if (title && title[0]) lib_strcat(out, title);
    for (int pass = 0; pass < 2; pass++) {
        int want_type = (pass == 0) ? 1 : 0;
        for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
            if (!db->entries[i].name[0]) continue;
            if (type_filter != -1 && db->entries[i].type != type_filter) continue;
            if (db->entries[i].type != want_type) continue;
            if (name_prefix && name_prefix[0] && !str_starts_with(db->entries[i].name, name_prefix)) continue;
            char row[64];
            snprintf(row, sizeof(row), "  %c %s\n", (db->entries[i].type == 1 ? 'd' : 'f'), db->entries[i].name);
            if (strlen(out) + strlen(row) < OUT_BUF_SIZE - 2) {
                lib_strcat(out, row);
                printed = 1;
            } else {
                return;
            }
        }
    }
    if (!printed) lib_strcat(out, " (empty)");
}

static void append_virtual_mount_entries(int show, int all, char *out, int *printed) {
    char row[128];
    if (!show || !out) return;
    if (all) {
        row[0] = '\0';
        append_out_pad(row, sizeof(row), "drwx", 5); append_out_str(row, sizeof(row), " ");
        append_out_pad(row, sizeof(row), "virt", 8); append_out_str(row, sizeof(row), " ");
        append_out_pad(row, sizeof(row), "-", 19); append_out_str(row, sizeof(row), " ");
        append_out_str(row, sizeof(row), "sd");
    } else {
        lib_strcpy(row, "d sd");
    }
    append_out_str(row, sizeof(row), "\n");
    if (strlen(out) + strlen(row) < OUT_BUF_SIZE - 2) {
        lib_strcat(out, row);
        if (printed) *printed = 1;
    }

    if (all) {
        row[0] = '\0';
        append_out_pad(row, sizeof(row), "drwx", 5); append_out_str(row, sizeof(row), " ");
        append_out_pad(row, sizeof(row), "virt", 8); append_out_str(row, sizeof(row), " ");
        append_out_pad(row, sizeof(row), "-", 19); append_out_str(row, sizeof(row), " ");
        append_out_str(row, sizeof(row), "sftpd");
    } else {
        lib_strcpy(row, "d sftpd");
    }
    append_out_str(row, sizeof(row), "\n");
    if (strlen(out) + strlen(row) < OUT_BUF_SIZE - 2) {
        lib_strcat(out, row);
        if (printed) *printed = 1;
    }
}

static void append_single_entry_line(struct file_entry *e, char *out, int all) {
    if (!e || !out) return;
    out[0] = '\0';
    if (all) {
        char ms[5], sz[16], ts[20], name[32];
        mode_to_str(e->mode, e->type, ms);
        format_size_human(e->size, sz);
        format_hms(e->mtime, ts);
        copy_name20(name, e->name);
        char row[128];
        snprintf(row, sizeof(row), "%5s %8s %8s %s %s\n",
                 ms, sz, ts, (e->type == 1 ? "d" : "f"), name);
        lib_strcat(out, row);
    } else {
        char row[64];
        snprintf(row, sizeof(row), "  %c %s\n", (e->type == 1 ? 'd' : 'f'), e->name);
        lib_strcat(out, row);
    }
}









void append_hex32(char *dst, uint32_t v) {
    static const char hex[] = "0123456789abcdef";
    while (*dst) dst++;
    dst[0] = '0';
    dst[1] = 'x';
    for (int i = 0; i < 8; i++) {
        dst[2 + i] = hex[(v >> (28 - i * 4)) & 0xF];
    }
    dst[10] = '\0';
}

void append_hex8(char *dst, unsigned char v) {
    static const char hex[] = "0123456789abcdef";
    while (*dst) dst++;
    dst[0] = hex[(v >> 4) & 0xF];
    dst[1] = hex[v & 0xF];
    dst[2] = '\0';
}

void format_size_human(uint32_t bytes, char *out) {
    const char *unit = "B";
    uint32_t whole = bytes;
    uint32_t frac = 0;
    if (bytes >= 1024U * 1024U * 1024U) {
        unit = "GB";
        whole = bytes / (1024U * 1024U * 1024U);
        frac = (bytes % (1024U * 1024U * 1024U)) * 10U / (1024U * 1024U * 1024U);
    } else if (bytes >= 1024U * 1024U) {
        unit = "MB";
        whole = bytes / (1024U * 1024U);
        frac = (bytes % (1024U * 1024U)) * 10U / (1024U * 1024U);
    } else if (bytes >= 1024U) {
        unit = "KB";
        whole = bytes / 1024U;
        frac = (bytes % 1024U) * 10U / 1024U;
    }
    lib_itoa(whole, out);
    if (unit[0] != 'B') {
        while (*out) out++;
        out[0] = '.';
        out[1] = (char)('0' + frac);
        out[2] = '\0';
    }
    lib_strcat(out, unit);
}

int is_mostly_text(const unsigned char *buf, uint32_t size) {
    if (size == 0) return 1;
    uint32_t printable = 0;
    for (uint32_t i = 0; i < size; i++) {
        unsigned char c = buf[i];
        if ((c >= 32 && c <= 126) || c == '\n' || c == '\r' || c == '\t') printable++;
    }
    return printable * 100U >= size * 85U;
}

void render_hex_dump(char *out, const unsigned char *buf, uint32_t size) {
    out[0] = '\0';
    uint32_t limit = size;
    if (limit > 128U) limit = 128U;
    for (uint32_t i = 0; i < limit; i += 16U) {
        char off[12];
        off[0] = '\0';
        append_hex32(off, i);
        lib_strcat(out, off);
        lib_strcat(out, ": ");
        for (uint32_t j = 0; j < 16U && i + j < limit; j++) {
            append_hex8(out, buf[i + j]);
            lib_strcat(out, " ");
        }
        if (i + 16U < limit) lib_strcat(out, "\n");
    }
    if (size > limit) lib_strcat(out, "\n...");
}

static int launch_app_file(struct Window *w, const char *name, const char *argstr, char *out, int out_max) {
    unsigned char *buf = 0;
    uint32_t size = 0;
    if (app_running) {
        lib_strcpy(out, "ERR: App Busy (previous app still running).");
        return -9;
    }
    int rc = load_file_bytes_alloc(w, name, &buf, &size);
    uint8_t *app_base = (uint8_t *)(uintptr_t)APP_START;

    if (rc != 0) {
        if (buf) free(buf);
        if (rc == -2) lib_strcpy(out, "ERR: File Not Found.");
        else if (rc == -4) lib_strcpy(out, "ERR: No Memory.");
        else lib_strcpy(out, "ERR: Load Failed.");
        return rc;
    }

    loaded_app_entry = 0;
    appfs_set_cwd(w->cwd_bno, w->cwd);
    memset(app_base, 0, APP_SIZE);

    if (size < sizeof(struct elf32_ehdr) ||
        ((struct elf32_ehdr *)buf)->e_ident[0] != ELF32_MAG0 ||
        ((struct elf32_ehdr *)buf)->e_ident[1] != ELF32_MAG1 ||
        ((struct elf32_ehdr *)buf)->e_ident[2] != ELF32_MAG2 ||
        ((struct elf32_ehdr *)buf)->e_ident[3] != ELF32_MAG3) {
        free(buf);
        lib_strcpy(out, "ERR: ELF Only.");
        return -11;
    }

    if (app_load_elf_image(buf, size, out) != 0) {
        free(buf);
        return -11;
    }

    if (!loaded_app_entry) {
        free(buf);
        lib_strcpy(out, "ERR: Bad App Entry.");
        return -8;
    }

    if (APP_SIZE <= APP_STACK_SIZE + APP_HEAP_GUARD) {
        lib_strcpy(out, "ERR: App Mem Too Small.");
        return -9;
    }
    loaded_app_user_sp = ((reg_t)(uintptr_t)APP_END & ~(reg_t)0xF) - 16;
    if (loaded_app_user_sp <= (reg_t)(uintptr_t)APP_START + APP_HEAP_GUARD) {
        free(buf);
        lib_strcpy(out, "ERR: App Heap Too Small.");
        return -10;
    }

    if (app_prepare_argv_stack(name, argstr, &loaded_app_user_sp) != 0) {
        free(buf);
        lib_strcpy(out, "ERR: Bad Args.");
        return -12;
    }

    app_running = 1;
    app_owner_win_id = w->id;
    clear_window_input_queue(w);
    app_vm_reset();
    app_heap_reset((reg_t)(uintptr_t)APP_START + APP_HEAP_GUARD, loaded_app_user_sp - APP_HEAP_GUARD);
    if (app_task_id < 0) {
        app_task_id = task_create(&app_bootstrap, 0, 1);
    } else {
        task_reset(app_task_id, &app_bootstrap, 0, 1);
    }
    task_wake(app_task_id);
    free(buf);
    lib_strcpy(out, ">> App Started.");
    return 0;
}

static int try_launch_elf_command(struct Window *w, char *cmd, char *out) {
    char tmp[COLS];
    char *fn = tmp;
    char *args = 0;
    char *sp;
    size_t len;
    int i = 0;

    while (cmd[i] == ' ') i++;
    if (cmd[i] == '\0') return 0;
    while (cmd[i] && i < COLS - 1) {
        tmp[i] = cmd[i];
        i++;
    }
    tmp[i] = '\0';

    while (*fn == ' ') fn++;
    if (*fn == '\0') return 0;
    sp = fn;
    while (*sp && *sp != ' ') sp++;
    if (*sp == ' ') {
        *sp++ = '\0';
        while (*sp == ' ') sp++;
        if (*sp == '\0') sp = 0;
    } else {
        sp = 0;
    }
    len = strlen(fn);
    if (len < 4 || strcmp(fn + len - 4, ".elf") != 0) {
        return 0;
    }
    args = sp;
    if (launch_app_file(w, fn, args, out, OUT_BUF_SIZE) != 0) return 1;
    return 1;
}


static void recursive_find(struct Window *w, uint32_t bno, const char *path, const char *search, char *out) {
    if (bno == 0 || bno >= FS_MAX_BLOCKS) return;
    
    // Allocate a block buffer on the heap to avoid stack overflow
    struct blk *b = malloc(sizeof(struct blk));
    if (!b) return;
    
    memset(b, 0, sizeof(struct blk));
    b->blockno = bno;
    virtio_disk_rw(b, 0);
    struct dir_block *db = (struct dir_block *)b->data;
    if (db->magic != FS_MAGIC) {
        free(b);
        return;
    }
    
    // Copy necessary info to minimize stack usage during recursion
    struct file_entry entries[MAX_DIR_ENTRIES];
    memcpy(entries, db->entries, sizeof(entries));
    free(b); // Done with the disk block
    
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (entries[i].name[0] == '\0') continue;
        
        char full_path[256];
        lib_strcpy(full_path, path);
        if (full_path[strlen(full_path)-1] != '/') lib_strcat(full_path, "/");
        lib_strcat(full_path, entries[i].name);
        
        if (strstr(entries[i].name, search)) {
            char row[300];
            snprintf(row, sizeof(row), "%s %s\n", (entries[i].type == 1 ? "[DIR]" : "[FILE]"), full_path);
            if (strlen(out) + strlen(row) < OUT_BUF_SIZE - 2) {
                lib_strcat(out, row);
            }
        }
        
        if (entries[i].type == 1) {
            recursive_find(w, entries[i].bno, full_path, search, out);
        }
    }
}

static uint32_t calculate_dir_size(uint32_t bno, int curr_depth, int max_depth) {
    if (bno == 0 || bno >= FS_MAX_BLOCKS || curr_depth > max_depth) return 0;
    
    struct blk *b = malloc(sizeof(struct blk));
    if (!b) return 0;
    
    memset(b, 0, sizeof(struct blk));
    b->blockno = bno;
    virtio_disk_rw(b, 0);
    struct dir_block *db = (struct dir_block *)b->data;
    
    if (db->magic != FS_MAGIC) {
        free(b);
        return 0;
    }
    
    uint32_t total = 0;
    uint32_t child_bnos[MAX_DIR_ENTRIES];
    int count = 0;

    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (db->entries[i].name[0] == '\0') continue;
        if (db->entries[i].type == 1) { // Directory
            if (count < MAX_DIR_ENTRIES) {
                child_bnos[count++] = db->entries[i].bno;
            }
        } else if (db->entries[i].type == 0) { // File
            total += db->entries[i].size;
        }
    }

    free(b);
    
    // Only recurse if we haven't reached max depth
    if (curr_depth < max_depth) {
        for (int i = 0; i < count; i++) {
            total += calculate_dir_size(child_bnos[i], curr_depth + 1, max_depth);
        }
    }
    return total;
}

void list_dir_contents(uint32_t bno, char *out) {
    extern struct Window fs_tmp_window;
    struct Window *ctx = &fs_tmp_window;
    memset(ctx, 0, sizeof(*ctx));
    ctx->cwd_bno = bno;
    struct dir_block *db = load_current_dir(ctx);
    if (!db) { lib_strcpy(out, "ERR: Directory unreadable."); return; }

    append_dir_entries_sorted(db, "Directory contents:\n", 0, -1, out);
    if (bno == 1) {
        int printed = 0;
        if (strcmp(out, "Directory contents:\n (empty)") == 0) {
            lib_strcpy(out, "Directory contents:\n");
        }
        append_virtual_mount_entries(bno == 1, 0, out, &printed);
    }
}

static int check_dir_and_list(struct Window *w, const char *path, char *out) {
    if (!path || path[0] == '\0') return 0;
    char expanded[128]; uint32_t p_bno = 0; char p_cwd[128], leaf[20];
    terminal_expand_home_path(w, path, expanded, sizeof(expanded));
    if (resolve_editor_target(w, expanded, &p_bno, p_cwd, leaf) == 0) {
        uint32_t target_bno = 0;
        if (leaf[0] == '\0' || strcmp(leaf, ".") == 0) target_bno = p_bno;
        else {
            extern struct Window fs_tmp_window; struct Window *tmp = &fs_tmp_window;
            memset(tmp, 0, sizeof(*tmp)); tmp->cwd_bno = p_bno;
            struct dir_block *db = load_current_dir(tmp);
            int idx = find_entry_index(db, leaf, -1);
            if (idx >= 0 && db->entries[idx].type == 1) target_bno = db->entries[idx].bno;
        }
        if (target_bno != 0) { list_dir_contents(target_bno, out); return 1; }
    }
    return 0;
}

extern uint32_t TEXT_START, TEXT_END;
extern uint32_t RODATA_START, RODATA_END;
extern uint32_t DATA_START, DATA_END;
extern uint32_t BSS_START, BSS_END;
extern uint32_t HEAP_START, HEAP_SIZE;

static void out_append(char *out, const char *s) {
    int n = strlen(out);
    int i = 0;
    while (s && s[i] && n < OUT_BUF_SIZE - 1) {
        out[n++] = s[i++];
    }
    out[n] = '\0';
}

static int parse_hex_u32(const char *s, uint32_t *out, const char **endp) {
    uint32_t v = 0;
    int any = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        int d = -1;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        v = (v << 4) | (uint32_t)d;
        any = 1;
        s++;
    }
    if (!any) return -1;
    if (out) *out = v;
    if (endp) *endp = s;
    return 0;
}

static int mem_range_safe(uint32_t addr, uint32_t len) {
    uint32_t end;
    if (len == 0) return 0;
    end = addr + len;
    if (end < addr) return 0;
    return addr >= (uint32_t)(uintptr_t)TEXT_START && end <= (uint32_t)(uintptr_t)APP_END;
}

static void mem_append_map(char *out) {
    char row[160];
    uint32_t kstart = 0, kend = 0;
    uint32_t ub = 0, us = 0, uu = 0, uf = 0, ublk = 0;
    kernel_heap_range_info(&kstart, &kend);
    jit_uheap_info(&ub, &us, &uu, &uf, &ublk);
    out[0] = '\0';
    snprintf(row, sizeof(row), "Memory map:\ntext   %x-%x\nrodata %x-%x\ndata   %x-%x\nbss    %x-%x\n",
             (uint32_t)(uintptr_t)TEXT_START, (uint32_t)(uintptr_t)TEXT_END,
             (uint32_t)(uintptr_t)RODATA_START, (uint32_t)(uintptr_t)RODATA_END,
             (uint32_t)(uintptr_t)DATA_START, (uint32_t)(uintptr_t)DATA_END,
             (uint32_t)(uintptr_t)BSS_START, (uint32_t)(uintptr_t)BSS_END);
    out_append(out, row);
    snprintf(row, sizeof(row), "kheap  %x-%x size=%uKB\n", kstart, kend, (kend - kstart) / 1024);
    out_append(out, row);
    snprintf(row, sizeof(row), "uheap  %x-%x used=%uKB free=%uKB blocks=%u\n", ub, ub + us, uu / 1024, uf / 1024, ublk);
    out_append(out, row);
    snprintf(row, sizeof(row), "app    %x-%x size=%uKB\n", (uint32_t)(uintptr_t)APP_START, (uint32_t)(uintptr_t)APP_END, (uint32_t)(uintptr_t)APP_SIZE / 1024);
    out_append(out, row);
    if (loaded_app_heap_hi > loaded_app_heap_lo) {
        snprintf(row, sizeof(row), "appheap %x-%x cur=%x\n", (uint32_t)loaded_app_heap_lo, (uint32_t)loaded_app_heap_hi, (uint32_t)loaded_app_heap_cur);
        out_append(out, row);
    }
}

static void mem_dump_range(char *out, uint32_t addr, uint32_t len) {
    static const char hex[] = "0123456789abcdef";
    const uint8_t *p = (const uint8_t *)(uintptr_t)addr;
    char row[128];
    if (len == 0) len = 128;
    if (len > 256) len = 256;
    out[0] = '\0';
    if (!mem_range_safe(addr, len)) {
        snprintf(out, OUT_BUF_SIZE, "ERR: unsafe range %x + %u. Use 'mem map' first.", addr, len);
        return;
    }
    for (uint32_t off = 0; off < len; off += 16) {
        char bytes[16 * 3 + 1];
        char ascii[17];
        uint32_t n = len - off;
        int bp = 0;
        if (n > 16) n = 16;
        for (uint32_t i = 0; i < 16; i++) {
            if (i < n) {
                uint8_t c = p[off + i];
                bytes[bp++] = hex[c >> 4];
                bytes[bp++] = hex[c & 15];
                bytes[bp++] = ' ';
                ascii[i] = (c >= 32 && c < 127) ? (char)c : '.';
            } else {
                bytes[bp++] = ' ';
                bytes[bp++] = ' ';
                bytes[bp++] = ' ';
                ascii[i] = ' ';
            }
        }
        bytes[bp] = '\0';
        ascii[16] = '\0';
        snprintf(row, sizeof(row), "%x: %s |%s|\n", addr + off, bytes, ascii);
        out_append(out, row);
    }
}

static void exec_mem_cmd(char *out, char *arg) {
    char row[160];
    while (*arg == ' ' || *arg == '\t') arg++;
    if (*arg == '\0') {
        uint32_t total, used, free_pg, m_calls, f_calls;
        uint32_t ub = 0, us = 0, uu = 0, uf = 0, ublk = 0;
        mem_usage_info(&total, &used, &free_pg, &m_calls, &f_calls);
        jit_uheap_info(&ub, &us, &uu, &uf, &ublk);
        snprintf(out, OUT_BUF_SIZE,
                 "RAM: total=%uKB, used=%uKB, free=%uKB\nActivity: malloc=%u, free=%u\nJIT uheap: base=%x size=%uKB used=%uKB free=%uKB blocks=%u",
                 total * 4, used * 4, free_pg * 4, m_calls, f_calls,
                 ub, us / 1024, uu / 1024, uf / 1024, ublk);
        return;
    }
    if (strcmp(arg, "h") == 0 || strcmp(arg, "-h") == 0) {
        lib_strcpy(out, "usage: mem | mem map | mem view <hex_addr|kheap|uheap|app> [len<=256]");
        return;
    }
    if (strncmp(arg, "map", 3) == 0 && (arg[3] == '\0' || arg[3] == ' ')) {
        mem_append_map(out);
        return;
    }
    if (strncmp(arg, "view", 4) == 0 && (arg[4] == '\0' || arg[4] == ' ')) {
        uint32_t addr = 0;
        uint32_t len = 128;
        uint32_t kstart = 0, kend = 0;
        uint32_t ub = 0, us = 0, uu = 0, uf = 0, ublk = 0;
        const char *endp = NULL;
        arg += 4;
        while (*arg == ' ' || *arg == '\t') arg++;
        if (*arg == '\0') {
            lib_strcpy(out, "usage: mem view <hex_addr|kheap|uheap|app> [len<=256]");
            return;
        }
        if (strncmp(arg, "kheap", 5) == 0 && (arg[5] == '\0' || arg[5] == ' ')) {
            kernel_heap_range_info(&kstart, &kend);
            addr = kstart;
            arg += 5;
        } else if (strncmp(arg, "uheap", 5) == 0 && (arg[5] == '\0' || arg[5] == ' ')) {
            jit_uheap_info(&ub, &us, &uu, &uf, &ublk);
            addr = ub;
            arg += 5;
        } else if (strncmp(arg, "app", 3) == 0 && (arg[3] == '\0' || arg[3] == ' ')) {
            addr = (uint32_t)(uintptr_t)APP_START;
            arg += 3;
        } else {
            if (parse_hex_u32(arg, &addr, &endp) != 0) {
                lib_strcpy(out, "ERR: bad address. Example: mem view 80000000 128");
                return;
            }
            arg = (char *)endp;
        }
        while (*arg == ' ' || *arg == '\t') arg++;
        if (*arg) {
            int n = atoi(arg);
            if (n > 0) len = (uint32_t)n;
        }
        if (len > 256) len = 256;
        mem_dump_range(out, addr, len);
        return;
    }
    snprintf(row, sizeof(row), "ERR: mem | mem map | mem view <addr> [len]");
    lib_strcpy(out, row);
}

struct command_handler {
    const char *name;
    int (*run)(struct Window *w, char *cmd, char *out);
};

static int cmd_handle_mem(struct Window *w, char *cmd, char *out) {
    (void)w;
    if (strncmp(cmd, "mem", 3) != 0 || (cmd[3] != '\0' && cmd[3] != ' ')) return 0;
    exec_mem_cmd(out, cmd + 3);
    return 1;
}

static int cmd_handle_help(struct Window *w, char *cmd, char *out) {
    (void)w;
    if (strncmp(cmd, "help", 4) != 0 || (cmd[4] != '\0' && cmd[4] != ' ')) return 0;
        lib_strcpy(out, "Commands: ls, sd, sdls, find, mem, ethstat, df, du, mkdir, rm, mv, touch, cd, pwd, write, cat, wget, open, run, jit, gbemu, vim, asm, demo3d, frankenstein, netsurf, ssh, sftp, wrp, format, clear, env, export, unset, alias, unalias, source, ., echo\nType '<cmd> h' for usage.");
    return 1;
}

static int path_is_sd_root(const char *path) {
    if (!path) return 0;
    return strcmp(path, "sd") == 0 ||
           strcmp(path, "/sd") == 0 ||
           strcmp(path, "/sd/") == 0 ||
           strcmp(path, "./sd") == 0 ||
           strcmp(path, "./sd/") == 0;
}

static int cmd_handle_ethstat(struct Window *w, char *cmd, char *out) {
    (void)w;
    if (strncmp(cmd, "ethstat", 7) != 0 || (cmd[7] != '\0' && cmd[7] != ' ')) return 0;
    if (cmd[7] == ' ' && (cmd[8] == 'h' || cmd[8] == '-')) {
        lib_strcpy(out, "usage: ethstat");
        return 1;
    }
    uint32_t dbg[8];
    if (ps_gem_is_ready()) ps_gem_poll();
    else ps_gem_init();
    ps_gem_get_debug(dbg);

    const char *probe = "unknown";
    if ((dbg[7] & 0xfffffff0UL) == 0x45544880UL) probe = "OK";
    else if ((dbg[7] & 0xfffffff0UL) == 0x45544890UL) probe = "DRIVER";
    else if (dbg[7] == 0x455448e0UL) probe = "GEM_OFF";
    else if (dbg[7] == 0x455448f0UL) probe = "FAIL";

    if ((dbg[7] & 0xfffffff0UL) == 0x45544890UL) {
        snprintf(out, OUT_BUF_SIZE,
                 "eth0: %s ip=192.168.0.154 link=%s\n"
                 "nwcfg=%x nwctrl=%x nwsr=%x\n"
                 "tx=%x rx=%x status=%x",
                 probe,
                 (dbg[7] & 1UL) ? "up" : "down",
                 dbg[0], dbg[1], dbg[2], dbg[5], dbg[6], dbg[7]);
    } else {
        snprintf(out, OUT_BUF_SIZE,
                 "eth0: phy %s link=%s\n"
                 "nwcfg=%x nwctrl=%x nwsr=%x\n"
                 "phyid=%x:%x bmsr=%x rc=%x status=%x",
                 probe,
                 (dbg[7] & 1UL) ? "up" : "down",
                 dbg[0], dbg[1], dbg[2],
                 dbg[3] & 0xffff, dbg[4] & 0xffff, dbg[6] & 0xffff,
                 dbg[5], dbg[7]);
    }
    return 1;
}

static void exec_single_cmd_legacy(struct Window *w, char *cmd) {
    char *out = w->out_buf; out[0] = '\0';
    if (strncmp(cmd, "jit", 3) == 0 && (cmd[3] == ' ' || cmd[3] == '\0')) {
        int bg = 0;
        char *arg = cmd + 3; while (*arg == ' ') arg++;
        if (strncmp(arg, "ps", 2) == 0 && (arg[2] == '\0' || arg[2] == ' ')) {
            os_jit_ps(out, OUT_BUF_SIZE);
            return;
        }
        if (jit_debugger_exec_cmd(w, arg, out, OUT_BUF_SIZE)) {
            return;
        }
        if (strncmp(arg, "kill", 4) == 0 && (arg[4] == '\0' || arg[4] == ' ')) {
            arg += 4;
            while (*arg == ' ') arg++;
            if (*arg == '\0') {
                lib_strcpy(out, "usage: jit kill <id>");
                return;
            }
            os_jit_kill(atoi(arg), out, OUT_BUF_SIZE);
            return;
        }
        if (strncmp(arg, "shared reset", 12) == 0 && (arg[12] == '\0' || arg[12] == ' ')) {
            os_jit_shared_reset();
            lib_strcpy(out, "JIT shared memory reset.");
            return;
        }
        if ((*arg == 'h' && (arg[1] == '\0' || arg[1] == ' ')) || strncmp(arg, "-h", 2) == 0) {
            lib_strcpy(out, "usage: jit [bg] \"source\" | jit [bg] file.c | jit dbg [file.c] | jit c | jit ps | jit kill <id> | jit shared reset");
            return;
        }
        if (strncmp(arg, "bg", 2) == 0 && (arg[2] == '\0' || arg[2] == ' ')) {
            bg = 1;
            arg += 2;
            while (*arg == ' ') arg++;
        }
        if (*arg == '\0') {
            lib_strcpy(out, "usage: jit [bg] \"source\" | jit [bg] file.c | jit dbg [file.c] | jit c | jit ps | jit kill <id> | jit shared reset");
            return;
        }
        if (arg[0] == '"') {
            char *source = arg + 1;
            char *end = strrchr(source, '"');
            if (end) *end = '\0';
            if (bg) {
                os_jit_run_bg(source, w->id, out, OUT_BUF_SIZE);
            } else {
                int rc = os_jit_run(source, w->id);
                if (rc == -2) out[0] = '\0';
                else lib_strcpy(out, "JIT execution finished.");
            }
        } else {
            uint32_t size = 0;
            if (path_is_sftp(arg)) {
                unsigned char *remote_buf = NULL;
                char msg[OUT_BUF_SIZE];
                if (ssh_client_sftp_read_alloc(sftp_subpath(arg), &remote_buf, &size, msg, sizeof(msg)) == 0 && remote_buf) {
                    char *src = (char *)malloc(size + 1);
                    int rc = 0;
                    if (!src) {
                        free(remote_buf);
                        lib_strcpy(out, "ERR: No Memory.");
                        return;
                    }
                    memcpy(src, remote_buf, size);
                    src[size] = '\0';
                    free(remote_buf);
                    appfs_set_cwd(w->cwd_bno, w->cwd);
                    if (bg) os_jit_run_bg(src, w->id, out, OUT_BUF_SIZE);
                    else {
                        rc = os_jit_run(src, w->id);
                        if (rc == -2) out[0] = '\0';
                    }
                    free(src);
                    if (!bg && rc != -2 && out[0] == '\0') lib_strcpy(out, "JIT SFTP file execution finished.");
                } else {
                    lib_strcpy(out, msg[0] ? msg : "ERR: Could not read SFTP file.");
                }
            } else if (load_file_bytes(w, arg, file_io_buf, WGET_MAX_FILE_SIZE, &size) == 0) {
                char expanded[128];
                uint32_t p_bno = 0;
                char p_cwd[128], leaf[20], abs_path[128];
                if (resolve_editor_target(w, arg, &p_bno, p_cwd, leaf) == 0 && leaf[0]) {
                    build_editor_path(abs_path, p_cwd, leaf);
                } else {
                    abs_path[0] = '\0';
                }
                appfs_set_cwd(w->cwd_bno, w->cwd);
                if (bg) {
                    if (abs_path[0]) os_jit_run_bg_file(abs_path, w->id, out, OUT_BUF_SIZE);
                    else os_jit_run_bg((const char *)file_io_buf, w->id, out, OUT_BUF_SIZE);
                } else {
                    int rc;
                    if (abs_path[0]) rc = os_jit_run_file(abs_path, w->id);
                    else rc = os_jit_run((const char *)file_io_buf, w->id);
                    if (rc == -2) out[0] = '\0';
                    else lib_strcpy(out, "JIT file execution finished.");
                }
            } else {
                lib_strcpy(out, "ERR: Could not read file.");
            }
        }
    } else if (strncmp(cmd, "pwd", 3) == 0) {
        if (cmd[3] == ' ' && (cmd[4] == 'h' || cmd[4] == '-')) { lib_strcpy(out, "usage: pwd"); return; }
        lib_strcpy(out, w->cwd);
    }
    else if (strncmp(cmd, "df", 2) == 0 && (cmd[2] == '\0' || cmd[2] == ' ')) {
        if (cmd[2] == ' ' && (cmd[3] == 'h' || cmd[3] == '-')) { lib_strcpy(out, "usage: df"); return; }
        uint32_t total, used, free_blks;
        fs_usage_info(&total, &used, &free_blks);
        char row[128], sz_total[16], sz_used[16], sz_free[16];
        format_size_human(total * BSIZE, sz_total);
        format_size_human(used * BSIZE, sz_used);
        format_size_human(free_blks * BSIZE, sz_free);
        snprintf(row, sizeof(row), "Disk Usage (BlockSize=%d):\n  Total: %8s (%u blocks)\n  Used:  %8s (%u blocks)\n  Free:  %8s (%u blocks)\n",
                 BSIZE, sz_total, total, sz_used, used, sz_free, free_blks);
        lib_strcpy(out, row);
    } else if (strcmp(cmd, "sdls") == 0 || strncmp(cmd, "sdls ", 5) == 0 ||
               strcmp(cmd, "sd") == 0 || strncmp(cmd, "sd ", 3) == 0) {
        char *arg;
        int all = 0;
        if (strncmp(cmd, "sdls", 4) == 0) arg = cmd + 4;
        else arg = cmd + 2;
        while (*arg == ' ') arg++;
        if (*arg == 'h' && (*(arg+1) == '\0' || *(arg+1) == ' ')) {
            lib_strcpy(out, "usage: sd status | sd ls [-all] | sdls [-all]");
            return;
        }
        if (strcmp(arg, "status") == 0) {
            sd_fat_status(out, OUT_BUF_SIZE);
            return;
        }
        if (strncmp(arg, "ls", 2) == 0 && (arg[2] == '\0' || arg[2] == ' ')) {
            arg += 2;
            while (*arg == ' ') arg++;
        }
        if (strncmp(arg, "-all", 4) == 0) all = 1;
        sd_fat_list_root(out, OUT_BUF_SIZE, all);
    } else if (strncmp(cmd, "du", 2) == 0 && (cmd[2] == '\0' || cmd[2] == ' ')) {
        char *path_arg = cmd + 2; while (*path_arg == ' ') path_arg++;
        if (*path_arg == 'h' && (*(path_arg+1) == '\0' || *(path_arg+1) == ' ')) { lib_strcpy(out, "usage: du [-maxd N] [path]"); return; }
        int max_depth = 20; char *mpos = strstr(cmd, "-maxd");
        if (mpos) {
            char *dstr = mpos + 5; while (*dstr == ' ') dstr++;
            if (*dstr >= '0' && *dstr <= '9') {
                max_depth = 0; while (*dstr >= '0' && *dstr <= '9') { max_depth = max_depth * 10 + (*dstr - '0'); dstr++; }
            }
            path_arg = dstr; while (*path_arg == ' ') path_arg++;
        }
        uint32_t target_bno = w->cwd_bno; const char *dpath = w->cwd; char expanded[128];
        if (*path_arg != '\0') {
            uint32_t p_bno = 0; char p_cwd[128], leaf[20];
            terminal_expand_home_path(w, path_arg, expanded, sizeof(expanded));
            if (resolve_editor_target(w, expanded, &p_bno, p_cwd, leaf) == 0) {
                if (leaf[0] == '\0' || strcmp(leaf, ".") == 0) { target_bno = p_bno; dpath = (expanded[0] ? expanded : w->cwd); }
                else {
                    extern struct Window fs_tmp_window; struct Window *tmp = &fs_tmp_window;
                    memset(tmp, 0, sizeof(*tmp)); tmp->cwd_bno = p_bno;
                    struct dir_block *db = load_current_dir(tmp);
                    if (db) {
                        int idx = find_entry_index(db, leaf, 1);
                        if (idx >= 0) { target_bno = db->entries[idx].bno; dpath = expanded; }
                        else { lib_strcpy(out, "ERR: Dir Not Found."); return; }
                    }
                }
            } else { lib_strcpy(out, "ERR: Invalid Path."); return; }
        }
        uint32_t size = calculate_dir_size(target_bno, 1, max_depth);
        char sz_str[16]; format_size_human(size, sz_str);
        snprintf(out, OUT_BUF_SIZE, "Size of %s (maxdepth %d): %s (%u bytes)", dpath, max_depth, sz_str, size);
    } else if (strncmp(cmd, "format", 6) == 0) {
        char *arg = cmd + 6;
        while (*arg == ' ') arg++;
        if (strcmp(arg, "YES_I_AM_SURE") == 0) {
            fs_format_root(w);
            fs_reset_window_cwd(w);
            terminal_env_sync_pwd(w);
            lib_strcpy(out, ">> Matrix Rebuilt. All data wiped.");
        } else {
            lib_strcpy(out, "DANGER: This will wipe EVERYTHING.\nType: format YES_I_AM_SURE");
        }
    } else if (strncmp(cmd, "cd ", 3) == 0 || strcmp(cmd, "cd") == 0) {
        char *target = cmd + 2;
        while (*target == ' ') target++;
        if (*target == 'h' && (*(target+1) == '\0' || *(target+1) == ' ')) {
            lib_strcpy(out, "usage: cd ~ | cd .. | cd /root/subdir | cd subdir");
            return;
        }
        if (*target == '\0' || strcmp(target, "/root") == 0 || strcmp(target, "/") == 0) {
            w->cwd_bno = 1;
            lib_strcpy(w->cwd, "/root");
            terminal_env_sync_pwd(w);
        } else if (strcmp(target, "..") == 0) {
            struct dir_block *db = load_current_dir(w);
            if (!db) { lib_strcpy(out, "ERR: Run 'format'."); return; }
            w->cwd_bno = db->parent_bno ? db->parent_bno : 1;
            if (w->cwd_bno == 1) lib_strcpy(w->cwd, "/root");
            else path_set_parent(w->cwd);
            terminal_env_sync_pwd(w);
        } else {
            char expanded[128];
            uint32_t parent_bno = 0;
            char parent_cwd[128];
            char leaf[20];
            extern struct Window fs_tmp_window;
            struct Window *tmp = &fs_tmp_window;
            struct dir_block *db;
            int idx;
            terminal_expand_home_path(w, target, expanded, sizeof(expanded));
            if (expanded[0] == '\0') { lib_strcpy(out, "ERR: Dir Not Found."); return; }
            if (resolve_editor_target(w, expanded, &parent_bno, parent_cwd, leaf) != 0) {
                lib_strcpy(out, "ERR: Dir Not Found.");
                return;
            }
            if (leaf[0] == '\0' || strcmp(leaf, ".") == 0) {
                w->cwd_bno = parent_bno ? parent_bno : 1;
                lib_strcpy(w->cwd, parent_cwd[0] ? parent_cwd : "/root");
                terminal_env_sync_pwd(w);
                return;
            }
            memset(tmp, 0, sizeof(*tmp));
            tmp->cwd_bno = parent_bno ? parent_bno : 1;
            lib_strcpy(tmp->cwd, parent_cwd[0] ? parent_cwd : "/root");
            db = load_current_dir(tmp);
            if (!db) { lib_strcpy(out, "ERR: Run 'format'."); return; }
            idx = find_entry_index(db, leaf, 1);
            if (idx < 0) { lib_strcpy(out, "ERR: Dir Not Found."); return; }
            w->cwd_bno = db->entries[idx].bno;
            lib_strcpy(w->cwd, tmp->cwd[0] ? tmp->cwd : "/root");
            path_set_child(w->cwd, db->entries[idx].name);
            terminal_env_sync_pwd(w);
        }
    } else if (strncmp(cmd, "alias", 5) == 0 && (cmd[5] == '\0' || cmd[5] == ' ')) {
        char *spec = cmd + 5;
        while (*spec == ' ') spec++;
        if (*spec == 'h' && (*(spec+1) == '\0' || *(spec+1) == ' ')) {
            lib_strcpy(out, "usage: alias name=value | alias (to list all)");
            return;
        }
        char *eq;
        char row[OUT_BUF_SIZE];
        while (*spec == ' ') spec++;
        if (*spec == '\0') {
            out[0] = '\0';
            for (int i = 0; i < w->alias_count; i++) {
                append_out_str(out, OUT_BUF_SIZE, "alias ");
                append_out_str(out, OUT_BUF_SIZE, w->alias_names[i]);
                append_out_str(out, OUT_BUF_SIZE, "='");
                append_out_str(out, OUT_BUF_SIZE, w->alias_values[i]);
                append_out_str(out, OUT_BUF_SIZE, "'");
                if (i + 1 < w->alias_count) append_out_str(out, OUT_BUF_SIZE, "\n");
            }
            if (w->alias_count == 0) lib_strcpy(out, "(empty)");
        } else {
            eq = strchr(spec, '=');
            if (!eq) {
                const char *val = terminal_alias_get(w, spec);
                if (val && val[0]) {
                    lib_strcpy(row, "alias ");
                    lib_strcat(row, spec);
                    lib_strcat(row, "='");
                    lib_strcat(row, val);
                    lib_strcat(row, "'");
                    lib_strcpy(out, row);
                } else {
                    lib_strcpy(out, "ERR: alias not found.");
                }
            } else {
                *eq++ = '\0';
                while (*eq == ' ') eq++;
                if (*spec == '\0' || *eq == '\0') {
                    lib_strcpy(out, "ERR: alias name=value.");
                } else if (terminal_alias_set(w, spec, eq) != 0) {
                    lib_strcpy(out, "ERR: alias failed.");
                }
            }
        }
    } else if (strncmp(cmd, "unalias", 7) == 0 && (cmd[7] == '\0' || cmd[7] == ' ')) {
        char *name = cmd + 7;
        while (*name == ' ') name++;
        if (*name == '\0') {
            lib_strcpy(out, "ERR: unalias NAME.");
        } else if (terminal_alias_unset(w, name) != 0) {
            lib_strcpy(out, "ERR: unalias failed.");
        }
    } else if (strncmp(cmd, "source", 6) == 0 && (cmd[6] == '\0' || cmd[6] == ' ')) {
        char *path = cmd + 6;
        while (*path == ' ') path++;
        if (*path == 'h' && (*(path+1) == '\0' || *(path+1) == ' ')) {
            lib_strcpy(out, "usage: source <file> | . <file>");
            return;
        }
        if (*path == '\0') {
            lib_strcpy(out, "ERR: source <file>.");
        } else {
            terminal_source_script(w, path);
        }
    } else if (cmd[0] == '.' && (cmd[1] == '\0' || cmd[1] == ' ')) {
        char *path = cmd + 1;
        while (*path == ' ') path++;
        if (*path == '\0') {
            lib_strcpy(out, "ERR: source <file>.");
        } else {
            terminal_source_script(w, path);
        }
    } else if (strncmp(cmd, "export", 6) == 0 && (cmd[6] == '\0' || cmd[6] == ' ')) {
        char *spec = cmd + 6;
        while (*spec == ' ') spec++;
        if (*spec == 'h' && (*(spec+1) == '\0' || *(spec+1) == ' ')) {
            lib_strcpy(out, "usage: export NAME=value");
            return;
        }
        char name[ENV_NAME_LEN];
        char value[ENV_VALUE_LEN];
        char *eq;
        int n = 0;
        while (*spec == ' ') spec++;
        if (*spec == '\0') {
            lib_strcpy(out, "ERR: export NAME=value.");
        } else {
            eq = strchr(spec, '=');
            if (!eq) {
                while (spec[n] && spec[n] != ' ' && n < ENV_NAME_LEN - 1) {
                    name[n] = spec[n];
                    n++;
                }
                name[n] = '\0';
                value[0] = '\0';
            } else {
                while (spec[n] && spec + n < eq && n < ENV_NAME_LEN - 1) {
                    name[n] = spec[n];
                    n++;
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
            }
            if (terminal_env_set(w, name, value) != 0) {
                lib_strcpy(out, "ERR: export failed.");
            }
        }
    } else if (strncmp(cmd, "unset", 5) == 0 && (cmd[5] == '\0' || cmd[5] == ' ')) {
        char *name = cmd + 5;
        while (*name == ' ') name++;
        if (*name == '\0') {
            lib_strcpy(out, "ERR: unset NAME.");
        } else if (terminal_env_unset(w, name) != 0) {
            lib_strcpy(out, "ERR: unset failed.");
        }
    } else if (strcmp(cmd, "env") == 0 || strncmp(cmd, "printenv", 8) == 0) {
        out[0] = '\0';
        for (int i = 0; i < w->env_count; i++) {
            append_out_str(out, OUT_BUF_SIZE, w->env_names[i]);
            append_out_str(out, OUT_BUF_SIZE, "=");
            append_out_str(out, OUT_BUF_SIZE, w->env_values[i]);
            if (i + 1 < w->env_count) append_out_str(out, OUT_BUF_SIZE, "\n");
        }
        if (w->env_count == 0) {
            lib_strcpy(out, "(empty)");
        }
    } else if (strncmp(cmd, "find ", 5) == 0) {
        char *target = cmd + 5;
        while (*target == ' ') target++;
        if (*target == '\0') { lib_strcpy(out, "usage: find <filename>"); return; }
        out[0] = '\0';
        recursive_find(w, 1, "/root", target, out);
        if (out[0] == '\0') lib_strcpy(out, "No files found.");
    } else if (strncmp(cmd, "ls", 2) == 0 && (cmd[2] == '\0' || cmd[2] == ' ')) {
        char *h_ptr = cmd + 2; while (*h_ptr == ' ') h_ptr++;
        if (strcmp(h_ptr, "h") == 0 || strcmp(h_ptr, "-h") == 0) {
            lib_strcpy(out, "usage: ls [-all] [path]"); return;
        }
        int all = (strstr(cmd, "-all") != 0);
        char *path_arg = cmd + 2; while (*path_arg == ' ') path_arg++;
        if (all) {
            char *all_ptr = strstr(cmd, "-all");
            path_arg = all_ptr + 4; while (*path_arg == ' ') path_arg++;
            if (*path_arg == '\0') {
                path_arg = cmd + 2; while (*path_arg == ' ' || *path_arg == '-') path_arg++;
                if (strncmp(path_arg, "all", 3) == 0) { path_arg += 3; while (*path_arg == ' ') path_arg++; }
            }
        }
        uint32_t target_bno = w->cwd_bno;
        int show_virtual_mounts = 0;
        char single_out[OUT_BUF_SIZE];
        single_out[0] = '\0';
        if (*path_arg != '\0' && strcmp(path_arg, "-all") != 0) {
            if (path_is_sd_root(path_arg)) {
                sd_fat_list_root(out, OUT_BUF_SIZE, all);
                return;
            }
            if (path_is_sftp(path_arg)) {
                ssh_client_sftp_ls(sftp_subpath(path_arg), all, out, OUT_BUF_SIZE);
                return;
            }
            uint32_t p_bno = 0; char p_cwd[128], leaf[20];
            if (resolve_fs_target(w, path_arg, &p_bno, p_cwd, leaf) == 0) {
                if (leaf[0] == '\0' || strcmp(leaf, ".") == 0) { target_bno = p_bno; }
                else {
                    extern struct Window fs_tmp_window; struct Window *ltmp = &fs_tmp_window;
                    memset(ltmp, 0, sizeof(*ltmp)); ltmp->cwd_bno = p_bno;
                    struct dir_block *db = load_current_dir(ltmp);
                    if (db) {
                        int idx_file = find_entry_index(db, leaf, 0);
                        int idx_dir = find_entry_index(db, leaf, 1);
                        if (idx_file >= 0) {
                            append_single_entry_line(&db->entries[idx_file], single_out, all);
                            lib_strcpy(out, single_out);
                            return;
                        }
                        if (idx_dir >= 0) target_bno = db->entries[idx_dir].bno;
                        else { lib_strcpy(out, "ERR: Dir Not Found."); return; }
                    }
                }
            } else { lib_strcpy(out, "ERR: Invalid Path."); return; }
            if (strcmp(path_arg, "/") == 0 || strcmp(path_arg, "/root") == 0 || strcmp(path_arg, "~") == 0) {
                show_virtual_mounts = 1;
            }
        } else if (target_bno == 1 || strcmp(w->cwd, "/root") == 0 || strcmp(w->cwd, "/") == 0) {
            show_virtual_mounts = 1;
        }
        extern struct Window fs_tmp_window; struct Window *ctx = &fs_tmp_window;
        memset(ctx, 0, sizeof(*ctx)); ctx->cwd_bno = target_bno;
        struct dir_block *db = load_current_dir(ctx);
        if(!db) { lib_strcpy(out, "ERR: Run 'format'."); return; }
        int printed = 0;
        int stream_lines = 0;
        out[0] = '\0';
        for(int i=0; i<MAX_DIR_ENTRIES; i++) if(db->entries[i].name[0]) {
            if(!all && db->entries[i].name[0] == '.' && strcmp(db->entries[i].name, ".bashrc") != 0) continue;
            char row[OUT_BUF_SIZE]; row[0] = '\0';
            if(all) {
                char ms[5], sz[16], ts[10], name[32];
                mode_to_str(db->entries[i].mode, db->entries[i].type, ms);
                format_size_human(db->entries[i].size, sz); format_hms(db->entries[i].mtime, ts);
                copy_name20(name, db->entries[i].name);
                append_out_pad(row, OUT_BUF_SIZE, ms, 5); append_out_str(row, OUT_BUF_SIZE, " ");
                append_out_pad(row, OUT_BUF_SIZE, sz, 8); append_out_str(row, OUT_BUF_SIZE, " ");
                append_out_pad(row, OUT_BUF_SIZE, ts, 19); append_out_str(row, OUT_BUF_SIZE, " ");
                append_out_str(row, OUT_BUF_SIZE, name);
            } else {
                append_out_str(row, OUT_BUF_SIZE, (db->entries[i].type==1) ? "d " : "f ");
                append_out_str(row, OUT_BUF_SIZE, db->entries[i].name);
            }
            append_out_str(row, OUT_BUF_SIZE, "\n");
            append_terminal_line(w, row);
            stream_lines++;
            if ((stream_lines & 7) == 0) task_os();
            printed = 1;
        }
        if (show_virtual_mounts || target_bno == 1) {
            char mount_row[OUT_BUF_SIZE];
            int mount_printed = 0;
            mount_row[0] = '\0';
            append_virtual_mount_entries(1, all, mount_row, &mount_printed);
            if (mount_printed) {
                append_terminal_line(w, mount_row);
                printed = 1;
            }
        }
        if (!printed) {
            append_terminal_line(w, "(empty)");
        }
        out[0] = '\0';
        return;
    } else if (strncmp(cmd, "sftp", 4) == 0 && (cmd[4] == '\0' || cmd[4] == ' ')) {
        char *arg = cmd + 4;
        while (*arg == ' ') arg++;
        if (*arg == 'h' && (*(arg+1) == '\0' || *(arg+1) == ' ')) {
            lib_strcpy(out, "usage: sftp status | sftp mount [remote_root] | sftp ls [path] | sftp get <remote> [local] | sftp put <local> [remote]");
            return;
        }
        if (*arg == '\0' || strcmp(arg, "status") == 0) {
            ssh_client_sftp_status(out, OUT_BUF_SIZE);
        } else if (strncmp(arg, "mount", 5) == 0 && (arg[5] == '\0' || arg[5] == ' ')) {
            char *root = arg + 5;
            while (*root == ' ') root++;
            ssh_client_sftp_mount(*root ? root : ".", out, OUT_BUF_SIZE);
        } else if (strncmp(arg, "ls", 2) == 0 && (arg[2] == '\0' || arg[2] == ' ')) {
            char *path = arg + 2;
            int all = 0;
            while (*path == ' ') path++;
            if (strncmp(path, "-all", 4) == 0 && (path[4] == '\0' || path[4] == ' ')) {
                all = 1;
                path += 4;
                while (*path == ' ') path++;
            }
            ssh_client_sftp_ls(path, all, out, OUT_BUF_SIZE);
        } else if (strncmp(arg, "get ", 4) == 0) {
            char *remote = arg + 4;
            char *local;
            while (*remote == ' ') remote++;
            local = strchr(remote, ' ');
            if (local) {
                *local++ = '\0';
                while (*local == ' ') local++;
                if (*local == '\0') local = NULL;
            }
            ssh_client_sftp_get(w, remote, local, out, OUT_BUF_SIZE);
        } else if (strncmp(arg, "put ", 4) == 0) {
            char *local = arg + 4;
            char *remote;
            while (*local == ' ') local++;
            remote = strchr(local, ' ');
            if (remote) {
                *remote++ = '\0';
                while (*remote == ' ') remote++;
                if (*remote == '\0') remote = NULL;
            }
            ssh_client_sftp_put(w, local, remote, out, OUT_BUF_SIZE);
        } else {
            lib_strcpy(out, "ERR: sftp status | mount | ls | get | put");
        }
    } else if (strncmp(cmd, "ssh", 3) == 0 && (cmd[3] == '\0' || cmd[3] == ' ')) {
        char *arg = cmd + 3;
        while (*arg == ' ') arg++;
        if (*arg == 'h' && (*(arg+1) == '\0' || *(arg+1) == ' ')) {
            lib_strcpy(out, "usage: ssh status | ssh diag | ssh probe <host[:port]> | ssh set <user@host[:port]> | ssh auth [password] | ssh boot init|seal|apply | ssh exec <cmd>");
            return;
        }
        if (*arg == '\0' || strcmp(arg, "status") == 0) {
            char target[96];
            char wrp_url[160];
            out[0] = '\0';
            lib_strcat(out, "ssh: ");
            if (ssh_client_get_target(target, sizeof(target)) == 0) {
                lib_strcat(out, target);
            } else {
                lib_strcat(out, "(unset)");
            }
            lib_strcat(out, " wrp: ");
            if (ssh_client_get_wrp_url(wrp_url, sizeof(wrp_url)) == 0) {
                lib_strcat(out, wrp_url);
            } else {
                lib_strcat(out, "(unset)");
            }
        } else if (strcmp(arg, "diag") == 0) {
            ssh_client_diag(out, OUT_BUF_SIZE);
        } else if (strchr(arg, '@') != NULL && strchr(arg, ' ') == NULL) {
            if (ssh_client_set_target(arg, out, OUT_BUF_SIZE) < 0 && out[0] == '\0') {
                lib_strcpy(out, "ERR: ssh set <user@host[:port]>.");
            }
        } else if (strncmp(arg, "set ", 4) == 0) {
            char *spec = arg + 4;
            while (*spec == ' ') spec++;
            if (ssh_client_set_target(spec, out, OUT_BUF_SIZE) < 0 && out[0] == '\0') {
                lib_strcpy(out, "ERR: ssh set <user@host[:port]>.");
            }
        } else if (strncmp(arg, "probe ", 6) == 0) {
            char *spec = arg + 6;
            while (*spec == ' ') spec++;
            if (ssh_client_probe(spec, out, OUT_BUF_SIZE) < 0 && out[0] == '\0') {
                lib_strcpy(out, "ERR: ssh probe <host[:port]>.");
            }
        } else if (strncmp(arg, "auth ", 5) == 0) {
            char *password = arg + 5;
            while (*password == ' ') password++;
            if (ssh_client_set_password(password, out, OUT_BUF_SIZE) < 0 && out[0] == '\0') {
                lib_strcpy(out, "ERR: ssh auth <password>.");
            }
        } else if (strncmp(arg, "boot", 4) == 0 && (arg[4] == '\0' || arg[4] == ' ')) {
            char *sub = arg + 4;
            while (*sub == ' ') sub++;
            if (strcmp(sub, "init") == 0 || *sub == '\0') {
                const char *tmpl =
                    "# ssh auto-login boot config\n"
                    "# run: ssh boot seal <password>\n"
                    "# quick manual test:\n"
                    "# ssh probe 192.168.0.152:2221\n"
                    "# ssh set root@192.168.0.152:2221\n"
                    "# ssh auth <your WSL root password>\n"
                    "# ssh exec uname -a\n"
                    "target=root@192.168.0.152:2221\n"
                    "# password=your-password\n"
                    "# password_obf=v1:...\n"
                    "sftp=/root/trade_new/os/mini-riscv-os/08-BlockDeviceDriver/http_test\n";
                if (store_file_bytes(w, "~/.sshboot", (const unsigned char *)tmpl, (uint32_t)strlen(tmpl)) == 0) {
                    lib_strcpy(out, "OK: wrote ~/.sshboot. Run: ssh boot seal <password>");
                } else {
                    lib_strcpy(out, "ERR: cannot write ~/.sshboot.");
                }
            } else if (strncmp(sub, "seal ", 5) == 0) {
                char *password = sub + 5;
                char enc[160];
                char target[96];
                char tmpl[512];
                while (*password == ' ') password++;
                if (*password == '\0') {
                    lib_strcpy(out, "usage: ssh boot seal <password>");
                    return;
                }
                if (sshboot_password_obfuscate(password, enc, sizeof(enc)) != 0) {
                    lib_strcpy(out, "ERR: password too long.");
                    return;
                }
                if (ssh_client_get_target(target, sizeof(target)) != 0 || target[0] == '\0') {
                    lib_strcpy(target, "root@192.168.0.152:2221");
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
                    lib_strcpy(out, "OK: wrote obfuscated ~/.sshboot and loaded it.");
                } else {
                    lib_strcpy(out, "ERR: cannot write ~/.sshboot.");
                }
            } else if (strcmp(sub, "apply") == 0) {
                terminal_load_sshboot(w);
                lib_strcpy(out, "OK: loaded ~/.sshboot.");
            } else {
                lib_strcpy(out, "usage: ssh boot init | ssh boot seal <password> | ssh boot apply");
            }
        } else if (strncmp(arg, "exec ", 5) == 0) {
            char *remote = arg + 5;
            while (*remote == ' ') remote++;
            if (*remote == '\0') {
                lib_strcpy(out, "ERR: ssh exec <command>.");
            } else {
                ssh_client_exec_remote(remote, out, OUT_BUF_SIZE);
            }
        } else {
            lib_strcpy(out, "ERR: ssh status | ssh diag | ssh probe <host[:port]> | ssh set <user@host[:port]> | ssh auth [password] | ssh boot init|seal|apply | ssh exec <cmd>");
        }
    } else if (strncmp(cmd, "wrp", 3) == 0 && (cmd[3] == '\0' || cmd[3] == ' ')) {
        char *arg = cmd + 3;
        while (*arg == ' ') arg++;
        if (*arg == 'h' && (*(arg+1) == '\0' || *(arg+1) == ' ')) {
            lib_strcpy(out, "usage: wrp status | wrp set <http://host:8080> | wrp open");
            return;
        }
        if (*arg == '\0' || strcmp(arg, "status") == 0) {
            char wrp_url[160];
            out[0] = '\0';
            lib_strcat(out, "wrp: ");
            if (ssh_client_get_wrp_url(wrp_url, sizeof(wrp_url)) == 0) {
                lib_strcat(out, wrp_url);
            } else {
                lib_strcat(out, "(unset)");
            }
        } else if (strncmp(arg, "set ", 4) == 0) {
            char *url = arg + 4;
            while (*url == ' ') url++;
            if (ssh_client_set_wrp_url(url, out, OUT_BUF_SIZE) < 0 && out[0] == '\0') {
                lib_strcpy(out, "ERR: wrp set <http://host:8080>.");
            }
        } else if (strcmp(arg, "open") == 0) {
            char wrp_url[160];
            if (ssh_client_get_wrp_url(wrp_url, sizeof(wrp_url)) < 0) {
                lib_strcpy(out, "ERR: wrp url not set. Use wrp set <http://host:8080>.");
            } else {
                char wcmd[208];
                lib_strcpy(wcmd, "netsurf ");
                lib_strcat(wcmd, wrp_url);
                exec_single_cmd(w, wcmd);
                lib_strcpy(out, ">> WRP Opened.");
            }
        } else {
            lib_strcpy(out, "ERR: wrp status | wrp set <http://host:8080> | wrp open");
        }
    } else if (strncmp(cmd, "mkdir ", 6) == 0) {
        char *path = cmd + 6; while (*path == ' ') path++;
        if (strcmp(path, "h") == 0) { lib_strcpy(out, "usage: mkdir <path>"); return; }
        if (check_dir_and_list(w, path, out)) return;
        char expanded[128]; uint32_t p_bno = 0; char p_cwd[128], leaf[20];
        terminal_expand_home_path(w, path, expanded, sizeof(expanded));
        if (resolve_editor_target(w, expanded, &p_bno, p_cwd, leaf) != 0 || leaf[0] == '\0') { lib_strcpy(out, "ERR: Invalid Path."); return; }
        extern struct Window fs_tmp_window; struct Window *ctx = &fs_tmp_window;
        memset(ctx, 0, sizeof(*ctx)); ctx->cwd_bno = p_bno;
        struct dir_block *db = load_current_dir(ctx);
        if (!db) { lib_strcpy(out, "ERR: Run 'format'."); return; }
        if (find_entry_index(db, leaf, -1) != -1) { lib_strcpy(out, "ERR: Already Exists."); return; }
        for(int i=0; i<MAX_DIR_ENTRIES; i++) if(db->entries[i].name[0]==0) {
            uint32_t nb = balloc(); memset(&db->entries[i], 0, sizeof(db->entries[i]));
            copy_name20(db->entries[i].name, leaf); db->entries[i].bno = nb; db->entries[i].type = 1; db->entries[i].mode = 7;
            db->entries[i].ctime = db->entries[i].mtime = current_fs_time();
            virtio_disk_rw(&ctx->local_b, 1);
            struct blk nb_blk; memset(&nb_blk, 0, sizeof(nb_blk)); nb_blk.blockno = nb;
            struct dir_block *nd = (struct dir_block *)nb_blk.data; nd->magic = FS_MAGIC; nd->parent_bno = p_bno;
            virtio_disk_rw(&nb_blk, 1); lib_strcpy(out, ">> Directory Created."); return;
        }
        lib_strcpy(out, "ERR: Dir Full.");
    } else if (strncmp(cmd, "rm ", 3) == 0) {
        char *target = cmd + 3; while (*target == ' ') target++;
        if (strcmp(target, "h") == 0) { lib_strcpy(out, "usage: rm <path>"); return; }
        if (check_dir_and_list(w, target, out)) return;
        if (remove_entry_named(w, target) == 0) lib_strcpy(out, ">> Removed."); else lib_strcpy(out, "ERR: Remove Failed.");
    } else if (strncmp(cmd, "cp", 2) == 0 && (cmd[2] == '\0' || cmd[2] == ' ')) {
        char *args = cmd + 2;
        char *src;
        char *dst;
        char *extra;
        int recursive = 0;
        while (*args == ' ') args++;
        if (strncmp(args, "-r", 2) == 0 && (args[2] == '\0' || args[2] == ' ')) {
            recursive = 1;
            args += 2;
            while (*args == ' ') args++;
        }
        if (*args == '\0' || strcmp(args, "h") == 0) {
            lib_strcpy(out, "usage: cp [-r] <src> <dst>");
            return;
        }
        src = args;
        while (*args && *args != ' ') args++;
        if (*args == '\0') {
            lib_strcpy(out, "usage: cp [-r] <src> <dst>");
            return;
        }
        *args++ = '\0';
        while (*args == ' ') args++;
        if (*args == '\0') {
            lib_strcpy(out, "usage: cp [-r] <src> <dst>");
            return;
        }
        dst = args;
        extra = strchr(dst, ' ');
        if (extra) {
            *extra++ = '\0';
            while (*extra == ' ') extra++;
            if (*extra != '\0') {
                lib_strcpy(out, "usage: cp [-r] <src> <dst>");
                return;
            }
        }
        if (recursive) {
            int src_type = -1, dst_type = -1;
            uint32_t src_bno = 0;
            char src_name[20];
            char final_dst[128];
            int dst_is_curdir = (strcmp(dst, ".") == 0 || strcmp(dst, "./") == 0);
            if (path_is_sftp(src) && !path_is_sftp(dst)) {
                const char *src_remote = sftp_subpath(src);
                if (!src_remote || !*src_remote) {
                    lib_strcpy(out, "ERR: Source dir not found.");
                    return;
                }
                copy_last_path_segment(src_name, src_remote, "copy");
                if (dst_is_curdir || (local_path_info(w, dst, &dst_type, NULL, NULL) == 0 && dst_type == 1)) {
                    if (dst_is_curdir) lib_strcpy(final_dst, ".");
                    else lib_strcpy(final_dst, dst);
                    if (final_dst[0] && strcmp(final_dst, ".") != 0 && final_dst[strlen(final_dst) - 1] != '/') lib_strcat(final_dst, "/");
                    else if (strcmp(final_dst, ".") == 0) lib_strcat(final_dst, "/");
                    lib_strcat(final_dst, src_name[0] ? src_name : "copy");
                } else {
                    lib_strcpy(final_dst, dst);
                }
                if (copy_sftp_dir_to_local_recursive(w, src_remote, final_dst, out, OUT_BUF_SIZE) == 0) {
                    lib_strcpy(out, ">> Copied.");
                }
            } else {
                if (path_is_sftp(dst)) {
                    lib_strcpy(out, "ERR: cp -r local->SFTP not supported yet.");
                    return;
                }
                if (local_path_info(w, src, &src_type, &src_bno, src_name) != 0 || src_type != 1) {
                    lib_strcpy(out, "ERR: Source dir not found.");
                    return;
                }
                if (dst_is_curdir || (local_path_info(w, dst, &dst_type, NULL, NULL) == 0 && dst_type == 1)) {
                    if (dst_is_curdir) lib_strcpy(final_dst, ".");
                    else lib_strcpy(final_dst, dst);
                    if (final_dst[0] && strcmp(final_dst, ".") != 0 && final_dst[strlen(final_dst) - 1] != '/') lib_strcat(final_dst, "/");
                    else if (strcmp(final_dst, ".") == 0) lib_strcat(final_dst, "/");
                    lib_strcat(final_dst, src_name[0] ? src_name : path_basename(src));
                } else {
                    lib_strcpy(final_dst, dst);
                }
                if (copy_local_dir_recursive(w, src_bno, final_dst, out, OUT_BUF_SIZE) == 0) {
                    lib_strcpy(out, ">> Copied.");
                }
            }
        } else {
            copy_between_paths(w, src, dst, out, OUT_BUF_SIZE);
        }
    } else if ((strncmp(cmd, "mv", 2) == 0 && (cmd[2] == '\0' || cmd[2] == ' ')) ||
               (strncmp(cmd, "rename", 6) == 0 && (cmd[6] == '\0' || cmd[6] == ' '))) {
        char *args = (cmd[0] == 'm' && cmd[1] == 'v') ? cmd + 2 : cmd + 6;
        char *src;
        char *dst;
        char *extra;
        int rc;

        while (*args == ' ') args++;
        if (*args == '\0' || strcmp(args, "h") == 0) {
            lib_strcpy(out, "usage: mv <src> <dst>");
            return;
        }

        src = args;
        while (*args && *args != ' ') args++;
        if (*args == '\0') {
            lib_strcpy(out, "usage: mv <src> <dst>");
            return;
        }
        *args++ = '\0';
        while (*args == ' ') args++;
        if (*args == '\0') {
            lib_strcpy(out, "usage: mv <src> <dst>");
            return;
        }

        dst = args;
        extra = strchr(dst, ' ');
        if (extra) {
            *extra++ = '\0';
            while (*extra == ' ') extra++;
            if (*extra != '\0') {
                lib_strcpy(out, "usage: mv <src> <dst>");
                return;
            }
        }

        if (!path_is_sftp(src) && !path_is_sftp(dst)) {
            rc = move_entry_named(w, src, dst);
            if (rc == 0) lib_strcpy(out, ">> Moved.");
            else if (rc == -3) lib_strcpy(out, "ERR: Source Not Found.");
            else if (rc == -4) lib_strcpy(out, "ERR: Destination Exists.");
            else if (rc == -5) lib_strcpy(out, "ERR: Dir Full.");
            else if (rc == -6) lib_strcpy(out, "ERR: Invalid Destination.");
            else lib_strcpy(out, "ERR: Move Failed.");
        } else if (path_is_sftp(src) && path_is_sftp(dst)) {
            if (ssh_client_sftp_rename(sftp_subpath(src), sftp_subpath(dst), out, OUT_BUF_SIZE) != 0) {
                copy_between_paths(w, src, dst, out, OUT_BUF_SIZE);
                if (out[0] && strncmp(out, "ERR:", 4) != 0) ssh_client_sftp_unlink(sftp_subpath(src), out, OUT_BUF_SIZE);
            }
        } else {
            if (copy_between_paths(w, src, dst, out, OUT_BUF_SIZE) == 0) {
                if (path_is_sftp(src)) ssh_client_sftp_unlink(sftp_subpath(src), out, OUT_BUF_SIZE);
                else if (remove_entry_named(w, src) == 0) lib_strcpy(out, ">> Moved.");
                else lib_strcpy(out, "ERR: Remove Source Failed.");
            }
        }
    } else if (strncmp(cmd, "touch ", 6) == 0) {
        char *fn = cmd + 6; while (*fn == ' ') fn++;
        if (strcmp(fn, "h") == 0) { lib_strcpy(out, "usage: touch <path>"); return; }
        if (check_dir_and_list(w, fn, out)) return;
        if (store_file_bytes(w, fn, (const unsigned char *)"", 0) == 0) lib_strcpy(out, ">> Touched."); else lib_strcpy(out, "ERR: Touch Failed.");
    } else if (strncmp(cmd, "write ", 6) == 0) {
        char *fn = cmd + 6; char *data = strchr(fn, ' ');
        if (data) { 
            *data++ = '\0'; while (*data == ' ') data++;
            if (strcmp(fn, "h") == 0) { lib_strcpy(out, "usage: write <path> <text>"); return; }
            if (check_dir_and_list(w, fn, out)) return;
            if (store_file_bytes(w, fn, (const unsigned char *)data, strlen(data)) == 0) lib_strcpy(out, ">> Written."); else lib_strcpy(out, "ERR: Write Failed."); 
        } else lib_strcpy(out, "usage: write <path> <text>");
    } else if (strncmp(cmd, "cat ", 4) == 0) {
        char *fn = cmd + 4; while (*fn == ' ') fn++;
        if (strcmp(fn, "h") == 0) { lib_strcpy(out, "usage: cat <path>"); return; }
        if (path_is_sftp(fn)) {
            unsigned char *remote_buf = NULL;
            uint32_t remote_size = 0;
            char msg[OUT_BUF_SIZE];
            if (ssh_client_sftp_read_alloc(sftp_subpath(fn), &remote_buf, &remote_size, msg, sizeof(msg)) == 0 && remote_buf) {
                uint32_t n = remote_size;
                if (n >= OUT_BUF_SIZE) n = OUT_BUF_SIZE - 1;
                memcpy(out, remote_buf, n);
                out[n] = '\0';
                free(remote_buf);
            } else {
                lib_strcpy(out, msg[0] ? msg : "ERR: SFTP Cat Failed.");
            }
            return;
        }
        
        // Path resolution for smart behavior
        char expanded[128]; uint32_t p_bno = 0; char p_cwd[128], leaf[20];
        terminal_expand_home_path(w, fn, expanded, sizeof(expanded));
        if (resolve_editor_target(w, expanded, &p_bno, p_cwd, leaf) == 0) {
            if (leaf[0] == '\0' || strcmp(leaf, ".") == 0) {
                list_dir_contents(p_bno, out); return;
            } else {
                extern struct Window fs_tmp_window; struct Window *ltmp = &fs_tmp_window;
                memset(ltmp, 0, sizeof(*ltmp)); ltmp->cwd_bno = p_bno;
                struct dir_block *db = load_current_dir(ltmp);
                int idx = find_entry_index(db, leaf, -1);
                if (idx >= 0 && db->entries[idx].type == 1) {
                    list_dir_contents(db->entries[idx].bno, out); return;
                }
            }
        }
        
        uint32_t sz; if (load_file_bytes(w, fn, (unsigned char *)out, OUT_BUF_SIZE - 1, &sz) == 0) out[sz] = '\0'; else lib_strcpy(out, "ERR: Cat Failed.");
    } else if (strncmp(cmd, "wget status", 11) == 0) {
        if (wget_job.request_pending || wget_job.active) {
            lib_strcpy(out, "wget: ");
            lib_strcat(out, wget_stage_name());
            lib_strcat(out, " ");
            char sz[12];
            lib_itoa(wget_job.body_len, sz);
            lib_strcat(out, sz);
            lib_strcat(out, "B");
            if (wget_job.wait_started_ms != 0) {
                lib_strcat(out, " ");
                char ms[16];
                lib_itoa((uint32_t)(sys_now() - wget_job.wait_started_ms), ms);
                lib_strcat(out, ms);
                lib_strcat(out, "ms");
            }
        } else if (wget_job.done && wget_job.success) {
            lib_strcpy(out, "wget: done ");
            lib_strcat(out, wget_job.filename);
            lib_strcat(out, " ");
            char sz[16];
            format_size_human(wget_job.body_len, sz);
            lib_strcat(out, sz);
        } else if (wget_job.done) {
            lib_strcpy(out, "wget: fail ");
            lib_strcat(out, wget_job.err[0] ? wget_job.err : "-");
        } else {
            lib_strcpy(out, "wget: idle");
        }
    } else if (strncmp(cmd, "wget ", 5) == 0) {
        char *arg1 = cmd + 5;
        while (*arg1 == ' ') arg1++;
        if (*arg1 == 'h' && (*(arg1+1) == '\0' || *(arg1+1) == ' ')) {
            lib_strcpy(out, "usage: wget <ip> <path> <file>\n"
                            "       wget http(s)://host[:port]/path [file]");
            return;
        }
        char *arg2 = 0;
        char *arg3 = 0;
        uint32_t progress_row = (w->total_rows > 0) ? (uint32_t)(w->total_rows - 1) : 0;

        if (*arg1 == '\0') { lib_strcpy(out, "ERR: wget <ip> <path> <file>"); return; }
        arg2 = strchr(arg1, ' ');
        if (arg2) {
            *arg2++ = '\0';
            while (*arg2 == ' ') arg2++;
            if (*arg2 == '\0') arg2 = 0;
        }
        if (arg2) {
            arg3 = strchr(arg2, ' ');
            if (arg3) {
                *arg3++ = '\0';
                while (*arg3 == ' ') arg3++;
                if (*arg3 == '\0') arg3 = 0;
            }
        }
        if (wget_job.active || wget_job.request_pending) { lib_strcpy(out, "ERR: wget busy."); return; }

        if (str_starts_with(arg1, "http://") || str_starts_with(arg1, "https://")) {
            char host[32];
            char path[96];
            char file[20];
            uint16_t port = 0;
            int use_tls = str_starts_with(arg1, "https://");
            if (!parse_wget_url(arg1, host, &port, path)) { lib_strcpy(out, "ERR: wget <ip> <path> <file>"); return; }
            if (path[0] != '/') { lib_strcpy(out, "ERR: wget <ip> <path> <file>"); return; }
            if (arg2 && *arg2) copy_name20(file, arg2);
            else derive_filename_from_path(path, file, sizeof(file));
            if (file[0] == '\0') { lib_strcpy(out, "ERR: wget <ip> <path> <file>"); return; }
            if (strlen(file) > 19) { lib_strcpy(out, "ERR: File Name Too Long."); return; }
            wget_queue_request(host, path, file, port, use_tls);
        } else {
            char *host = arg1;
            char *path = arg2;
            char *name = arg3;
            if (!path || !name) { lib_strcpy(out, "ERR: wget <ip> <path> <file>"); return; }
            if (*host == '\0' || *path == '\0' || *name == '\0') { lib_strcpy(out, "ERR: wget <ip> <path> <file>"); return; }
            if (strlen(name) > 19) { lib_strcpy(out, "ERR: File Name Too Long."); return; }
            wget_queue_request(host, path, name, 80, 0);
        }

        wget_job.save_pending = 1;
        wget_job.owner_win_id = w->id;
        wget_job.progress_row = progress_row;
        wget_job.target_cwd_bno = w->cwd_bno;
        lib_strcpy(wget_job.target_cwd, w->cwd);
        w->waiting_wget = 1;
        set_wget_progress_line(w, progress_row);
        out[0] = '\0';
    } else if (strncmp(cmd, "demo3d", 6) == 0) {
        int rc;
        char *spec = cmd + 6;
        while (*spec == ' ') spec++;
        if (*spec) {
            if (strcmp(spec, "cube") == 0) {
                rc = open_demo3d_cube_window();
            } else {
            int pts[8][3];
            int count = 0;
            rc = parse_demo3d_points(spec, pts, &count);
            if (rc == 0) rc = open_demo3d_window_points(pts, count);
            else if (rc == -2) { lib_strcpy(out, "ERR: Max 8 Points."); return; }
            else { lib_strcpy(out, "ERR: demo3d x,y,z x,y,z ..."); return; }
            }
        } else {
            rc = open_demo3d_window();
        }
        if (rc == 0) lib_strcpy(out, ">> 3D Demo Opened.");
        else lib_strcpy(out, "ERR: No Free Window.");
    } else if (strncmp(cmd, "frankenstein", 12) == 0) {
        int rc = open_frankenstein_window();
        if (rc == 0) lib_strcpy(out, ">> Frankenstein Opened.");
        else lib_strcpy(out, "ERR: No Free Window.");
    } else if (strncmp(cmd, "open ", 5) == 0) {
        char *fn = cmd + 5;
        while (*fn == ' ') fn++;
        if (*fn == '\0') { lib_strcpy(out, "ERR: Missing File."); return; }
        int rc = open_image_file(w, fn);
        if (rc == 0) lib_strcpy(out, ">> Image Opened.");
        else if (rc == -2) lib_strcpy(out, "ERR: Only 24-bit BMP.");
        else if (rc == -3) lib_strcpy(out, "ERR: No Free Window.");
        else lib_strcpy(out, "ERR: Open Failed.");
    } else if (strncmp(cmd, "run ", 4) == 0) {
        char *fn = cmd + 4;
        char *args = 0;
        while (*fn == ' ') fn++;
        if (*fn == '\0') { lib_strcpy(out, "ERR: Missing File."); return; }
        args = fn;
        while (*args && *args != ' ') args++;
        if (*args == ' ') {
            *args++ = '\0';
            while (*args == ' ') args++;
            if (*args == '\0') args = 0;
        } else {
            args = 0;
        }
        if (launch_app_file(w, fn, args, out, OUT_BUF_SIZE) != 0) return;
    } else if (strncmp(cmd, "gbemu", 5) == 0 && (cmd[5] == '\0' || cmd[5] == ' ')) {
        char *path = cmd + 5;
        char *rom = 0;
        while (*path == ' ') path++;
        if (*path == '\0') {
            rom = "lez.gb";
        } else {
            rom = path;
        }
        if (strlen(rom) < 3 || (strcmp(rom + strlen(rom) - 3, ".gb") != 0 && strcmp(rom + strlen(rom) - 4, ".gbc") != 0)) {
            lib_strcpy(out, "ERR: gbemu <rom.gb|rom.gbc>");
            return;
        }
        int rc = open_gbemu_window(w, rom);
        if (rc == 0) lib_strcpy(out, ">> GBEMU Opened.");
        else if (rc == -3) lib_strcpy(out, "ERR: No Free Window.");
        else if (rc == -2) lib_strcpy(out, "ERR: Out of memory.");
        else if (rc == -1) lib_strcpy(out, "ERR: Invalid ROM.");
        else lib_strcpy(out, "ERR: GBEMU Failed.");
    } else if (try_launch_elf_command(w, cmd, out)) {
        return;
    } else if (strncmp(cmd, "vim", 3) == 0 && (cmd[3] == '\0' || cmd[3] == ' ')) {
        char *fn = cmd + 3;
        while (*fn == ' ') fn++;
        if (*fn == '\0') {
            lib_strcpy(out, "ERR: Missing File.");
            return;
        }
        int rc = open_text_editor(w, fn);
        if (rc == 0) lib_strcpy(out, ">> Vim Opened.");
        else if (rc == -4) lib_strcpy(out, "ERR: File Too Big.");
        else if (rc == -5) lib_strcpy(out, "ERR: No Free Window.");
        else if (rc == -6) lib_strcpy(out, "ERR: Is Directory.");
        else lib_strcpy(out, "ERR: Vim Failed.");
    } else if (strncmp(cmd, "asm", 3) == 0 && (cmd[3] == '\0' || cmd[3] == ' ')) {
        char *fn = cmd + 3;
        while (*fn == ' ') fn++;
        if (*fn == '\0') {
            lib_strcpy(out, "ERR: asm <file>");
            return;
        }
        if (run_asm_file(w, fn) == 0) {
            lib_strcpy(out, w->out_buf);
        } else if (w->out_buf[0] == '\0') {
            lib_strcpy(out, "ERR: asm failed.");
        } else {
            lib_strcpy(out, w->out_buf);
        }
    } else if (strncmp(cmd, "netsurf", 7) == 0) {
        char *url = cmd + 7; while (*url == ' ') url++;
        char launch_url[512];
        char clean_url[256];
        if (*url == '\0') url = "https://192.168.123.100/test.html";
        netsurf_normalize_target_url(url, clean_url, sizeof(clean_url));
        if (clean_url[0] == '\0' || netsurf_prepare_launch_url(clean_url, 400, 420, launch_url, sizeof(launch_url)) < 0) {
            lib_strcpy(out, "ERR: Unable to open NetSurf URL.");
            return;
        }
        int nid = -1;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (!wins[i].active) {
                reset_window(&wins[i], i);
                wins[i].active = 1; wins[i].kind = WINDOW_KIND_NETSURF;
                wins[i].x = 50; wins[i].y = 50; wins[i].w = 400; wins[i].h = 420;
                lib_strcpy(wins[i].title, "NetSurf Browser");
                lib_strcpy(wins[i].ns_url, launch_url);
                lib_strcpy(wins[i].ns_target_url, clean_url);
                lib_strcpy(wins[i].ns_history[0], launch_url);
                wins[i].ns_history_count = 1; wins[i].ns_history_pos = 0;
                extern void netsurf_init_engine(struct Window *w);
                netsurf_init_engine(&wins[i]);
                extern void netsurf_begin_navigation(int win_id);
                netsurf_begin_navigation(i);
                extern void netsurf_invalidate_layout(int win_id);
                netsurf_invalidate_layout(i);
                bring_to_front(i); nid = i; break;
            }
        }
        if (nid != -1) {
            lib_strcpy(wins[nid].ns_url, launch_url);
            lib_strcpy(wins[nid].ns_target_url, clean_url);
            lib_strcpy(wins[nid].ns_history[0], launch_url);
            wins[nid].ns_history_count = 1;
            wins[nid].ns_history_pos = 0;
            extern void netsurf_begin_navigation(int win_id);
            netsurf_begin_navigation(nid);
            if (netsurf_queue_wrapped_request(clean_url, wins[nid].w, wins[nid].h, nid) < 0) {
                lib_strcpy(out, "ERR: Unable to open NetSurf URL.");
                return;
            }
        } else lib_strcpy(out, "ERR: No Free Window.");
    } else { lib_strcpy(out, "Invalid signal."); }
}

static const struct command_handler command_handlers[] = {
    { "mem", cmd_handle_mem },
    { "ethstat", cmd_handle_ethstat },
    { "help", cmd_handle_help },
};

void exec_single_cmd(struct Window *w, char *cmd) {
    char *out = w->out_buf;
    out[0] = '\0';
    for (uint32_t i = 0; i < sizeof(command_handlers) / sizeof(command_handlers[0]); i++) {
        if (command_handlers[i].run(w, cmd, out)) return;
    }
    exec_single_cmd_legacy(w, cmd);
}
