#include "user.h"

extern struct Window wins[MAX_WINDOWS];

#ifndef FS_MAX_BLOCKS
#define FS_MAX_BLOCKS 65536u
#endif

static uint32_t app_fs_cwd_bno = 1;
static char app_fs_cwd[32] = "/root";
static struct Window app_fs_window;
struct Window fs_tmp_window;
static int fs_alloc_ready;
static uint32_t next_free_block = 60;
static uint8_t fs_block_map[FS_MAX_BLOCKS / 8];

#define APP_MAX_FDS 16
#define APP_O_READ   0x01
#define APP_O_WRITE  0x02
#define APP_O_CREAT  0x100
#define APP_O_TRUNC  0x200

struct app_fd_entry {
    int used;
    int flags;
    uint32_t pos;
    uint32_t size;
    uint32_t cap;
    unsigned char *buf;
    uint32_t cwd_bno;
    char name[20];
    char cwd[32];
};

static struct app_fd_entry app_fd_table[APP_MAX_FDS];

static inline int fs_block_is_used(uint32_t bno) {
    if (bno >= FS_MAX_BLOCKS) return 1;
    return (fs_block_map[bno >> 3] >> (bno & 7)) & 1u;
}

static inline void fs_block_set_used(uint32_t bno) {
    if (bno >= FS_MAX_BLOCKS) return;
    fs_block_map[bno >> 3] |= (uint8_t)(1u << (bno & 7));
}

static inline void fs_block_clear_used(uint32_t bno) {
    if (bno >= FS_MAX_BLOCKS) return;
    fs_block_map[bno >> 3] &= (uint8_t)~(1u << (bno & 7));
}

static void fs_scan_dir(uint32_t bno, int depth);

static void fs_path_set_child(char *cwd, const char *name) {
    int len = strlen(cwd);
    if (len > 1 && cwd[len - 1] != '/') {
        lib_strcat(cwd, "/");
    }
    lib_strncat(cwd, name, 19);
}

static void fs_path_set_parent(char *cwd) {
    int len = strlen(cwd);
    if (len <= 1) {
        lib_strcpy(cwd, "/");
        return;
    }
    while (len > 1 && cwd[len - 1] == '/') {
        cwd[--len] = '\0';
    }
    while (len > 1 && cwd[len - 1] != '/') {
        cwd[--len] = '\0';
    }
    if (len > 1) {
        cwd[len - 1] = '\0';
    } else {
        cwd[0] = '/';
        cwd[1] = '\0';
    }
}

int resolve_fs_target(struct Window *term, const char *input, uint32_t *dir_bno_out, char *cwd_out, char *leaf_out) {
    struct Window *tmp = &fs_tmp_window;
    char expanded[64];
    const char *p;
    char seg[20];
    int seg_len = 0;

    if (!term || !input || !dir_bno_out || !cwd_out || !leaf_out) return -1;

    terminal_expand_home_path(term, input, expanded, sizeof(expanded));
    input = expanded;

    memset(tmp, 0, sizeof(*tmp));
    tmp->cwd_bno = term->cwd_bno ? term->cwd_bno : 1;
    if (term->cwd[0]) lib_strcpy(tmp->cwd, term->cwd);
    else lib_strcpy(tmp->cwd, "/root");

    p = input;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') return -1;

    if (*p == '/') {
        tmp->cwd_bno = 1;
        lib_strcpy(tmp->cwd, "/root");
        while (*p == '/') p++;
    }
    
    // Normalize: if the first segment is 'root', skip it and resolve from Block 1
    if (strncmp(p, "root/", 5) == 0) {
        tmp->cwd_bno = 1;
        lib_strcpy(tmp->cwd, "/root");
        p += 5;
    } else if (strcmp(p, "root") == 0) {
        tmp->cwd_bno = 1;
        lib_strcpy(tmp->cwd, "/root");
        p += 4;
    }

    while (*p == '/') p++;

    while (*p) {
        seg_len = 0;
        while (p[seg_len] && p[seg_len] != '/') {
            if (seg_len >= (int)sizeof(seg) - 1) return -1;
            seg[seg_len] = p[seg_len];
            seg_len++;
        }
        seg[seg_len] = '\0';
        p += seg_len;
        while (*p == '/') p++;
        if (seg_len == 0 || strcmp(seg, ".") == 0) continue;
        if (strcmp(seg, "..") == 0) {
            struct dir_block *db = load_current_dir(tmp);
            if (!db) return -1;
            tmp->cwd_bno = db->parent_bno ? db->parent_bno : 1;
            if (tmp->cwd_bno == 1) lib_strcpy(tmp->cwd, "/root");
            else fs_path_set_parent(tmp->cwd);
            continue;
        }
        if (*p == '\0') {
            copy_name20(leaf_out, seg);
            *dir_bno_out = tmp->cwd_bno ? tmp->cwd_bno : 1;
            lib_strcpy(cwd_out, tmp->cwd[0] ? tmp->cwd : "/root");
            return 0;
        }
        struct dir_block *db = load_current_dir(tmp);
        if (!db) return -1;
        int idx = find_entry_index(db, seg, 1);
        if (idx == -1) return -1;
        tmp->cwd_bno = db->entries[idx].bno;
        fs_path_set_child(tmp->cwd, db->entries[idx].name);
    }

    *dir_bno_out = tmp->cwd_bno ? tmp->cwd_bno : 1;
    lib_strcpy(cwd_out, tmp->cwd[0] ? tmp->cwd : "/root");
    copy_name20(leaf_out, input);
    return 0;
}

static int fs_load_dir_block(uint32_t bno, struct blk *blkbuf, struct dir_block **db_out) {
    if (!blkbuf || !db_out || bno == 0 || bno >= FS_MAX_BLOCKS) return -1;
    memset(blkbuf, 0, sizeof(*blkbuf));
    blkbuf->blockno = bno;
    virtio_disk_rw(blkbuf, 0);
    *db_out = (struct dir_block *)blkbuf->data;
    if ((*db_out)->magic != FS_MAGIC) return -1;
    return 0;
}

struct dir_block *load_current_dir(struct Window *w) {
    if (!fs_alloc_ready) {
        fs_rebuild_alloc_state();
    }
    w->local_b.blockno = w->cwd_bno;
    virtio_disk_rw(&w->local_b, 0);
    struct dir_block *db = (struct dir_block *)w->local_b.data;
    if (db->magic != FS_MAGIC) {
        return 0;
    }
    return db;
}

int find_free_entry_index(struct dir_block *db) {
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (db->entries[i].name[0] == 0) return i;
    }
    return -1;
}

uint32_t balloc(void) {
    if (!fs_alloc_ready) fs_rebuild_alloc_state();
    for (uint32_t i = next_free_block; i < FS_MAX_BLOCKS; i++) {
        if (!(fs_block_map[i >> 3] & (1u << (i & 7)))) {
            fs_block_map[i >> 3] |= (uint8_t)(1u << (i & 7));
            next_free_block = i + 1;
            struct blk b;
            memset(&b, 0, sizeof(b));
            b.blockno = i;
            virtio_disk_rw(&b, 1);
            return i;
        }
    }
    return 0;
}

void fs_usage_info(uint32_t *total_blocks, uint32_t *used_blocks, uint32_t *free_blocks) {
    uint32_t used = 0;
    if (!fs_alloc_ready) fs_rebuild_alloc_state();
    for (uint32_t i = 0; i < FS_MAX_BLOCKS; i++) {
        if (fs_block_map[i >> 3] & (1u << (i & 7))) {
            used++;
        }
    }
    if (total_blocks) *total_blocks = FS_MAX_BLOCKS;
    if (used_blocks) *used_blocks = used;
    if (free_blocks) *free_blocks = FS_MAX_BLOCKS - used;
}

void bfree(uint32_t bno) {
    if (bno < 60 || bno >= FS_MAX_BLOCKS) return;
    fs_block_map[bno >> 3] &= (uint8_t)~(1u << (bno & 7));
    if (bno < next_free_block) next_free_block = bno;
}

void fs_free_chain(uint32_t bno) {
    while (bno != 0) {
        struct blk b;
        b.blockno = bno;
        virtio_disk_rw(&b, 0);
        struct data_block *d = (struct data_block *)b.data;
        uint32_t next = (d->magic == FS_DATA_MAGIC) ? d->next_bno : 0;
        bfree(bno);
        bno = next;
    }
}

void fs_reset_window_cwd(struct Window *w) {
    if (!w) return;
    w->cwd_bno = 1;
    lib_strcpy(w->cwd, "/root");
    memset(&w->local_b, 0, sizeof(w->local_b));
}

void fs_format_root(struct Window *owner) {
    memset(owner->local_b.data, 0, BSIZE);
    struct dir_block *rb = (struct dir_block *)owner->local_b.data;
    rb->magic = FS_MAGIC;
    rb->parent_bno = 1;
    owner->local_b.blockno = 1;
    virtio_disk_rw(&owner->local_b, 1);
    memset(fs_block_map, 0, sizeof(fs_block_map));
    for (uint32_t i = 0; i < 60; i++) {
        fs_block_map[i >> 3] |= (uint8_t)(1u << (i & 7));
    }
    next_free_block = 60;
    fs_alloc_ready = 1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!wins[i].active) continue;
        fs_reset_window_cwd(&wins[i]);
        if (wins[i].kind == WINDOW_KIND_TERMINAL) {
            terminal_env_sync_pwd(&wins[i]);
        }
    }
}

static int dir_block_is_empty(uint32_t bno) {
    struct blk blkbuf;
    memset(&blkbuf, 0, sizeof(blkbuf));
    blkbuf.blockno = bno;
    virtio_disk_rw(&blkbuf, 0);
    struct dir_block *db = (struct dir_block *)blkbuf.data;
    if (db->magic != FS_MAGIC) return 0;
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (db->entries[i].name[0]) return 0;
    }
    return 1;
}

int remove_entry_named(struct Window *w, const char *name) {
    struct dir_block *db = load_current_dir(w);
    if (!db) return -1;
    int idx = find_entry_index(db, name, -1);
    if (idx == -1) return -2;
    struct file_entry old = db->entries[idx];
    if (old.type == 1 && !dir_block_is_empty(old.bno)) return -3;
    memset(&db->entries[idx], 0, sizeof(db->entries[idx]));
    virtio_disk_rw(&w->local_b, 1);
    if (old.type == 1) {
        if (old.bno != 0) bfree(old.bno);
    } else if (old.bno != 0) {
        fs_free_chain(old.bno);
    }
    return 0;
}

int appfs_unlink(const char *path) {
    uint32_t dir_bno = 0;
    char cwd_buf[32];
    char leaf[20];
    struct Window *ctx = &app_fs_window;

    if (!path || !*path) return -1;
    memset(ctx, 0, sizeof(*ctx));
    ctx->cwd_bno = app_fs_cwd_bno ? app_fs_cwd_bno : 1;
    lib_strcpy(ctx->cwd, app_fs_cwd[0] ? app_fs_cwd : "/root");

    if (resolve_fs_target(ctx, path, &dir_bno, cwd_buf, leaf) != 0) return -1;
    if (leaf[0] == '\0' || strcmp(leaf, ".") == 0 || strcmp(leaf, "..") == 0) return -1;

    memset(ctx, 0, sizeof(*ctx));
    ctx->cwd_bno = dir_bno ? dir_bno : 1;
    lib_strcpy(ctx->cwd, cwd_buf[0] ? cwd_buf : "/root");
    return remove_entry_named(ctx, leaf);
}

static int fs_dir_is_ancestor(uint32_t ancestor_bno, uint32_t child_bno) {
    struct blk blkbuf;
    struct dir_block *db;
    int guard = 0;

    while (child_bno != 0 && guard++ < 64) {
        if (child_bno == ancestor_bno) return 1;
        if (child_bno == 1) return 0;
        if (fs_load_dir_block(child_bno, &blkbuf, &db) != 0) return 0;
        child_bno = db->parent_bno ? db->parent_bno : 1;
    }
    return 0;
}

int move_entry_named(struct Window *w, const char *src_input, const char *dst_input) {
    uint32_t src_dir_bno = 0, dst_dir_bno = 0;
    char src_cwd[32];
    char dst_cwd[32];
    char src_leaf[20];
    char dst_leaf[20];
    struct blk src_blk;
    struct blk dst_blk;
    struct blk child_blk;
    struct dir_block *src_db;
    struct dir_block *dst_db;
    struct file_entry src_entry;
    int src_idx;
    int dst_idx;

    if (!w || !src_input || !dst_input) return -1;
    while (*src_input == ' ') src_input++;
    while (*dst_input == ' ') dst_input++;
    if (*src_input == '\0' || *dst_input == '\0') return -1;
    if (resolve_fs_target(w, src_input, &src_dir_bno, src_cwd, src_leaf) != 0) return -1;
    if (resolve_fs_target(w, dst_input, &dst_dir_bno, dst_cwd, dst_leaf) != 0) return -1;
    if (src_dir_bno == dst_dir_bno && strcmp(src_leaf, dst_leaf) == 0) return 0;

    if (fs_load_dir_block(src_dir_bno, &src_blk, &src_db) != 0) return -2;
    src_idx = find_entry_index(src_db, src_leaf, -1);
    if (src_idx == -1) return -3;
    src_entry = src_db->entries[src_idx];

    if (fs_load_dir_block(dst_dir_bno, &dst_blk, &dst_db) != 0) return -2;
    dst_idx = find_entry_index(dst_db, dst_leaf, -1);
    if (dst_idx != -1 && dst_db->entries[dst_idx].type == 1) {
        uint32_t existing_dir_bno = dst_db->entries[dst_idx].bno;
        if (src_entry.type == 1 && fs_dir_is_ancestor(src_entry.bno, existing_dir_bno)) return -6;
        dst_dir_bno = existing_dir_bno;
        copy_name20(dst_leaf, src_leaf);
        if (fs_load_dir_block(dst_dir_bno, &dst_blk, &dst_db) != 0) return -2;
        dst_idx = find_entry_index(dst_db, dst_leaf, -1);
    }
    if (src_dir_bno == dst_dir_bno) {
        if (dst_idx != -1 && dst_idx != src_idx) return -4;
        copy_name20(src_db->entries[src_idx].name, dst_leaf);
        src_db->entries[src_idx].mtime = current_fs_time();
        virtio_disk_rw(&src_blk, 1);
        return 0;
    }
    if (dst_idx != -1) return -4;
    dst_idx = find_free_entry_index(dst_db);
    if (dst_idx == -1) return -5;

    dst_db->entries[dst_idx] = src_entry;
    copy_name20(dst_db->entries[dst_idx].name, dst_leaf);
    dst_db->entries[dst_idx].mtime = current_fs_time();

    memset(&src_db->entries[src_idx], 0, sizeof(src_db->entries[src_idx]));
    virtio_disk_rw(&src_blk, 1);
    virtio_disk_rw(&dst_blk, 1);

    if (src_entry.type == 1 && src_entry.bno != 0) {
        struct dir_block *child_db;
        if (fs_load_dir_block(src_entry.bno, &child_blk, &child_db) == 0) {
            child_db->parent_bno = dst_dir_bno;
            virtio_disk_rw(&child_blk, 1);
        }
    }

    return 0;
}

int store_file_bytes(struct Window *w, const char *name, const unsigned char *data, uint32_t size) {
    struct dir_block *db;
    const char *leaf = name;
    char leaf_buf[20];
    int use_resolve = 0;
    struct blk dir_blk; 
    uint32_t target_dir_bno = 0;

    lib_printf("[fs] store_file_bytes name='%s' size=%u\n", name, size);

    if (!w || !name) return -1;
    if (strchr(name, '/') || name[0] == '~' || name[0] == '/') use_resolve = 1;

    if (use_resolve) {
        char cwd_buf[128];
        lib_printf("[fs] resolving path...\n");
        if (resolve_fs_target(w, name, &target_dir_bno, cwd_buf, leaf_buf) != 0) {
            lib_printf("[fs] resolve failed for '%s'\n", name);
            return -1;
        }
        target_dir_bno = target_dir_bno ? target_dir_bno : 1;
        leaf = leaf_buf;
        lib_printf("[fs] resolved target_dir_bno=%u leaf='%s'\n", target_dir_bno, leaf);
    } else {
        target_dir_bno = w->cwd_bno ? w->cwd_bno : 1;
        lib_printf("[fs] using current dir_bno=%u\n", target_dir_bno);
    }

    memset(&dir_blk, 0, sizeof(dir_blk));
    dir_blk.blockno = target_dir_bno;
    virtio_disk_rw(&dir_blk, 0);
    db = (struct dir_block *)dir_blk.data;
    if (db->magic != FS_MAGIC) {
        lib_printf("[fs] error: target block %u is not a valid directory (magic=0x%x)\n", target_dir_bno, db->magic);
        return -1;
    }

    int idx = find_entry_index(db, leaf, -1);
    uint32_t now = current_fs_time();
    uint32_t created_at = now;
    struct file_entry old = {0};

    if (idx != -1) {
        lib_printf("[fs] updating existing file at index %d\n", idx);
        if (db->entries[idx].type == 1) {
            lib_printf("[fs] error: '%s' is a directory\n", leaf);
            return -3;
        }
        old = db->entries[idx];
        created_at = db->entries[idx].ctime;
    } else {
        lib_printf("[fs] creating new file entry for '%s'\n", leaf);
        idx = find_free_entry_index(db);
    }
    
    if (idx == -1) {
        lib_printf("[fs] error: directory at block %u is full\n", target_dir_bno);
        return -2;
    }

    uint32_t first_bno = 0;
    uint32_t prev_bno = 0;
    uint32_t written = 0;

    if (size == 0) {
        first_bno = balloc();
        lib_printf("[fs] allocated first block %u for empty file\n", first_bno);
        if (first_bno == 0) return -4;
        struct blk empty_blk;
        memset(&empty_blk, 0, sizeof(empty_blk));
        empty_blk.blockno = first_bno;
        struct data_block *d = (struct data_block *)empty_blk.data;
        d->magic = FS_DATA_MAGIC;
        d->next_bno = 0;
        virtio_disk_rw(&empty_blk, 1);
    }

    while (written < size) {
        uint32_t this_bno = balloc();
        if (this_bno == 0) {
            lib_printf("[fs] error: disk full (balloc=0)\n");
            return -4;
        }
        
        struct blk blkbuf;
        uint32_t chunk = size - written;
        if (chunk > FS_DATA_PAYLOAD) chunk = FS_DATA_PAYLOAD;

        memset(&blkbuf, 0, sizeof(blkbuf));
        blkbuf.blockno = this_bno;
        struct data_block *dblk = (struct data_block *)blkbuf.data;
        dblk->magic = FS_DATA_MAGIC;
        dblk->next_bno = 0;
        memcpy(dblk->data, data + written, chunk);
        virtio_disk_rw(&blkbuf, 1);

        if (prev_bno != 0) {
            struct blk link_blk;
            memset(&link_blk, 0, sizeof(link_blk));
            link_blk.blockno = prev_bno;
            virtio_disk_rw(&link_blk, 0);
            ((struct data_block *)link_blk.data)->next_bno = this_bno;
            virtio_disk_rw(&link_blk, 1);
        } else {
            first_bno = this_bno;
        }
        prev_bno = this_bno;
        written += chunk;
    }

    lib_printf("[fs] committing entry to index %d, first_bno=%u\n", idx, first_bno);
    copy_name20(db->entries[idx].name, leaf);
    db->entries[idx].bno = first_bno;
    db->entries[idx].size = size;
    db->entries[idx].ctime = created_at;
    db->entries[idx].mtime = now;
    db->entries[idx].type = 0;
    db->entries[idx].mode = 7;
    
    virtio_disk_rw(&dir_blk, 1);

    if (old.bno != 0) {
        lib_printf("[fs] freeing old data chain starting at %u\n", old.bno);
        fs_free_chain(old.bno);
    }
    
    lib_printf("[fs] store_file_bytes success\n");
    return 0;
}

int load_file_bytes(struct Window *w, const char *name, unsigned char *dst, uint32_t max_size, uint32_t *out_size) {
    struct Window *ctx = &fs_tmp_window;
    struct dir_block *db;
    const char *leaf = name;
    char leaf_buf[20];
    int use_resolve = 0;
    uint32_t remaining;
    uint32_t copied = 0;
    uint32_t bno;

    if (!w || !name) return -1;
    if (strchr(name, '/') || name[0] == '~') use_resolve = 1;
    if (use_resolve) {
        uint32_t dir_bno = 0;
        char cwd_buf[32];
        if (resolve_fs_target(w, name, &dir_bno, cwd_buf, leaf_buf) != 0) return -1;
        memset(ctx, 0, sizeof(*ctx));
        ctx->cwd_bno = dir_bno ? dir_bno : 1;
        lib_strcpy(ctx->cwd, cwd_buf[0] ? cwd_buf : "/root");
        db = load_current_dir(ctx);
        leaf = leaf_buf;
    } else {
        db = load_current_dir(w);
    }
    if (!db) return -1;
    int idx = find_entry_index(db, leaf, 0);
    if (idx == -1) return -2;

    remaining = db->entries[idx].size;
    if (remaining > max_size) return -3;
    bno = db->entries[idx].bno;

    if (bno == 0) {
        *out_size = 0;
        return 0;
    }

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
    return 0;
}

int load_file_bytes_alloc(struct Window *w, const char *name, unsigned char **dst, uint32_t *out_size) {
    struct Window *ctx = &fs_tmp_window;
    struct dir_block *db;
    const char *leaf = name;
    char leaf_buf[20];
    int use_resolve = 0;
    uint32_t remaining;
    uint32_t copied = 0;
    uint32_t bno;
    uint32_t size;
    unsigned char *buf;

    if (!w || !name) return -1;
    if (strchr(name, '/') || name[0] == '~') use_resolve = 1;
    if (use_resolve) {
        uint32_t dir_bno = 0;
        char cwd_buf[32];
        if (resolve_fs_target(w, name, &dir_bno, cwd_buf, leaf_buf) != 0) return -1;
        memset(ctx, 0, sizeof(*ctx));
        ctx->cwd_bno = dir_bno ? dir_bno : 1;
        lib_strcpy(ctx->cwd, cwd_buf[0] ? cwd_buf : "/root");
        db = load_current_dir(ctx);
        leaf = leaf_buf;
    } else {
        db = load_current_dir(w);
    }
    if (!db) return -1;
    int idx = find_entry_index(db, leaf, 0);
    if (idx == -1) return -2;

    size = db->entries[idx].size;
    buf = (unsigned char *)malloc(size ? size : 1);
    if (!buf) return -4;

    remaining = size;
    bno = db->entries[idx].bno;

    if (bno == 0) {
        *dst = buf;
        *out_size = 0;
        return 0;
    }

    while (remaining > 0 && bno != 0) {
        struct blk blkbuf;
        memset(&blkbuf, 0, sizeof(blkbuf));
        blkbuf.blockno = bno;
        virtio_disk_rw(&blkbuf, 0);

        struct data_block *dblk = (struct data_block *)blkbuf.data;
        if (dblk->magic == FS_DATA_MAGIC) {
            uint32_t chunk = remaining;
            if (chunk > FS_DATA_PAYLOAD) chunk = FS_DATA_PAYLOAD;
            memcpy(buf + copied, dblk->data, chunk);
            copied += chunk;
            remaining -= chunk;
            bno = dblk->next_bno;
        } else {
            uint32_t chunk = remaining;
            if (chunk > BSIZE) chunk = BSIZE;
            memcpy(buf + copied, blkbuf.data, chunk);
            copied += chunk;
            remaining -= chunk;
            bno = 0;
        }
    }

    if (copied != size) {
        free(buf);
        return -5;
    }

    *dst = buf;
    *out_size = copied;
    return 0;
}

static void appfs_reset_table(void) {
    for (int i = 0; i < APP_MAX_FDS; i++) {
        if (app_fd_table[i].buf) {
            free(app_fd_table[i].buf);
        }
        memset(&app_fd_table[i], 0, sizeof(app_fd_table[i]));
    }
}

void appfs_set_cwd(uint32_t cwd_bno, const char *cwd) {
    appfs_reset_table();
    app_fs_cwd_bno = cwd_bno ? cwd_bno : 1;
    if (cwd && cwd[0]) {
        lib_strcpy(app_fs_cwd, cwd);
    } else {
        lib_strcpy(app_fs_cwd, "/root");
    }
}

static int appfs_path_to_name(const char *path, char *name_out) {
    int i = 0;
    if (!path || !name_out) return 0;
    while (*path == '/') path++;
    if (*path == '\0') return 0;
    while (*path && *path != '/') {
        if (i >= 19) return 0;
        name_out[i++] = *path++;
    }
    if (*path != '\0') return 0;
    name_out[i] = '\0';
    return 1;
}

static int appfs_find_fd(void) {
    for (int i = 0; i < APP_MAX_FDS; i++) {
        if (!app_fd_table[i].used) return i;
    }
    return -1;
}

static int appfs_reserve(struct app_fd_entry *fd, uint32_t need) {
    if (need <= fd->cap) return 0;
    uint32_t new_cap = fd->cap ? fd->cap : 256;
    while (new_cap < need) {
        new_cap <<= 1;
        if (new_cap < fd->cap) return -1;
    }
    unsigned char *nbuf = (unsigned char *)malloc(new_cap);
    if (!nbuf) return -1;
    if (fd->buf && fd->size) {
        memcpy(nbuf, fd->buf, fd->size);
    }
    if (fd->buf) {
        free(fd->buf);
    }
    fd->buf = nbuf;
    fd->cap = new_cap;
    return 0;
}

static int appfs_load_file(struct app_fd_entry *fd, const char *name, int flags) {
    struct dir_block *db;
    struct Window *w = &app_fs_window;
    int idx;

    memset(w, 0, sizeof(*w));
    w->cwd_bno = fd && fd->cwd_bno ? fd->cwd_bno : app_fs_cwd_bno;
    lib_strcpy(w->cwd, (fd && fd->cwd[0]) ? fd->cwd : app_fs_cwd);
    db = load_current_dir(w);
    if (!db) return -1;

    idx = find_entry_index(db, name, 0);
    if (idx == -1) {
        if (!(flags & APP_O_CREAT)) return -2;
        fd->size = 0;
        fd->cap = 0;
        fd->buf = 0;
        return 0;
    }

    fd->size = db->entries[idx].size;
    fd->cap = 0;
    fd->buf = 0;
    if (fd->size == 0) {
        if (appfs_reserve(fd, 1) != 0) return -3;
        fd->size = 0;
        fd->buf[0] = 0;
        return 0;
    }
    if (appfs_reserve(fd, fd->size) != 0) return -3;
    if (load_file_bytes(w, name, fd->buf, fd->cap, &fd->size) != 0) return -4;
    return 0;
}

static int appfs_commit(struct app_fd_entry *fd) {
    struct Window *w = &app_fs_window;
    memset(w, 0, sizeof(*w));
    if (!fd || !fd->used) return -1;
    w->cwd_bno = fd->cwd_bno ? fd->cwd_bno : app_fs_cwd_bno;
    lib_strcpy(w->cwd, fd->cwd[0] ? fd->cwd : app_fs_cwd);
    if (store_file_bytes(w, fd->name, fd->buf ? fd->buf : (const unsigned char *)"", fd->size) != 0) {
        return -1;
    }
    return 0;
}

static int appfs_close_fd(int fd_idx) {
    if (fd_idx < 0 || fd_idx >= APP_MAX_FDS) return -1;
    struct app_fd_entry *fd = &app_fd_table[fd_idx];
    if (!fd->used) return -1;
    if ((fd->flags & APP_O_WRITE) || (fd->flags & APP_O_CREAT) || (fd->flags & APP_O_TRUNC)) {
        if (appfs_commit(fd) != 0) {
            return -1;
        }
    }
    if (fd->buf) {
        free(fd->buf);
    }
    memset(fd, 0, sizeof(*fd));
    return 0;
}

int appfs_close_all(void) {
    int rc = 0;
    for (int i = 0; i < APP_MAX_FDS; i++) {
        if (app_fd_table[i].used) {
            if (appfs_close_fd(i) != 0) {
                rc = -1;
            }
        }
    }
    return rc;
}

int appfs_open(const char *path, int flags) {
    char name[20];
    char cwd_buf[32];
    uint32_t dir_bno = 0;
    int fd_idx;
    struct app_fd_entry *fd;

    fd_idx = appfs_find_fd();
    if (fd_idx < 0) return -2;

    fd = &app_fd_table[fd_idx];
    memset(fd, 0, sizeof(*fd));
    fd->used = 1;
    fd->flags = flags;

    if (path && (strchr(path, '/') || path[0] == '~')) {
        if (resolve_fs_target(&app_fs_window, path, &dir_bno, cwd_buf, name) != 0 || name[0] == '\0') {
            memset(fd, 0, sizeof(*fd));
            return -1;
        }
        fd->cwd_bno = dir_bno ? dir_bno : 1;
        lib_strcpy(fd->cwd, cwd_buf[0] ? cwd_buf : "/root");
    } else {
        if (!appfs_path_to_name(path, name)) {
            memset(fd, 0, sizeof(*fd));
            return -1;
        }
        fd->cwd_bno = app_fs_cwd_bno ? app_fs_cwd_bno : 1;
        lib_strcpy(fd->cwd, app_fs_cwd[0] ? app_fs_cwd : "/root");
    }
    copy_name20(fd->name, name);

    if ((flags & APP_O_WRITE) && (flags & (APP_O_CREAT | APP_O_TRUNC))) {
        fd->size = 0;
        fd->cap = 0;
        fd->buf = 0;
        if (fd->flags & APP_O_TRUNC) {
            fd->size = 0;
        }
        fd->pos = 0;
        return fd_idx + 3;
    }

    if (flags & APP_O_READ) {
        if (appfs_load_file(fd, name, flags) != 0) {
            memset(fd, 0, sizeof(*fd));
            return -3;
        }
    } else {
        if (appfs_load_file(fd, name, flags) != 0) {
            if (!(flags & APP_O_CREAT)) {
                memset(fd, 0, sizeof(*fd));
                return -3;
            }
            fd->size = 0;
            fd->cap = 0;
            fd->buf = 0;
        }
    }

    if (flags & APP_O_TRUNC) {
        fd->size = 0;
        if (fd->cap == 0) {
            if (appfs_reserve(fd, 1) != 0) {
                memset(fd, 0, sizeof(*fd));
                return -4;
            }
        }
    }

    fd->pos = 0;
    return fd_idx + 3;
}

int appfs_read(int fd_no, void *buf, size_t size) {
    int fd_idx = fd_no - 3;
    struct app_fd_entry *fd;
    uint32_t remain;
    uint32_t chunk;

    if (fd_idx < 0 || fd_idx >= APP_MAX_FDS || !buf) return -1;
    fd = &app_fd_table[fd_idx];
    if (!fd->used) return -1;
    if (!(fd->flags & APP_O_READ)) return -1;
    if (fd->pos >= fd->size) return 0;
    remain = fd->size - fd->pos;
    chunk = (uint32_t)size;
    if (chunk > remain) chunk = remain;
    memcpy(buf, fd->buf + fd->pos, chunk);
    fd->pos += chunk;
    return (int)chunk;
}

int appfs_write(int fd_no, const void *buf, size_t size) {
    int fd_idx = fd_no - 3;
    struct app_fd_entry *fd;
    uint32_t need;

    if (fd_idx < 0 || fd_idx >= APP_MAX_FDS || !buf) return -1;
    fd = &app_fd_table[fd_idx];
    if (!fd->used) return -1;
    if (!(fd->flags & APP_O_WRITE)) return -1;
    need = fd->pos + (uint32_t)size;
    if (need > fd->cap) {
        if (appfs_reserve(fd, need) != 0) return -1;
    }
    memcpy(fd->buf + fd->pos, buf, size);
    fd->pos += (uint32_t)size;
    if (fd->pos > fd->size) fd->size = fd->pos;
    return (int)size;
}

int appfs_seek(int fd_no, int offset, int whence) {
    int fd_idx = fd_no - 3;
    struct app_fd_entry *fd;
    int base;
    int next;

    if (fd_idx < 0 || fd_idx >= APP_MAX_FDS) return -1;
    fd = &app_fd_table[fd_idx];
    if (!fd->used) return -1;

    if (whence == 0) base = 0;
    else if (whence == 1) base = (int)fd->pos;
    else if (whence == 2) base = (int)fd->size;
    else return -1;

    next = base + offset;
    if (next < 0) return -1;
    if ((uint32_t)next > fd->size) {
        if (!(fd->flags & APP_O_WRITE)) return -1;
        if ((uint32_t)next > fd->cap) {
            if (appfs_reserve(fd, (uint32_t)next) != 0) return -1;
        }
        if ((uint32_t)next > fd->size) {
            memset(fd->buf + fd->size, 0, (uint32_t)next - fd->size);
            fd->size = (uint32_t)next;
        }
    }
    fd->pos = (uint32_t)next;
    return next;
}

int appfs_tell(int fd_no) {
    int fd_idx = fd_no - 3;
    if (fd_idx < 0 || fd_idx >= APP_MAX_FDS) return -1;
    if (!app_fd_table[fd_idx].used) return -1;
    return (int)app_fd_table[fd_idx].pos;
}

int appfs_close(int fd_no) {
    int fd_idx = fd_no - 3;
    return appfs_close_fd(fd_idx);
}

static void fs_scan_chain(uint32_t bno) {
    uint32_t guard = 0;
    while (bno >= 60 && bno < FS_MAX_BLOCKS && guard++ < FS_MAX_BLOCKS) {
        if (fs_block_is_used(bno)) return;
        struct blk blkbuf;
        memset(&blkbuf, 0, sizeof(blkbuf));
        blkbuf.blockno = bno;
        virtio_disk_rw(&blkbuf, 0);
        fs_block_set_used(bno);
        struct data_block *dblk = (struct data_block *)blkbuf.data;
        if (dblk->magic != FS_DATA_MAGIC) return;
        if (dblk->next_bno == 0 || dblk->next_bno == bno) return;
        bno = dblk->next_bno;
    }
}

static void fs_scan_dir(uint32_t bno, int depth) {
    if (bno == 0 || bno >= FS_MAX_BLOCKS || depth > 32) return;
    if (depth > 0 && fs_block_is_used(bno)) return;

    struct blk blkbuf;
    memset(&blkbuf, 0, sizeof(blkbuf));
    blkbuf.blockno = bno;
    virtio_disk_rw(&blkbuf, 0);

    struct dir_block *db = (struct dir_block *)blkbuf.data;
    if (db->magic != FS_MAGIC) return;

    fs_block_set_used(bno);
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (!db->entries[i].name[0]) continue;
        if (db->entries[i].type == 1) {
            fs_scan_dir(db->entries[i].bno, depth + 1);
        } else {
            fs_scan_chain(db->entries[i].bno);
        }
    }
}

void fs_rebuild_alloc_state(void) {
    memset(fs_block_map, 0, sizeof(fs_block_map));
    for (uint32_t i = 0; i < 60; i++) {
        fs_block_set_used(i);
    }
    next_free_block = 60;
    fs_alloc_ready = 1;

    struct blk blkbuf;
    memset(&blkbuf, 0, sizeof(blkbuf));
    blkbuf.blockno = 1;
    virtio_disk_rw(&blkbuf, 0);
    struct dir_block *db = (struct dir_block *)blkbuf.data;
    if (db->magic != FS_MAGIC) {
        return;
    }
    fs_scan_dir(1, 0);
    next_free_block = 60;
}
