#include "user.h"
#include "user_wget.h"

#define ASM_MAX_LINES 256
#define ASM_MAX_LABELS 64
#define ASM_LINE_LEN 192

static int asm_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char *asm_trim(char *s) {
    char *e;
    while (*s && asm_is_space(*s)) s++;
    e = s + strlen(s);
    while (e > s && asm_is_space(e[-1])) {
        e--;
        *e = '\0';
    }
    return s;
}

static void asm_strip_comment(char *s) {
    for (int i = 0; s[i]; i++) {
        if (s[i] == '#') {
            s[i] = '\0';
            return;
        }
        if (s[i] == '/' && s[i + 1] == '/') {
            s[i] = '\0';
            return;
        }
    }
}

static int asm_is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.';
}

static int asm_is_ident_char(char c) {
    return asm_is_ident_start(c) || (c >= '0' && c <= '9');
}

static int asm_parse_reg(const char *name) {
    if (!name || !name[0]) return -1;
    if (name[0] == 'x' && name[1] >= '0' && name[1] <= '9') {
        int n = 0;
        for (int i = 1; name[i] >= '0' && name[i] <= '9'; i++) {
            n = n * 10 + (name[i] - '0');
            if (name[i + 1] == '\0') {
                if (n >= 0 && n < 32) return n;
                return -1;
            }
        }
        return -1;
    }
    static const char *regs[] = {
        "zero", "ra", "sp", "gp", "tp",
        "t0", "t1", "t2",
        "s0", "fp", "s1",
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
        "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11",
        "t3", "t4", "t5", "t6"
    };
    static const int reg_ids[] = {
        0, 1, 2, 3, 4,
        5, 6, 7,
        8, 8, 9,
        10, 11, 12, 13, 14, 15, 16, 17,
        18, 19, 20, 21, 22, 23, 24, 25, 26, 27,
        28, 29, 30, 31
    };
    for (unsigned int i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        if (strcmp(name, regs[i]) == 0) return reg_ids[i];
    }
    return -1;
}

static int asm_parse_imm(const char *s, int32_t *out) {
    int sign = 1;
    uint32_t v = 0;
    int base = 10;
    if (!s || !*s) return -1;
    if (*s == '+') s++;
    else if (*s == '-') { sign = -1; s++; }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    if (!*s) return -1;
    while (*s) {
        char c = *s++;
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') digit = 10 + (c - 'a');
        else if (base == 16 && c >= 'A' && c <= 'F') digit = 10 + (c - 'A');
        else return -1;
        if (digit >= base) return -1;
        v = v * (uint32_t)base + (uint32_t)digit;
    }
    if (sign < 0) {
        if (v == 0) *out = 0;
        else *out = (int32_t)(-(int64_t)v);
    } else {
        *out = (int32_t)v;
    }
    return 0;
}

static char *asm_next_token(char **cursor, char *tok, int max_len) {
    char *p = *cursor;
    int n = 0;
    while (*p && (asm_is_space(*p) || *p == ',')) p++;
    if (!*p) {
        tok[0] = '\0';
        *cursor = p;
        return 0;
    }
    while (*p && !asm_is_space(*p) && *p != ',') {
        if (n < max_len - 1) tok[n++] = *p;
        p++;
    }
    tok[n] = '\0';
    while (*p && (asm_is_space(*p) || *p == ',')) p++;
    *cursor = p;
    return tok;
}

struct asm_label_entry {
    char name[20];
    int pc;
};

struct asm_program {
    char *insts[ASM_MAX_LINES];
    int line_nos[ASM_MAX_LINES];
    int count;
    struct asm_label_entry labels[ASM_MAX_LABELS];
    int label_count;
};

static int asm_find_label(const struct asm_program *prog, const char *name) {
    for (int i = 0; i < prog->label_count; i++) {
        if (strcmp(prog->labels[i].name, name) == 0) return prog->labels[i].pc;
    }
    return -1;
}

static int asm_add_label(struct asm_program *prog, const char *name, int pc) {
    if (prog->label_count >= ASM_MAX_LABELS) return -1;
    if (asm_find_label(prog, name) >= 0) return -2;
    copy_name20(prog->labels[prog->label_count].name, name);
    prog->labels[prog->label_count].pc = pc;
    prog->label_count++;
    return 0;
}

static void asm_append_text(char *out, const char *s) {
    int used = strlen(out);
    int remain = OUT_BUF_SIZE - 1 - used;
    while (*s && remain > 0) {
        out[used++] = *s++;
        remain--;
    }
    out[used] = '\0';
}

static void asm_append_int32(char *out, int32_t v) {
    char tmp[16];
    if (v < 0) {
        asm_append_text(out, "-");
        if (v == (int32_t)0x80000000) {
            uint32_t mag = 0x80000000U;
            lib_itoa(mag, tmp);
            asm_append_text(out, tmp);
            return;
        }
        v = -v;
    }
    lib_itoa((uint32_t)v, tmp);
    asm_append_text(out, tmp);
}

static void append_hex32_local(char *dst, uint32_t v) {
    static const char hex[] = "0123456789abcdef";
    char buf[8];
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[v & 0xF];
        v >>= 4;
    }
    memcpy(dst, buf, 8);
    dst[8] = '\0';
}

static int asm_build_program(char *src, struct asm_program *prog, char *err) {
    char *p = src;
    int line_no = 1;
    memset(prog, 0, sizeof(*prog));
    err[0] = '\0';
    while (*p) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') {
            *p = '\0';
            p++;
        }
        asm_strip_comment(line);
        char *s = asm_trim(line);
        if (*s == '\0') {
            line_no++;
            continue;
        }
        while (1) {
            char *colon = strchr(s, ':');
            if (!colon) break;
            {
                int valid = 1;
                char *t;
                for (t = s; t < colon; t++) {
                    if (!asm_is_ident_char(*t)) {
                        valid = 0;
                        break;
                    }
                }
                if (!valid || t == s || !asm_is_ident_start(*s)) break;
                *colon = '\0';
                s = asm_trim(s);
                if (*s == '\0') {
                    lib_strcpy(err, "asm: empty label");
                    return -1;
                }
                if (asm_add_label(prog, s, prog->count) != 0) {
                    lib_strcpy(err, "asm: duplicate/too many labels");
                    return -1;
                }
                s = asm_trim(colon + 1);
                if (*s == '\0') break;
            }
        }
        if (*s == '\0') {
            line_no++;
            continue;
        }
        if (s[0] == '.') {
            line_no++;
            continue;
        }
        if (prog->count >= ASM_MAX_LINES) {
            lib_strcpy(err, "asm: program too large");
            return -1;
        }
        prog->insts[prog->count] = s;
        prog->line_nos[prog->count] = line_no;
        prog->count++;
        line_no++;
    }
    return 0;
}

static int asm_exec_program(struct asm_program *prog, char *out) {
    int32_t regs[32];
    int pc = 0;
    int steps = 0;
    out[0] = '\0';
    memset(regs, 0, sizeof(regs));
    while (pc >= 0 && pc < prog->count) {
        char linebuf[ASM_LINE_LEN];
        char op[24], a[24], b[24], c[24];
        char *cursor;
        int rd, rs1, rs2;
        int32_t imm;
        int target;
        int line_no = prog->line_nos[pc];
        if (++steps > 200000) {
            lib_strcpy(out, "asm: step limit reached");
            return -1;
        }
        {
            int inst_len = strlen(prog->insts[pc]);
            if (inst_len >= ASM_LINE_LEN) inst_len = ASM_LINE_LEN - 1;
            memcpy(linebuf, prog->insts[pc], inst_len);
            linebuf[inst_len] = '\0';
        }
        cursor = linebuf;
        if (!asm_next_token(&cursor, op, sizeof(op))) {
            pc++;
            continue;
        }
        if (strcmp(op, "li") == 0) {
            if (!asm_next_token(&cursor, a, sizeof(a)) || !asm_next_token(&cursor, b, sizeof(b))) {
                lib_strcpy(out, "asm: li needs rd, imm");
                return -1;
            }
            rd = asm_parse_reg(a);
            if (rd < 0 || asm_parse_imm(b, &imm) != 0) {
                lib_strcpy(out, "asm: bad li operand");
                return -1;
            }
            if (rd != 0) regs[rd] = imm;
        } else if (strcmp(op, "mv") == 0) {
            if (!asm_next_token(&cursor, a, sizeof(a)) || !asm_next_token(&cursor, b, sizeof(b))) {
                lib_strcpy(out, "asm: mv needs rd, rs");
                return -1;
            }
            rd = asm_parse_reg(a);
            rs1 = asm_parse_reg(b);
            if (rd < 0 || rs1 < 0) {
                lib_strcpy(out, "asm: bad mv operand");
                return -1;
            }
            if (rd != 0) regs[rd] = regs[rs1];
        } else if (strcmp(op, "addi") == 0) {
            if (!asm_next_token(&cursor, a, sizeof(a)) || !asm_next_token(&cursor, b, sizeof(b)) || !asm_next_token(&cursor, c, sizeof(c))) {
                lib_strcpy(out, "asm: addi needs rd, rs, imm");
                return -1;
            }
            rd = asm_parse_reg(a);
            rs1 = asm_parse_reg(b);
            if (rd < 0 || rs1 < 0 || asm_parse_imm(c, &imm) != 0) {
                lib_strcpy(out, "asm: bad addi operand");
                return -1;
            }
            if (rd != 0) regs[rd] = regs[rs1] + imm;
        } else if (strcmp(op, "add") == 0 || strcmp(op, "sub") == 0) {
            if (!asm_next_token(&cursor, a, sizeof(a)) || !asm_next_token(&cursor, b, sizeof(b)) || !asm_next_token(&cursor, c, sizeof(c))) {
                lib_strcpy(out, "asm: add/sub needs rd, rs1, rs2");
                return -1;
            }
            rd = asm_parse_reg(a);
            rs1 = asm_parse_reg(b);
            rs2 = asm_parse_reg(c);
            if (rd < 0 || rs1 < 0 || rs2 < 0) {
                lib_strcpy(out, "asm: bad add/sub operand");
                return -1;
            }
            if (rd != 0) regs[rd] = (strcmp(op, "add") == 0) ? (regs[rs1] + regs[rs2]) : (regs[rs1] - regs[rs2]);
        } else if (strcmp(op, "lui") == 0) {
            if (!asm_next_token(&cursor, a, sizeof(a)) || !asm_next_token(&cursor, b, sizeof(b))) {
                lib_strcpy(out, "asm: lui needs rd, imm");
                return -1;
            }
            rd = asm_parse_reg(a);
            if (rd < 0 || asm_parse_imm(b, &imm) != 0) {
                lib_strcpy(out, "asm: bad lui operand");
                return -1;
            }
            if (rd != 0) regs[rd] = (int32_t)((uint32_t)imm << 12);
        } else if (strcmp(op, "j") == 0) {
            if (!asm_next_token(&cursor, a, sizeof(a))) {
                lib_strcpy(out, "asm: j needs label");
                return -1;
            }
            target = asm_find_label(prog, a);
            if (target < 0) {
                lib_strcpy(out, "asm: unknown label");
                return -1;
            }
            pc = target;
            continue;
        } else if (strcmp(op, "beq") == 0 || strcmp(op, "bne") == 0) {
            if (!asm_next_token(&cursor, a, sizeof(a)) || !asm_next_token(&cursor, b, sizeof(b)) || !asm_next_token(&cursor, c, sizeof(c))) {
                lib_strcpy(out, "asm: beq/bne needs rs1, rs2, label");
                return -1;
            }
            rs1 = asm_parse_reg(a);
            rs2 = asm_parse_reg(b);
            target = asm_find_label(prog, c);
            if (rs1 < 0 || rs2 < 0 || target < 0) {
                lib_strcpy(out, "asm: bad branch operand");
                return -1;
            }
            if ((strcmp(op, "beq") == 0 && regs[rs1] == regs[rs2]) ||
                (strcmp(op, "bne") == 0 && regs[rs1] != regs[rs2])) {
                pc = target;
                continue;
            }
        } else if (strcmp(op, "ecall") == 0) {
            int32_t sysno = regs[17];
            if (sysno == 1) {
                asm_append_int32(out, regs[10]);
                asm_append_text(out, "\n");
            } else if (sysno == 2) {
                char hex[16];
                hex[0] = '\0';
                append_hex32_local(hex, (uint32_t)regs[10]);
                asm_append_text(out, hex);
                asm_append_text(out, "\n");
            } else if (sysno == 10) {
                break;
            } else {
                lib_strcpy(out, "asm: unsupported ecall");
                return -1;
            }
        } else if (strcmp(op, "nop") == 0) {
            ;
        } else {
            char msg[64];
            lib_strcpy(msg, "asm: bad opcode on line ");
            char num[12];
            lib_itoa((uint32_t)line_no, num);
            lib_strcat(msg, num);
            lib_strcpy(out, msg);
            return -1;
        }
        regs[0] = 0;
        pc++;
    }
    if (out[0] == '\0') lib_strcpy(out, ">> asm done");
    return 0;
}

int run_asm_file(struct Window *term, const char *name) {
    uint32_t size = 0;
    struct asm_program prog;
    char err[128];
    if (load_file_bytes(term, name, file_io_buf, WGET_MAX_FILE_SIZE, &size) != 0) return -1;
    if (size == 0) {
        lib_strcpy(term->out_buf, "asm: empty file");
        return -1;
    }
    if (size >= WGET_MAX_FILE_SIZE - 1U) {
        lib_strcpy(term->out_buf, "asm: file too large");
        return -1;
    }
    file_io_buf[size] = '\0';
    if (asm_build_program((char *)file_io_buf, &prog, err) != 0) {
        lib_strcpy(term->out_buf, err);
        return -1;
    }
    if (asm_exec_program(&prog, term->out_buf) != 0) return -1;
    return 0;
}
