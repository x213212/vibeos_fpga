#include "user_editor.h"
#include "user_internal.h"

static int load_file_entry_prefix(const struct file_entry *entry, unsigned char *dst, uint32_t max_size, uint32_t *out_size) {
    uint32_t remaining;
    uint32_t copied = 0;
    uint32_t bno;

    if (!entry || !dst || !out_size) return -1;
    remaining = entry->size;
    if (remaining > max_size) remaining = max_size;
    bno = entry->bno;

    while (remaining > 0 && bno != 0) {
        struct blk blkbuf;
        memset(&blkbuf, 0, sizeof(blkbuf));
        blkbuf.blockno = bno;
        virtio_disk_rw(&blkbuf, 0);

        struct data_block *dblk = (struct data_block *)blkbuf.data;
        if (dblk->magic == FS_DATA_MAGIC) {
            uint32_t chunk = remaining;
            if (chunk > FS_DATA_PAYLOAD) chunk = FS_DATA_PAYLOAD;
            memcpy(dst + copied, dblk->data, chunk);
            copied += chunk;
            remaining -= chunk;
            bno = dblk->next_bno;
        } else {
            uint32_t chunk = remaining;
            if (chunk > BSIZE) chunk = BSIZE;
            memcpy(dst + copied, blkbuf.data, chunk);
            copied += chunk;
            remaining -= chunk;
            bno = 0;
        }
    }

    *out_size = copied;
    return (entry->size > copied) ? 1 : 0;
}

int editor_load_file_window(struct Window *w, uint32_t start_line) {
    uint32_t bno;
    uint32_t remaining;
    uint32_t line_no = 0;
    int row = 0;
    int col = 0;
    int in_window = 0;

    if (!w || w->editor_file_bno == 0) return -1;
    memset(w->editor_lines, 0, sizeof(w->editor_lines));

    bno = w->editor_file_bno;
    remaining = w->editor_file_size;
    while (remaining > 0 && bno != 0 && row < EDITOR_MAX_LINES) {
        struct blk blkbuf;
        uint32_t chunk;
        uint32_t next_bno;
        unsigned char *data;

        memset(&blkbuf, 0, sizeof(blkbuf));
        blkbuf.blockno = bno;
        virtio_disk_rw(&blkbuf, 0);

        struct data_block *dblk = (struct data_block *)blkbuf.data;
        if (dblk->magic == FS_DATA_MAGIC) {
            chunk = remaining;
            if (chunk > FS_DATA_PAYLOAD) chunk = FS_DATA_PAYLOAD;
            data = dblk->data;
            next_bno = dblk->next_bno;
        } else {
            chunk = remaining;
            if (chunk > BSIZE) chunk = BSIZE;
            data = blkbuf.data;
            next_bno = 0;
        }

        for (uint32_t i = 0; i < chunk && row < EDITOR_MAX_LINES; i++) {
            unsigned char ch = data[i];
            if (ch == '\r') continue;
            in_window = (line_no >= start_line);
            if (ch == '\n') {
                if (in_window) {
                    w->editor_lines[row][col] = '\0';
                    row++;
                    col = 0;
                }
                line_no++;
                continue;
            }
            if (in_window && col < EDITOR_LINE_LEN - 1) {
                w->editor_lines[row][col++] = (char)ch;
                w->editor_lines[row][col] = '\0';
            }
        }

        remaining -= chunk;
        bno = next_bno;
    }

    if (row < EDITOR_MAX_LINES) {
        if (col > 0 || line_no >= start_line) {
            w->editor_lines[row][col] = '\0';
            row++;
        }
    }
    if (row < 1) {
        row = 1;
        w->editor_lines[0][0] = '\0';
    }

    w->editor_line_count = row;
    w->editor_loaded_start_line = start_line;
    w->editor_loaded_complete = (remaining == 0 || bno == 0);
    w->editor_cursor_row = 0;
    w->editor_cursor_col = 0;
    w->editor_scroll_row = 0;
    w->editor_mode = 0;
    w->editor_cmd_len = 0;
    w->editor_cmd_cursor = 0;
    w->editor_dirty = 0;
    w->editor_pending_d = 0;
    w->editor_readonly = 1;
    editor_set_status(w, "Large file: lazy read-only view");
    return 0;
}


int open_text_editor(struct Window *term, const char *name) {
    uint32_t size = 0;
    int rc = -1;
    char display_name[64];
    char requested[64];
    char absolute_path[128];
    uint32_t dir_bno = term ? term->cwd_bno : 1;
    char cwd_copy[128];
    char leaf[32];
    extern struct Window fs_tmp_window;
    struct Window *load_ctx = &fs_tmp_window;
    struct dir_block *db;
    int idx;
    int lazy_view = 0;
    uint32_t file_bno = 0;
    uint32_t file_size = 0;

    if (!term) return -1;
    if (!name || name[0] == '\0') return -1;

    // Clear all local buffers
    memset(leaf, 0, sizeof(leaf));
    memset(absolute_path, 0, sizeof(absolute_path));
    memset(display_name, 0, sizeof(display_name));
    memset(requested, 0, sizeof(requested));
    memset(cwd_copy, 0, sizeof(cwd_copy));
    
    strncpy(requested, name, sizeof(requested)-1);

    if (resolve_editor_target(term, requested, &dir_bno, cwd_copy, leaf) != 0) return -1;
    if (leaf[0] == '\0' || strcmp(leaf, ".") == 0) return -6;

    memset(load_ctx, 0, sizeof(*load_ctx));
    load_ctx->cwd_bno = dir_bno ? dir_bno : 1;
    lib_strcpy(load_ctx->cwd, cwd_copy[0] ? cwd_copy : "/root");
    db = load_current_dir(load_ctx);
    if (!db) return -1;

    idx = find_entry_index(db, leaf, -1);
    if (idx >= 0 && db->entries[idx].type == 1) return -6;
    if (idx >= 0) {
        uint32_t eager_limit = EDITOR_MAX_LINES * (EDITOR_LINE_LEN - 1);
        file_bno = db->entries[idx].bno;
        file_size = db->entries[idx].size;
        if (db->entries[idx].size > eager_limit) {
            lazy_view = 1;
            rc = 0;
        } else {
            rc = load_file_bytes(load_ctx, leaf, file_io_buf, sizeof(file_io_buf), &size);
        }
    }
    else rc = -2;

    build_editor_path(absolute_path, cwd_copy, leaf);
    strncpy(display_name, leaf, sizeof(display_name)-1);
    
    if (rc == -3) return -4;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (wins[i].active) continue;
        reset_window(&wins[i], i);
        wins[i].active = 1;
        wins[i].kind = WINDOW_KIND_EDITOR;
        wins[i].maximized = 1;
        wins[i].x = 0; wins[i].y = 0; wins[i].w = WIDTH; wins[i].h = DESKTOP_H;
        
        strncpy(wins[i].editor_name, display_name, 19);
        lib_strcpy(wins[i].editor_path, absolute_path);
        lib_strcpy(wins[i].editor_cwd, cwd_copy[0] ? cwd_copy : "/root");
        wins[i].cwd_bno = dir_bno ? dir_bno : 1;
        wins[i].editor_file_bno = file_bno;
        wins[i].editor_file_size = file_size;
        wins[i].editor_lazy = lazy_view;
        
        if (lazy_view) editor_load_file_window(&wins[i], 0);
        else if (rc == 0) editor_load_bytes(&wins[i], file_io_buf, size);
        else editor_clear(&wins[i]);
        wins[i].editor_readonly = lazy_view;
        
        memset(wins[i].title, 0, sizeof(wins[i].title));
        lib_strcpy(wins[i].title, "Vim: ");
        shorten_path_for_title(wins[i].title + 5, wins[i].editor_path, 55);
        
        if (lazy_view) editor_set_status(&wins[i], "Large file: lazy read-only view");
        else editor_set_status(&wins[i], "i=insert  :wq=save+quit  :q=quit");
        bring_to_front(i);
        return 0;
    }
    return -5;
}
