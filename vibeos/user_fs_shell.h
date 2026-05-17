#ifndef USER_FS_SHELL_H
#define USER_FS_SHELL_H

#include "user.h"

void mode_to_str(uint32_t mode, uint16_t type, char *s);
void path_set_child(char *cwd, const char *name);
void path_set_parent(char *cwd);
void build_editor_path(char *dst, const char *cwd, const char *name);
void shorten_path_for_title(char *dst, const char *src, int max_len);
void copy_name20(char *dst, const char *src);
void append_out_str(char *out, int out_max, const char *s);
void append_out_pad(char *out, int out_max, const char *s, int width);
const char *path_basename(const char *p);
void copy_last_path_segment(char *dst, const char *path, const char *fallback);
int path_is_sftp(const char *path);
const char *sftp_subpath(const char *path);
int resolve_editor_target(struct Window *term, const char *input, uint32_t *dir_bno_out, char *cwd_out, char *leaf_out);
int copy_between_paths(struct Window *w, const char *src, const char *dst, char *out, int out_max);
int local_path_info(struct Window *w, const char *path, int *type_out, uint32_t *bno_out, char *name_out);
int copy_local_dir_recursive(struct Window *w, uint32_t src_bno, const char *dst_path, char *out, int out_max);
int copy_sftp_dir_to_local_recursive(struct Window *w, const char *src_remote, const char *dst_path, char *out, int out_max);

#endif
