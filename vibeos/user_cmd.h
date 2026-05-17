#ifndef USER_CMD_H
#define USER_CMD_H

#include "user.h"

void exec_single_cmd(struct Window *w, char *cmd);
void try_tab_complete(struct Window *w, int row);
int str_starts_with(const char *s, const char *prefix);
int terminal_first_token(const char *line, char *out, int out_size);
void append_dir_entries_sorted(struct dir_block *db, const char *title, const char *name_prefix, int type_filter, char *out);
void list_dir_contents(uint32_t bno, char *out);
void format_size_human(uint32_t bytes, char *out);
void append_hex32(char *dst, uint32_t v);
void append_hex8(char *dst, unsigned char v);
int is_mostly_text(const unsigned char *buf, uint32_t size);
void render_hex_dump(char *out, const unsigned char *buf, uint32_t size);

#endif
