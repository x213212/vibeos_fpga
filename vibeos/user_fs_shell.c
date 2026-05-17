#include "user_fs_shell.h"
#include "user_internal.h"

void mode_to_str(uint32_t mode, uint16_t type, char *s) {
    s[0] = (type == 1) ? 'd' : '-';
    s[1] = (mode & 4) ? 'r' : '-'; s[2] = (mode & 2) ? 'w' : '-'; s[3] = (mode & 1) ? 'x' : '-'; s[4] = '\0';
}

void path_set_child(char *cwd, const char *name) {
    int len = strlen(cwd);
    if (len > 1 && cwd[len - 1] != '/') {
        lib_strcat(cwd, "/");
    }
    lib_strncat(cwd, name, 19);
}

void path_set_parent(char *cwd) {
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

void build_editor_path(char *dst, const char *cwd, const char *name) {
    int pos = 0;
    int i = 0;
    if (!dst) return;
    dst[0] = '\0';
    if (cwd && cwd[0]) {
        while (cwd[i] && pos < 127) {
            dst[pos++] = cwd[i++];
        }
    } else {
        const char *root = "/root";
        while (root[i] && pos < 127) {
            dst[pos++] = root[i++];
        }
    }
    if (pos > 0 && dst[pos - 1] != '/' && pos < 127) {
        dst[pos++] = '/';
    }
    i = 0;
    while (name && name[i] && pos < 127) {
        dst[pos++] = name[i++];
    }
    dst[pos] = '\0';
}

void shorten_path_for_title(char *dst, const char *src, int max_len) {
    int src_len = 0;
    if (!dst || max_len <= 0) return;
    dst[0] = '\0';
    if (!src) return;
    while (src[src_len]) src_len++;
    if (src_len <= max_len) {
        int i = 0;
        for (; i < max_len && src[i]; i++) dst[i] = src[i];
        dst[i] = '\0';
        return;
    }
    if (max_len <= 3) {
        for (int i = 0; i < max_len; i++) dst[i] = '.';
        dst[max_len] = '\0';
        return;
    }
    dst[0] = '.';
    dst[1] = '.';
    dst[2] = '.';
    int keep = max_len - 3;
    int start = src_len - keep;
    if (start < 0) start = 0;
    int len = 0;
    for (len = 0; len < keep && src[start + len]; len++) {
        dst[3 + len] = src[start + len];
    }
    dst[3 + len] = '\0';
}

void copy_name20(char *dst, const char *src) {
    int i = 0;
    if (!dst) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    for (; i < 19 && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

void append_out_str(char *out, int out_max, const char *s) {
    int len = strlen(out);
    if (len >= out_max - 1) return;
    while (*s && len < out_max - 1) {
        out[len++] = *s++;
    }
    out[len] = '\0';
}

void append_out_pad(char *out, int out_max, const char *s, int width) {
    int len = strlen(out);
    int used = 0;
    if (width < 0) width = 0;
    while (s && *s && len < out_max - 1 && used < width) {
        out[len++] = *s++;
        used++;
    }
    while (len < out_max - 1 && used < width) {
        out[len++] = ' ';
        used++;
    }
    out[len] = '\0';
}

const char *path_basename(const char *p) {
    const char *base = p;
    if (!p) return "";
    while (*p) {
        if (*p == '/' || *p == '\\') base = p + 1;
        p++;
    }
    return base;
}

int path_is_sftp(const char *path) {
    return path &&
           ((strncmp(path, "/sftp", 5) == 0 && (path[5] == '\0' || path[5] == '/')) ||
            (strncmp(path, "/sftpd", 6) == 0 && (path[6] == '\0' || path[6] == '/')) ||
            (strncmp(path, "./sftp", 6) == 0 && (path[6] == '\0' || path[6] == '/')) ||
            (strncmp(path, "./sftpd", 7) == 0 && (path[7] == '\0' || path[7] == '/')) ||
            (strncmp(path, "sftp", 4) == 0 && (path[4] == '\0' || path[4] == '/')) ||
            (strncmp(path, "sftpd", 5) == 0 && (path[5] == '\0' || path[5] == '/')));
}

const char *sftp_subpath(const char *path) {
    if (!path_is_sftp(path)) return path;
    if (strncmp(path, "/sftpd", 6) == 0) path += 6;
    else if (strncmp(path, "/sftp", 5) == 0) path += 5;
    else if (strncmp(path, "./sftpd", 7) == 0) path += 7;
    else if (strncmp(path, "./sftp", 6) == 0) path += 6;
    else if (strncmp(path, "sftpd", 5) == 0) path += 5;
    else path += 4;
    if (*path == '/') path++;
    return *path ? path : ".";
}

int resolve_editor_target(struct Window *term, const char *input, uint32_t *dir_bno_out, char *cwd_out, char *leaf_out) {
    extern struct Window fs_tmp_window;
    struct Window *tmp = &fs_tmp_window;
    const char *p;
    char seg[20];
    int seg_len = 0;
    char expanded[128];
    int ends_with_slash = 0;

    if (!term || !input || !dir_bno_out || !cwd_out || !leaf_out) return -1;

    terminal_expand_home_path(term, input, expanded, sizeof(expanded));
    p = expanded;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') return -1;
    {
        int x = 0;
        while (expanded[x]) x++;
        while (x > 0 && (expanded[x - 1] == ' ' || expanded[x - 1] == '\t')) x--;
        ends_with_slash = (x > 0 && expanded[x - 1] == '/');
    }

    memset(tmp, 0, sizeof(*tmp));
    tmp->cwd_bno = term->cwd_bno ? term->cwd_bno : 1;
    if (term->cwd[0]) lib_strcpy(tmp->cwd, term->cwd);
    else lib_strcpy(tmp->cwd, "/root");

    if (p[0] == '/') {
        tmp->cwd_bno = 1;
        lib_strcpy(tmp->cwd, "/root");
        while (*p == '/') p++;
        if (strncmp(p, "root/", 5) == 0) p += 5;
        else if (strcmp(p, "root") == 0) p += 4;
        while (*p == '/') p++;
    }

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
        if (seg_len == 0 || strcmp(seg, ".") == 0) {
            continue;
        }
        if (strcmp(seg, "..") == 0) {
            struct dir_block *db = load_current_dir(tmp);
            if (!db) return -1;
            tmp->cwd_bno = db->parent_bno ? db->parent_bno : 1;
            if (tmp->cwd_bno == 1) lib_strcpy(tmp->cwd, "/root");
            else path_set_parent(tmp->cwd);
            continue;
        }
        if (*p == '\0') {
            if (ends_with_slash) {
                struct dir_block *db = load_current_dir(tmp);
                if (!db) return -1;
                int idx = find_entry_index(db, seg, 1);
                if (idx == -1) return -1;
                tmp->cwd_bno = db->entries[idx].bno;
                path_set_child(tmp->cwd, db->entries[idx].name);
                copy_name20(leaf_out, "");
                *dir_bno_out = tmp->cwd_bno ? tmp->cwd_bno : 1;
                lib_strcpy(cwd_out, tmp->cwd[0] ? tmp->cwd : "/root");
                return 0;
            }
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
        path_set_child(tmp->cwd, db->entries[idx].name);
    }

    *dir_bno_out = tmp->cwd_bno ? tmp->cwd_bno : 1;
    lib_strcpy(cwd_out, tmp->cwd[0] ? tmp->cwd : "/root");
    copy_name20(leaf_out, "");
    return 0;
}


int copy_between_paths(struct Window *w, const char *src, const char *dst, char *out, int out_max);
int local_path_info(struct Window *w, const char *path, int *type_out, uint32_t *bno_out, char *name_out);
static int local_mkdir_path(struct Window *w, const char *path);
int copy_local_dir_recursive(struct Window *w, uint32_t src_bno, const char *dst_path, char *out, int out_max);
int copy_sftp_dir_to_local_recursive(struct Window *w, const char *src_remote, const char *dst_path, char *out, int out_max);


int copy_between_paths(struct Window *w, const char *src, const char *dst, char *out, int out_max) {
    int src_sftp, dst_sftp;
    if (!w || !src || !dst || !out || out_max <= 0) return -1;
    out[0] = '\0';
    src_sftp = path_is_sftp(src);
    dst_sftp = path_is_sftp(dst);

    if (!src_sftp && !dst_sftp) {
        unsigned char *buf = NULL;
        uint32_t size = 0;
        if (load_file_bytes_alloc(w, src, &buf, &size) != 0 || !buf) {
            lib_strcpy(out, "ERR: Source Read Failed.");
            return -1;
        }
        if (store_file_bytes(w, dst, buf, size) != 0) {
            free(buf);
            lib_strcpy(out, "ERR: Destination Write Failed.");
            return -1;
        }
        free(buf);
        lib_strcpy(out, ">> Copied.");
        return 0;
    }
    if (src_sftp && !dst_sftp) {
        unsigned char *remote_buf = NULL;
        uint32_t remote_size = 0;
        char msg[OUT_BUF_SIZE];
        if (ssh_client_sftp_read_alloc(sftp_subpath(src), &remote_buf, &remote_size, msg, sizeof(msg)) != 0 || !remote_buf) {
            lib_strcpy(out, msg[0] ? msg : "ERR: SFTP Read Failed.");
            return -1;
        }
        if (store_file_bytes(w, dst, remote_buf, remote_size) != 0) {
            free(remote_buf);
            lib_strcpy(out, "ERR: Destination Write Failed.");
            return -1;
        }
        free(remote_buf);
        lib_strcpy(out, ">> Copied.");
        return 0;
    }
    if (!src_sftp && dst_sftp) {
        if (ssh_client_sftp_put(w, src, sftp_subpath(dst), out, out_max) != 0) return -1;
        return 0;
    }
    {
        unsigned char *remote_buf = NULL;
        uint32_t remote_size = 0;
        char msg[OUT_BUF_SIZE];
        if (ssh_client_sftp_read_alloc(sftp_subpath(src), &remote_buf, &remote_size, msg, sizeof(msg)) != 0 || !remote_buf) {
            lib_strcpy(out, msg[0] ? msg : "ERR: SFTP Read Failed.");
            return -1;
        }
        if (ssh_client_sftp_write_bytes(sftp_subpath(dst), remote_buf, remote_size, out, out_max) != 0) {
            free(remote_buf);
            return -1;
        }
        free(remote_buf);
        return 0;
    }
}

void copy_last_path_segment(char *dst, const char *path, const char *fallback) {
    const char *base;
    if (!dst) return;
    dst[0] = '\0';
    if (path) {
        int len = (int)strlen(path);
        while (len > 0 && path[len - 1] == '/') len--;
        if (len > 0) {
            int start = len - 1;
            while (start > 0 && path[start - 1] != '/') start--;
            base = path + start;
            {
                int n = len - start;
                if (n > 19) n = 19;
                memcpy(dst, base, n);
                dst[n] = '\0';
                if (dst[0]) return;
            }
        }
    }
    if (fallback && *fallback) copy_name20(dst, fallback);
}

int local_path_info(struct Window *w, const char *path, int *type_out, uint32_t *bno_out, char *name_out) {
    uint32_t p_bno = 0;
    char p_cwd[128], leaf[20];
    struct Window *tmp = &fs_tmp_window;
    struct dir_block *db;
    int idx;
    if (type_out) *type_out = -1;
    if (bno_out) *bno_out = 0;
    if (name_out) name_out[0] = '\0';
    if (!w || !path) return -1;
    if (resolve_fs_target(w, path, &p_bno, p_cwd, leaf) != 0) return -1;
    if (leaf[0] == '\0' || strcmp(leaf, ".") == 0) {
        if (type_out) *type_out = 1;
        if (bno_out) *bno_out = p_bno ? p_bno : 1;
        if (name_out) copy_name20(name_out, path_basename(p_cwd));
        return 0;
    }
    memset(tmp, 0, sizeof(*tmp));
    tmp->cwd_bno = p_bno ? p_bno : 1;
    db = load_current_dir(tmp);
    if (!db) return -1;
    idx = find_entry_index(db, leaf, -1);
    if (idx < 0) return -1;
    if (type_out) *type_out = db->entries[idx].type;
    if (bno_out) *bno_out = db->entries[idx].bno;
    if (name_out) copy_name20(name_out, db->entries[idx].name);
    return 0;
}

static int local_mkdir_path(struct Window *w, const char *path) {
    char expanded[128];
    uint32_t p_bno = 0;
    char p_cwd[128], leaf[20];
    struct Window *ctx = &fs_tmp_window;
    struct dir_block *db;
    int idx;
    if (!w || !path || !*path) return -1;
    terminal_expand_home_path(w, path, expanded, sizeof(expanded));
    if (strcmp(expanded, ".") == 0 || strcmp(expanded, "./") == 0) return 0;
    if (resolve_editor_target(w, expanded, &p_bno, p_cwd, leaf) != 0 || leaf[0] == '\0') return -1;
    memset(ctx, 0, sizeof(*ctx));
    ctx->cwd_bno = p_bno ? p_bno : 1;
    db = load_current_dir(ctx);
    if (!db) return -1;
    if (find_entry_index(db, leaf, -1) != -1) return 0;
    idx = find_free_entry_index(db);
    if (idx < 0) return -1;
    uint32_t nb = balloc();
    struct blk nb_blk;
    if (nb == 0) return -1;
    memset(&db->entries[idx], 0, sizeof(db->entries[idx]));
    copy_name20(db->entries[idx].name, leaf);
    db->entries[idx].bno = nb;
    db->entries[idx].type = 1;
    db->entries[idx].mode = 7;
    db->entries[idx].ctime = db->entries[idx].mtime = current_fs_time();
    virtio_disk_rw(&ctx->local_b, 1);
    memset(&nb_blk, 0, sizeof(nb_blk));
    nb_blk.blockno = nb;
    struct dir_block *nd = (struct dir_block *)nb_blk.data;
    nd->magic = FS_MAGIC;
    nd->parent_bno = p_bno ? p_bno : 1;
    virtio_disk_rw(&nb_blk, 1);
    return 0;
}

int copy_local_dir_recursive(struct Window *w, uint32_t src_bno, const char *dst_path, char *out, int out_max) {
    struct blk src_blk;
    struct dir_block *src_db;
    if (!w || !dst_path) return -1;
    if (local_mkdir_path(w, dst_path) != 0) {
        if (out && out_max > 0) lib_strcpy(out, "ERR: mkdir failed.");
        return -1;
    }
    memset(&src_blk, 0, sizeof(src_blk));
    src_blk.blockno = src_bno;
    virtio_disk_rw(&src_blk, 0);
    src_db = (struct dir_block *)src_blk.data;
    if (src_db->magic != FS_MAGIC) {
        if (out && out_max > 0) lib_strcpy(out, "ERR: bad source dir.");
        return -1;
    }
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        char child_src[128];
        char child_dst[128];
        unsigned char *buf = NULL;
        uint32_t size = 0;
        if (!src_db->entries[i].name[0]) continue;
        lib_strcpy(child_dst, dst_path);
        if (child_dst[0] && child_dst[strlen(child_dst) - 1] != '/') lib_strcat(child_dst, "/");
        lib_strcat(child_dst, src_db->entries[i].name);
        if (src_db->entries[i].type == 1) {
            if (copy_local_dir_recursive(w, src_db->entries[i].bno, child_dst, out, out_max) != 0) return -1;
            continue;
        }
        lib_strcpy(child_src, "/root");
        {
            char src_path_buf[128];
            // Resolve a stable absolute source path from src_bno/name by reusing destination dirname semantics is overkill;
            // use cwd swap on a temporary window rooted at the source dir.
            struct Window *tmp = &fs_tmp_window;
            memset(tmp, 0, sizeof(*tmp));
            tmp->cwd_bno = src_bno;
            lib_strcpy(tmp->cwd, "/root");
            if (load_file_bytes_alloc(tmp, src_db->entries[i].name, &buf, &size) != 0 || !buf) {
                if (out && out_max > 0) lib_strcpy(out, "ERR: file read failed.");
                return -1;
            }
            if (store_file_bytes(w, child_dst, buf, size) != 0) {
                free(buf);
                if (out && out_max > 0) lib_strcpy(out, "ERR: file write failed.");
                return -1;
            }
            free(buf);
            (void)src_path_buf;
        }
    }
    return 0;
}

int copy_sftp_dir_to_local_recursive(struct Window *w, const char *src_remote, const char *dst_path, char *out, int out_max) {
    char list_out[OUT_BUF_SIZE];
    char line[160];
    const char *p;
    if (!w || !src_remote || !dst_path) return -1;
    if (local_mkdir_path(w, dst_path) != 0) {
        if (out && out_max > 0) lib_strcpy(out, "ERR: mkdir failed.");
        return -1;
    }
    if (ssh_client_sftp_ls(src_remote, 0, list_out, sizeof(list_out)) != 0) {
        if (out && out_max > 0) lib_strcpy(out, "ERR: SFTP list failed.");
        return -1;
    }
    p = list_out;
    while (*p) {
        int li = 0;
        char type = 0;
        char name[96];
        char src_child[192];
        char dst_child[128];
        while (*p == '\n' || *p == '\r') p++;
        if (!*p) break;
        while (*p && *p != '\n' && li < (int)sizeof(line) - 1) line[li++] = *p++;
        if (*p == '\n') p++;
        line[li] = '\0';
        if (li < 3 || line[1] != ' ') continue;
        type = line[0];
        strncpy(name, line + 2, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        if (!name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        lib_strcpy(src_child, src_remote);
        if (src_child[0] && src_child[strlen(src_child) - 1] != '/') lib_strcat(src_child, "/");
        lib_strcat(src_child, name);
        lib_strcpy(dst_child, dst_path);
        if (dst_child[0] && dst_child[strlen(dst_child) - 1] != '/') lib_strcat(dst_child, "/");
        lib_strcat(dst_child, name);
        if (type == 'd') {
            if (copy_sftp_dir_to_local_recursive(w, src_child, dst_child, out, out_max) != 0) return -1;
        } else if (type == 'f') {
            unsigned char *remote_buf = NULL;
            uint32_t remote_size = 0;
            char msg[OUT_BUF_SIZE];
            if (ssh_client_sftp_read_alloc(src_child, &remote_buf, &remote_size, msg, sizeof(msg)) != 0 || !remote_buf) {
                if (out && out_max > 0) lib_strcpy(out, msg[0] ? msg : "ERR: SFTP read failed.");
                return -1;
            }
            if (store_file_bytes(w, dst_child, remote_buf, remote_size) != 0) {
                free(remote_buf);
                if (out && out_max > 0) lib_strcpy(out, "ERR: local write failed.");
                return -1;
            }
            free(remote_buf);
        }
    }
    return 0;
}
