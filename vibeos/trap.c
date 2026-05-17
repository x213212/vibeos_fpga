#include "os.h"
#include "tcc_runtime.h"
extern char _text_start;
extern char _text_end;
#include "user.h"

#define CTRLDBG_PRINTF(...) do { } while (0)
#define JIT_DEBUG_MAX_BREAKPOINTS 64
#define JIT_DEBUG_LINE_MAP_MAX 512
#define JIT_DEBUG_LINE_FLAG_IS_STMT         0x0001u
#define JIT_DEBUG_LINE_FLAG_BASIC_BLOCK     0x0002u
#define JIT_DEBUG_LINE_FLAG_PROLOGUE_END    0x0004u
#define JIT_DEBUG_LINE_FLAG_EPILOGUE_BEGIN  0x0008u
#define JIT_DEBUG_LINE_FLAG_END_SEQUENCE    0x0010u
#define JIT_DEBUG_LINE_FLAG_PROBE           0x8000u
extern void trap_vector();
extern void virtio_disk_isr();
extern void virtio_net_interrupt_handler();
extern void virtio_keyboard_isr();
extern void virtio_mouse_isr();
extern void virtio_input_poll(void);
extern reg_t app_exit_resume_pc(void);
extern reg_t app_exit_stack_top(void);
extern reg_t app_heap_alloc(reg_t size);
extern void app_mark_exited(void);
extern void terminal_app_stdout_putc(int win_id, char ch);
extern void terminal_app_stdout_puts(int win_id, const char *s);
extern char gui_key;
extern void wake_terminal_worker_for_window(int win_idx);
extern int app_owner_win_id;
extern uint32_t APP_START, APP_END;
extern volatile int need_resched;
volatile int trap_skip_restore = 0;

struct jit_debug_snapshot_state {
    int valid;
    int paused;
    int task_id;
    int line;
    int step_once;
    uint32_t watch_addr;
    uint32_t watch_len;
    uint32_t watch_hit_addr;
    uint32_t watch_hit_size;
    uint32_t watch_hit_is_store;
    uint32_t watch_hit_count;
    uint32_t column;
    uint32_t discriminator;
    uint16_t line_flags;
    uint16_t line_isa;
    char file[128];
    reg_t epc;
    reg_t regs[31];
    reg_t prev_epc;
    reg_t prev_regs[31];
    int have_prev;
};

struct jit_debug_breakpoint {
    int used;
    int line;
    char file[128];
};

struct jit_debug_line_map_entry {
    int used;
    uint32_t pc;
    uint32_t end_pc;
    int line;
    uint32_t column;
    uint32_t discriminator;
    uint16_t flags;
    uint16_t isa;
    uint8_t quality;
    char file[128];
};

static struct jit_debug_snapshot_state jit_debug_last;
static struct jit_debug_breakpoint jit_debug_breakpoints[JIT_DEBUG_MAX_BREAKPOINTS];
static struct jit_debug_line_map_entry jit_debug_line_map[JIT_DEBUG_LINE_MAP_MAX];
static int jit_debug_line_map_next;
static struct {
    int active;
    uint32_t addr;
    uint32_t orig;
    int len;
} jit_debug_temp_breaks[2];
static uint32_t jit_debug_code_lo;
static uint32_t jit_debug_code_hi;
static uint32_t jit_debug_probe_pc;
reg_t jit_debug_saved_frame[31];
extern volatile int gui_redraw_needed;

static int jit_debug_addr_in_code(uint32_t addr);
static int jit_debug_addr_stepable(uint32_t addr);
void jit_debug_set_location(const char *file, int line);
static void jit_debug_apply_line_map(uint32_t pc);
static uint32_t jit_debug_read_insn(uint32_t pc);

static const char *jit_debug_addr_class(uint32_t addr) {
    if (jit_debug_addr_in_code(addr)) return "JIT";
    if (addr >= 0x80000000u && addr < 0x81000000u) return "SYS";
    if (addr >= 0x81000000u && addr < 0x88000000u) return "HEAP";
    return "EXT";
}

static void dbg_append(char *out, int out_max, const char *s) {
    int n;
    int i = 0;
    if (!out || out_max <= 0 || !s) return;
    n = strlen(out);
    while (s[i] && n < out_max - 1) {
        out[n++] = s[i++];
    }
    out[n] = '\0';
}

static inline reg_t read_ra(void) {
    reg_t ra;
    asm volatile("mv %0, ra" : "=r"(ra));
    return ra;
}

static const char *jit_debug_basename(const char *p) {
    const char *base = p;
    if (!p) return "";
    while (*p) {
        if (*p == '/' || *p == '\\') base = p + 1;
        p++;
    }
    return base;
}

static int jit_debug_file_matches(const char *bp, const char *file) {
    if (!bp || !file) return 0;
    if (strcmp(bp, file) == 0) return 1;
    if (strcmp(bp, jit_debug_basename(file)) == 0) return 1;
    if (strcmp(jit_debug_basename(bp), jit_debug_basename(file)) == 0) return 1;
    return 0;
}

static int jit_debug_breakpoint_matches(const char *file, int line) {
    for (int i = 0; i < JIT_DEBUG_MAX_BREAKPOINTS; i++) {
        if (!jit_debug_breakpoints[i].used) continue;
        if (jit_debug_breakpoints[i].line != line) continue;
        if (jit_debug_file_matches(jit_debug_breakpoints[i].file, file)) return 1;
    }
    return 0;
}

static int jit_debug_file_is_synthetic(const char *file) {
    const char *base;
    size_t len;
    if (!file || !file[0]) return 1;
    base = jit_debug_basename(file);
    len = strlen(base);
    if (strcmp(base, "<string>") == 0) return 1;
    if (len >= 2 && base[0] == '<' && base[len - 1] == '>') return 1;
    return 0;
}

static uint8_t jit_debug_line_quality(const char *file, int probe_hint) {
    if (probe_hint) return 2;
    return jit_debug_file_is_synthetic(file) ? 0 : 1;
}

static void jit_debug_write_line_entry(int slot,
                                       uint32_t start_pc,
                                       uint32_t end_pc,
                                       const char *file,
                                       int line,
                                       uint32_t column,
                                       uint32_t discriminator,
                                       uint16_t flags,
                                       uint16_t isa,
                                       uint8_t quality) {
    jit_debug_line_map[slot].used = 1;
    jit_debug_line_map[slot].pc = start_pc;
    jit_debug_line_map[slot].end_pc = end_pc;
    jit_debug_line_map[slot].line = line;
    jit_debug_line_map[slot].column = column;
    jit_debug_line_map[slot].discriminator = discriminator;
    jit_debug_line_map[slot].flags = flags;
    jit_debug_line_map[slot].isa = isa;
    jit_debug_line_map[slot].quality = quality;
    strncpy(jit_debug_line_map[slot].file, file, sizeof(jit_debug_line_map[slot].file) - 1);
    jit_debug_line_map[slot].file[sizeof(jit_debug_line_map[slot].file) - 1] = '\0';
}

static void jit_debug_record_line_span(uint32_t start_pc,
                                       uint32_t end_pc,
                                       const char *file,
                                       int line,
                                       uint32_t column,
                                       uint32_t discriminator,
                                       uint16_t flags,
                                       uint16_t isa,
                                       int probe_hint) {
    int slot;
    uint8_t quality;
    if (!start_pc || !file || !file[0] || line <= 0) return;
    if (end_pc && end_pc <= start_pc) return;
    quality = jit_debug_line_quality(file, probe_hint);
    if (probe_hint) flags |= JIT_DEBUG_LINE_FLAG_PROBE;
    for (int i = 0; i < JIT_DEBUG_LINE_MAP_MAX; i++) {
        if (!jit_debug_line_map[i].used) continue;
        if (jit_debug_line_map[i].pc != start_pc) continue;
        if (jit_debug_line_map[i].end_pc != end_pc) continue;
        if (quality < jit_debug_line_map[i].quality) return;
        jit_debug_write_line_entry(i, start_pc, end_pc, file, line,
                                   column, discriminator, flags, isa, quality);
        return;
    }
    slot = -1;
    for (int i = 0; i < JIT_DEBUG_LINE_MAP_MAX; i++) {
        if (!jit_debug_line_map[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = jit_debug_line_map_next++;
        if (jit_debug_line_map_next >= JIT_DEBUG_LINE_MAP_MAX) jit_debug_line_map_next = 0;
    }
    jit_debug_write_line_entry(slot, start_pc, end_pc, file, line,
                               column, discriminator, flags, isa, quality);
}

void jit_debug_record_line_pc(uint32_t pc, const char *file, int line) {
    jit_debug_record_line_span(pc, 0, file, line, 0, 0, 0, 0, 0);
}

void jit_debug_record_line_pc_probe(uint32_t pc, const char *file, int line) {
    jit_debug_record_line_span(pc, 0, file, line, 0, 0, 0, 0, 1);
}

void jit_debug_record_line_range(uint32_t start_pc, uint32_t end_pc, const char *file, int line) {
    jit_debug_record_line_span(start_pc, end_pc, file, line, 0, 0, 0, 0, 0);
}

void jit_debug_record_line_range_ex(uint32_t start_pc, uint32_t end_pc,
                                    const char *file, int line,
                                    uint32_t column, uint32_t discriminator,
                                    uint16_t flags, uint16_t isa) {
    jit_debug_record_line_span(start_pc, end_pc, file, line,
                               column, discriminator, flags, isa, 0);
}

static const struct jit_debug_line_map_entry *jit_debug_lookup_line_pc_entry(uint32_t pc) {
    const struct jit_debug_line_map_entry *range = NULL;
    const struct jit_debug_line_map_entry *exact = NULL;
    if (!pc) return 0;
    for (int i = 0; i < JIT_DEBUG_LINE_MAP_MAX; i++) {
        if (!jit_debug_line_map[i].used) continue;
        if (jit_debug_line_map[i].end_pc > jit_debug_line_map[i].pc &&
            pc >= jit_debug_line_map[i].pc && pc < jit_debug_line_map[i].end_pc) {
            if (!range ||
                jit_debug_line_map[i].pc > range->pc ||
                (jit_debug_line_map[i].pc == range->pc &&
                 jit_debug_line_map[i].end_pc < range->end_pc) ||
                (jit_debug_line_map[i].pc == range->pc &&
                 jit_debug_line_map[i].end_pc == range->end_pc &&
                 jit_debug_line_map[i].quality > range->quality)) {
                range = &jit_debug_line_map[i];
            }
            continue;
        }
        if (jit_debug_line_map[i].pc == pc) {
            if (!exact || jit_debug_line_map[i].quality > exact->quality) {
                exact = &jit_debug_line_map[i];
            }
        }
    }
    return range ? range : exact;
}

static int jit_debug_lookup_line_pc(uint32_t pc, char *file, int file_max, int *line,
                                    uint32_t *column, uint32_t *discriminator,
                                    uint16_t *flags, uint16_t *isa) {
    const struct jit_debug_line_map_entry *entry = jit_debug_lookup_line_pc_entry(pc);
    if (!entry) return 0;
    if (file && file_max > 0) {
        strncpy(file, entry->file, file_max - 1);
        file[file_max - 1] = '\0';
    }
    if (line) *line = entry->line;
    if (column) *column = entry->column;
    if (discriminator) *discriminator = entry->discriminator;
    if (flags) *flags = entry->flags;
    if (isa) *isa = entry->isa;
    return 1;
}

static void jit_debug_apply_line_map(uint32_t pc) {
    char file[128];
    int line = 0;
    uint32_t column = 0;
    uint32_t discriminator = 0;
    uint16_t flags = 0;
    uint16_t isa = 0;
    if (!jit_debug_lookup_line_pc(pc, file, sizeof(file), &line,
                                  &column, &discriminator, &flags, &isa)) {
        return;
    }
    jit_debug_last.column = column;
    jit_debug_last.discriminator = discriminator;
    jit_debug_last.line_flags = flags;
    jit_debug_last.line_isa = isa;
    if (jit_debug_file_is_synthetic(file)) {
        jit_debug_set_line(line);
        return;
    }
    jit_debug_set_location(file, line);
}

static void jit_debug_hex8(char *out, uint32_t v) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        out[i] = hex[(v >> (28 - i * 4)) & 0xf];
    }
    out[8] = '\0';
}

static int32_t jit_debug_sext(uint32_t v, int bits) {
    uint32_t m = 1u << (bits - 1);
    return (int32_t)((v ^ m) - m);
}

static const char *jit_debug_reg_name(int r) {
    static const char *names[32] = {
        "zero","ra","sp","gp","tp","t0","t1","t2",
        "s0","s1","a0","a1","a2","a3","a4","a5",
        "a6","a7","s2","s3","s4","s5","s6","s7",
        "s8","s9","s10","s11","t3","t4","t5","t6"
    };
    if (r < 0 || r >= 32) return "x?";
    return names[r];
}

static uint32_t jit_debug_reg_value(const reg_t *r, int idx) {
    if (idx <= 0) return 0;
    if (idx > 31 || !r) return 0;
    return (uint32_t)r[idx - 1];
}

static int jit_debug_branch_taken(uint32_t insn, const reg_t *r) {
    int f3 = (insn >> 12) & 7;
    int rs1 = (insn >> 15) & 31;
    int rs2 = (insn >> 20) & 31;
    uint32_t v1 = jit_debug_reg_value(r, rs1);
    uint32_t v2 = jit_debug_reg_value(r, rs2);
    if (f3 == 0) return v1 == v2;
    if (f3 == 1) return v1 != v2;
    if (f3 == 4) return (int32_t)v1 < (int32_t)v2;
    if (f3 == 5) return (int32_t)v1 >= (int32_t)v2;
    if (f3 == 6) return v1 < v2;
    if (f3 == 7) return v1 >= v2;
    return 0;
}

static int jit_debug_insn_len(uint32_t addr) {
    uint16_t h = *(volatile uint16_t *)(uintptr_t)addr;
    return ((h & 3u) == 3u) ? 4 : 2;
}

static uint32_t jit_debug_read_insn(uint32_t pc) {
    if (jit_debug_insn_len(pc) == 2) {
        return (uint32_t)*(volatile uint16_t *)(uintptr_t)pc;
    }
    return *(volatile uint32_t *)(uintptr_t)pc;
}

static int jit_debug_insn_is_rv32_word(uint32_t insn) {
    return (insn & 3u) == 3u;
}

static int32_t jit_debug_u_imm20(uint32_t insn) {
    return (int32_t)insn >> 12;
}

static int jit_debug_rv32_bare_mem_fault(uint32_t insn) {
    uint32_t op = insn & 0x7fu;
    uint32_t rs1 = (insn >> 15) & 31u;
    int32_t imm;
    if (op == 0x03u) {
        imm = jit_debug_sext(insn >> 20, 12);
    } else if (op == 0x23u) {
        imm = jit_debug_sext(((insn >> 25) << 5) | ((insn >> 7) & 0x1fu), 12);
    } else {
        return 0;
    }
    return rs1 == 0u && imm >= -16 && imm < 4096;
}

static int jit_debug_c_insn_valid(uint16_t h) {
    uint32_t op = h & 3u;
    uint32_t f3 = (h >> 13) & 7u;
    if (!h) return 0;
    if (op == 0u && f3 == 0u) {
        uint32_t nzuimm = ((h >> 7) & 0x30u) | ((h >> 1) & 0x3c0u) |
                          ((h >> 4) & 0x4u) | ((h >> 2) & 0x8u);
        if (!nzuimm) return 0;
    }
    return 1;
}

static int32_t jit_debug_cj_imm(uint16_t h) {
    uint32_t imm = 0;
    imm |= ((h >> 12) & 1u) << 11;
    imm |= ((h >> 11) & 1u) << 4;
    imm |= ((h >> 9) & 3u) << 8;
    imm |= ((h >> 8) & 1u) << 10;
    imm |= ((h >> 7) & 1u) << 6;
    imm |= ((h >> 6) & 1u) << 7;
    imm |= ((h >> 3) & 7u) << 1;
    imm |= ((h >> 2) & 1u) << 5;
    return jit_debug_sext(imm, 12);
}

static int32_t jit_debug_cb_imm(uint16_t h) {
    uint32_t imm = 0;
    imm |= ((h >> 12) & 1u) << 8;
    imm |= ((h >> 10) & 3u) << 3;
    imm |= ((h >> 5) & 3u) << 6;
    imm |= ((h >> 3) & 3u) << 1;
    imm |= ((h >> 2) & 1u) << 5;
    return jit_debug_sext(imm, 9);
}

static uint32_t jit_debug_step_target_compressed(uint32_t pc, uint16_t h, const reg_t *r, int *is_control, uint32_t *target) {
    uint32_t next = pc + 2;
    uint32_t f3 = (h >> 13) & 7u;
    uint32_t op = h & 3u;
    *is_control = 0;
    *target = 0;

    if (op == 1u) {
        if (f3 == 1u || f3 == 5u) { // c.jal (rv32), c.j
            *target = pc + jit_debug_cj_imm(h);
            *is_control = 1;
            return *target;
        }
        if (f3 == 6u || f3 == 7u) { // c.beqz, c.bnez
            int rs1 = 8 + ((h >> 7) & 7u);
            uint32_t v = jit_debug_reg_value(r, rs1);
            *target = pc + jit_debug_cb_imm(h);
            *is_control = 1;
            if ((f3 == 6u && v == 0) || (f3 == 7u && v != 0)) return *target;
            return next;
        }
    } else if (op == 2u) {
        if (f3 == 4u) { // misc (jr, jalr, mv, add, break)
            int bit12 = (h >> 12) & 1u;
            int rs1 = (h >> 7) & 31u;
            int rs2 = (h >> 2) & 31u;
            if (!bit12 && rs1 != 0 && rs2 == 0) { // c.jr
                *target = jit_debug_reg_value(r, rs1);
                *is_control = 1;
                return *target;
            }
            if (bit12 && rs1 != 0 && rs2 == 0) { // c.jalr
                *target = jit_debug_reg_value(r, rs1);
                *is_control = 1;
                return *target;
            }
        }
    }
    return next;
}

static uint32_t jit_debug_next_addr(uint32_t pc) {
    return pc + (uint32_t)jit_debug_insn_len(pc);
}

static void jit_debug_format_c_insn(uint32_t pc, uint16_t h, char *out, int out_max) {
    uint32_t op = h & 3u;
    uint32_t f3 = (h >> 13) & 7u;
    int rd = (h >> 7) & 31;
    int rs2 = (h >> 2) & 31;
    int imm;

    if (!out || out_max <= 0) return;
    out[0] = '\0';
    if (!h) {
        snprintf(out, out_max, "c.invalid h=%x", (uint32_t)h);
        return;
    }

    if (op == 0u) {
        if (f3 == 0u) {
            uint32_t nzuimm = ((h >> 7) & 0x30u) | ((h >> 1) & 0x3c0u) |
                              ((h >> 4) & 0x4u) | ((h >> 2) & 0x8u);
            if (!nzuimm) {
                snprintf(out, out_max, "c.invalid-addi4spn h=%x", (uint32_t)h);
                return;
            }
            snprintf(out, out_max, "c.addi4spn %s,sp,%d",
                     jit_debug_reg_name(8 + ((h >> 2) & 7u)), (int)nzuimm);
            return;
        }
        if (f3 == 2u) {
            imm = (int)(((h >> 4) & 0x4u) | ((h >> 7) & 0x38u) | ((h << 1) & 0x40u));
            snprintf(out, out_max, "c.lw %s,%d(%s)",
                     jit_debug_reg_name(8 + ((h >> 2) & 7u)), imm,
                     jit_debug_reg_name(8 + ((h >> 7) & 7u)));
            return;
        }
        if (f3 == 6u) {
            imm = (int)(((h >> 4) & 0x4u) | ((h >> 7) & 0x38u) | ((h << 1) & 0x40u));
            snprintf(out, out_max, "c.sw %s,%d(%s)",
                     jit_debug_reg_name(8 + ((h >> 2) & 7u)), imm,
                     jit_debug_reg_name(8 + ((h >> 7) & 7u)));
            return;
        }
    } else if (op == 1u) {
        if (f3 == 0u) {
            imm = jit_debug_sext(((h >> 2) & 0x1fu) | ((h >> 7) & 0x20u), 6);
            if (rd == 0 && imm == 0) snprintf(out, out_max, "c.nop");
            else snprintf(out, out_max, "c.addi %s,%d", jit_debug_reg_name(rd), imm);
            return;
        }
        if (f3 == 1u || f3 == 5u) {
            snprintf(out, out_max, "%s %x", f3 == 1u ? "c.jal" : "c.j", pc + jit_debug_cj_imm(h));
            return;
        }
        if (f3 == 2u) {
            imm = jit_debug_sext(((h >> 2) & 0x1fu) | ((h >> 7) & 0x20u), 6);
            snprintf(out, out_max, "c.li %s,%d", jit_debug_reg_name(rd), imm);
            return;
        }
        if (f3 == 3u) {
            imm = jit_debug_sext(((h >> 2) & 0x1fu) | ((h >> 7) & 0x20u), 6);
            if (rd == 2) snprintf(out, out_max, "c.addi16sp %d", imm);
            else snprintf(out, out_max, "c.lui %s,%d", jit_debug_reg_name(rd), imm);
            return;
        }
        if (f3 == 6u || f3 == 7u) {
            snprintf(out, out_max, "%s %s,%x", f3 == 6u ? "c.beqz" : "c.bnez",
                     jit_debug_reg_name(8 + ((h >> 7) & 7u)), pc + jit_debug_cb_imm(h));
            return;
        }
    } else if (op == 2u) {
        if (f3 == 0u) {
            imm = (int)(((h >> 2) & 0x1fu) | ((h >> 7) & 0x20u));
            snprintf(out, out_max, "c.slli %s,%d", jit_debug_reg_name(rd), imm);
            return;
        }
        if (f3 == 2u) {
            imm = (int)(((h >> 2) & 0x1cu) | ((h >> 7) & 0x20u) | ((h << 4) & 0x40u));
            snprintf(out, out_max, "c.lwsp %s,%d(sp)", jit_debug_reg_name(rd), imm);
            return;
        }
        if (f3 == 4u) {
            int bit12 = (h >> 12) & 1u;
            if (!bit12 && rd && !rs2) snprintf(out, out_max, "c.jr %s", jit_debug_reg_name(rd));
            else if (!bit12 && rd && rs2) snprintf(out, out_max, "c.mv %s,%s", jit_debug_reg_name(rd), jit_debug_reg_name(rs2));
            else if (bit12 && rd && !rs2) snprintf(out, out_max, "c.jalr %s", jit_debug_reg_name(rd));
            else if (bit12 && !rd && !rs2) snprintf(out, out_max, "c.ebreak");
            else if (bit12 && rd && rs2) snprintf(out, out_max, "c.add %s,%s", jit_debug_reg_name(rd), jit_debug_reg_name(rs2));
            else snprintf(out, out_max, "c.misc h=%x", (uint32_t)h);
            return;
        }
        if (f3 == 6u) {
            imm = (int)(((h >> 7) & 0x3cu) | ((h >> 1) & 0xc0u));
            snprintf(out, out_max, "c.swsp %s,%d(sp)", jit_debug_reg_name(rs2), imm);
            return;
        }
    }

    snprintf(out, out_max, "c.unknown h=%x", (uint32_t)h);
}

static int jit_debug_try_resolve_jit_stub_target(uint32_t pc, uint32_t *stub_target) {
    uint32_t insn0;
    uint32_t insn1;
    uint32_t insn2;
    uint32_t got_addr;
    int rd0;
    int rd1;
    int rs1_1;
    int rd2;
    int rs1_2;
    int32_t hi20;
    int32_t lo12;
    int32_t jalr_imm;

    if (stub_target) *stub_target = 0;
    if (!jit_debug_addr_stepable(pc)) return 0;
    if (jit_debug_insn_len(pc) != 4 ||
        jit_debug_insn_len(pc + 4) != 4 ||
        jit_debug_insn_len(pc + 8) != 4) {
        return 0;
    }

    insn0 = *(volatile uint32_t *)(uintptr_t)pc;
    insn1 = *(volatile uint32_t *)(uintptr_t)(pc + 4);
    insn2 = *(volatile uint32_t *)(uintptr_t)(pc + 8);

    if ((insn0 & 0x7f) != 0x17) return 0;
    if ((insn1 & 0x7f) != 0x03 || ((insn1 >> 12) & 7) != 2) return 0;
    if ((insn2 & 0x7f) != 0x67) return 0;

    rd0 = (insn0 >> 7) & 31;
    rd1 = (insn1 >> 7) & 31;
    rs1_1 = (insn1 >> 15) & 31;
    rd2 = (insn2 >> 7) & 31;
    rs1_2 = (insn2 >> 15) & 31;
    if (rd0 == 0 || rd1 != rd0 || rs1_1 != rd0 || rs1_2 != rd0 || rd2 == 0) return 0;

    hi20 = (int32_t)(insn0 & 0xfffff000u);
    lo12 = jit_debug_sext(insn1 >> 20, 12);
    jalr_imm = jit_debug_sext(insn2 >> 20, 12);
    got_addr = (uint32_t)(pc + hi20 + lo12);
    if (!got_addr) return 0;

    if (stub_target) *stub_target = (*(volatile uint32_t *)(uintptr_t)got_addr + (uint32_t)jalr_imm) & ~1u;
    return 1;
}

static uint32_t jit_debug_single_step_pc(uint32_t pc, uint32_t target, int rd, int *resolved_external) {
    uint32_t stub_target = 0;

    if (resolved_external) *resolved_external = 0;
    if (!rd) return target;
    if (!jit_debug_addr_stepable(target)) {
        if (resolved_external) *resolved_external = 1;
        return jit_debug_next_addr(pc);
    }
    if (jit_debug_try_resolve_jit_stub_target(target, &stub_target) &&
        !jit_debug_addr_stepable(stub_target)) {
        if (resolved_external) *resolved_external = 1;
        return jit_debug_next_addr(pc);
    }
    return target;
}

static void jit_debug_format_insn(uint32_t pc, uint32_t insn, char *out, int out_max) {
    uint32_t op = insn & 0x7f;
    int rd = (insn >> 7) & 31;
    int f3 = (insn >> 12) & 7;
    int rs1 = (insn >> 15) & 31;
    int rs2 = (insn >> 20) & 31;
    int32_t imm;

    if (!out || out_max <= 0) return;
    out[0] = '\0';

    if (!jit_debug_insn_is_rv32_word(insn)) {
        jit_debug_format_c_insn(pc, (uint16_t)(insn & 0xffffu), out, out_max);
        return;
    }

    if (insn == 0x00100073) {
        snprintf(out, out_max, "ebreak");
        return;
    }
    if (insn == 0x00000073) {
        snprintf(out, out_max, "ecall");
        return;
    }

    switch (op) {
    case 0x37:
        snprintf(out, out_max, "lui %s,0x%x", jit_debug_reg_name(rd), (uint32_t)(insn >> 12));
        break;
    case 0x17:
        imm = jit_debug_u_imm20(insn);
        if (imm < 0) snprintf(out, out_max, "auipc %s,-0x%x", jit_debug_reg_name(rd), (uint32_t)-imm);
        else snprintf(out, out_max, "auipc %s,0x%x", jit_debug_reg_name(rd), (uint32_t)imm);
        break;
    case 0x6f:
        imm = jit_debug_sext(((insn >> 31) << 20) | (((insn >> 12) & 0xff) << 12) |
                             (((insn >> 20) & 1) << 11) | (((insn >> 21) & 0x3ff) << 1), 21);
        snprintf(out, out_max, "jal %s,%x", jit_debug_reg_name(rd), pc + imm);
        break;
    case 0x67:
        imm = jit_debug_sext(insn >> 20, 12);
        snprintf(out, out_max, "jalr %s,%d(%s)", jit_debug_reg_name(rd), imm, jit_debug_reg_name(rs1));
        break;
    case 0x63:
        imm = jit_debug_sext(((insn >> 31) << 12) | (((insn >> 7) & 1) << 11) |
                             (((insn >> 25) & 0x3f) << 5) | (((insn >> 8) & 0xf) << 1), 13);
        {
            const char *mn = "b?";
            if (f3 == 0) mn = "beq";
            else if (f3 == 1) mn = "bne";
            else if (f3 == 4) mn = "blt";
            else if (f3 == 5) mn = "bge";
            else if (f3 == 6) mn = "bltu";
            else if (f3 == 7) mn = "bgeu";
            snprintf(out, out_max, "%s %s,%s,%x", mn, jit_debug_reg_name(rs1), jit_debug_reg_name(rs2), pc + imm);
        }
        break;
    case 0x03:
        imm = jit_debug_sext(insn >> 20, 12);
        {
            const char *mn = "l?";
            if (f3 == 0) mn = "lb";
            else if (f3 == 1) mn = "lh";
            else if (f3 == 2) mn = "lw";
            else if (f3 == 4) mn = "lbu";
            else if (f3 == 5) mn = "lhu";
            snprintf(out, out_max, "%s %s,%d(%s)", mn, jit_debug_reg_name(rd), imm, jit_debug_reg_name(rs1));
        }
        break;
    case 0x23:
        imm = jit_debug_sext(((insn >> 25) << 5) | ((insn >> 7) & 0x1f), 12);
        {
            const char *mn = "s?";
            if (f3 == 0) mn = "sb";
            else if (f3 == 1) mn = "sh";
            else if (f3 == 2) mn = "sw";
            snprintf(out, out_max, "%s %s,%d(%s)", mn, jit_debug_reg_name(rs2), imm, jit_debug_reg_name(rs1));
        }
        break;
    case 0x13:
        imm = jit_debug_sext(insn >> 20, 12);
        {
            const char *mn = "imm?";
            if (f3 == 0) mn = "addi";
            else if (f3 == 2) mn = "slti";
            else if (f3 == 3) mn = "sltiu";
            else if (f3 == 4) mn = "xori";
            else if (f3 == 6) mn = "ori";
            else if (f3 == 7) mn = "andi";
            else if (f3 == 1) { mn = "slli"; imm &= 31; }
            else if (f3 == 5) { mn = (insn >> 30) ? "srai" : "srli"; imm &= 31; }
            snprintf(out, out_max, "%s %s,%s,%d", mn, jit_debug_reg_name(rd), jit_debug_reg_name(rs1), imm);
        }
        break;
    case 0x33:
        {
            const char *mn = "alu?";
            if (f3 == 0) mn = (insn >> 30) ? "sub" : "add";
            else if (f3 == 1) mn = "sll";
            else if (f3 == 2) mn = "slt";
            else if (f3 == 3) mn = "sltu";
            else if (f3 == 4) mn = "xor";
            else if (f3 == 5) mn = (insn >> 30) ? "sra" : "srl";
            else if (f3 == 6) mn = "or";
            else if (f3 == 7) mn = "and";
            snprintf(out, out_max, "%s %s,%s,%s", mn, jit_debug_reg_name(rd), jit_debug_reg_name(rs1), jit_debug_reg_name(rs2));
        }
        break;
    default:
        snprintf(out, out_max, "unknown-rv32 word=%x op=%x", insn, op);
        break;
    }
}

int jit_debug_asm_dump(char *out, int out_max) {
    uint32_t pc;
    char row[128];
    char control_info[128];
    char insn_text[80];
    char hpc[9];
    if (!out || out_max <= 0) return -1;
    out[0] = '\0';
    control_info[0] = '\0';
    if (!jit_debug_last.valid) {
        snprintf(out, out_max, "asm: no paused jit code.");
        return -1;
    }
    pc = (uint32_t)jit_debug_last.epc;
    if (!jit_debug_addr_stepable(pc)) {
        snprintf(out, out_max, "asm: pc outside jit text: %x", pc);
        return -1;
    }
    snprintf(row, sizeof(row), "pc 0x%x -> %s:%d\n",
             pc,
             jit_debug_last.file[0] ? jit_debug_last.file : "?",
             jit_debug_last.line);
    dbg_append(out, out_max, row);
    if (jit_debug_last.valid) {
        uint32_t cur_pc = (uint32_t)jit_debug_last.epc;
        int cur_len = jit_debug_insn_len(cur_pc);
        uint32_t cur_insn = jit_debug_read_insn(cur_pc);
        uint32_t op = cur_insn & 0x7f;
        if (cur_len == 2) {
            int c_control = 0;
            uint32_t c_target = 0;
            uint32_t c_step = jit_debug_step_target_compressed(cur_pc, (uint16_t)cur_insn,
                                                               jit_debug_last.regs, &c_control, &c_target);
            if (c_control) {
                snprintf(control_info, sizeof(control_info), "; target=0x%x(%s) next=0x%x stop=0x%x(%s)\n",
                         c_target, jit_debug_addr_class(c_target), cur_pc + 2,
                         c_step, jit_debug_addr_class(c_step));
            }
        } else if (op == 0x6f) {
            int rd = (cur_insn >> 7) & 31;
            int32_t imm = jit_debug_sext(((cur_insn >> 31) << 20) | (((cur_insn >> 12) & 0xff) << 12) |
                                         (((cur_insn >> 20) & 1) << 11) | (((cur_insn >> 21) & 0x3ff) << 1), 21);
            uint32_t target = cur_pc + imm;
            int external = 0;
            uint32_t step = jit_debug_single_step_pc(cur_pc, target, rd, &external);
            snprintf(control_info, sizeof(control_info),
                     "; target=0x%x(%s) next=0x%x stop=0x%x %s\n",
                     target, jit_debug_addr_class(target), cur_pc + 4, step,
                     external ? "step-over external call until return" : "step-into");
        } else if (op == 0x17) {
            int rd = (cur_insn >> 7) & 31;
            int32_t hi20 = (int32_t)(cur_insn & 0xfffff000u);
            uint32_t base = cur_pc + (uint32_t)hi20;
            uint32_t next_pc = cur_pc + 4;
            if (jit_debug_addr_stepable(next_pc) && jit_debug_insn_len(next_pc) == 4) {
                uint32_t next_insn = jit_debug_read_insn(next_pc);
                uint32_t next_op = next_insn & 0x7fu;
                int next_rs1 = (next_insn >> 15) & 31;
                if (next_op == 0x67 && next_rs1 == rd) {
                    int32_t lo12 = jit_debug_sext(next_insn >> 20, 12);
                    int next_rd = (next_insn >> 7) & 31;
                    uint32_t target = (base + (uint32_t)lo12) & ~1u;
                    uint32_t stop = jit_debug_single_step_pc(next_pc, target, next_rd, 0);
                    snprintf(control_info, sizeof(control_info),
                             "; auipc/jalr target=0x%x(%s) base=0x%x lo=%d stop=0x%x\n",
                             target, jit_debug_addr_class(target), base, lo12, stop);
                } else if (next_op == 0x03 && ((next_insn >> 12) & 7u) == 2u &&
                           ((next_insn >> 7) & 31) == rd && next_rs1 == rd &&
                           jit_debug_addr_stepable(cur_pc + 8) && jit_debug_insn_len(cur_pc + 8) == 4) {
                    uint32_t jalr_insn = jit_debug_read_insn(cur_pc + 8);
                    int jalr_rs1 = (jalr_insn >> 15) & 31;
                    if ((jalr_insn & 0x7fu) == 0x67 && jalr_rs1 == rd) {
                        int32_t load_lo = jit_debug_sext(next_insn >> 20, 12);
                        int32_t jalr_lo = jit_debug_sext(jalr_insn >> 20, 12);
                        uint32_t got_addr = base + (uint32_t)load_lo;
                        uint32_t target = (*(volatile uint32_t *)(uintptr_t)got_addr + (uint32_t)jalr_lo) & ~1u;
                        int jalr_rd = (jalr_insn >> 7) & 31;
                        uint32_t stop = jit_debug_single_step_pc(cur_pc + 8, target, jalr_rd, 0);
                        snprintf(control_info, sizeof(control_info),
                                 "; auipc/lw/jalr got=0x%x target=0x%x(%s) stop=0x%x\n",
                                 got_addr, target, jit_debug_addr_class(target), stop);
                    }
                }
            }
        } else if (op == 0x67) {
            int rd = (cur_insn >> 7) & 31;
            int rs1 = (cur_insn >> 15) & 31;
            int32_t imm = jit_debug_sext(cur_insn >> 20, 12);
            uint32_t base = jit_debug_reg_value(jit_debug_last.regs, rs1);
            uint32_t target = (base + imm) & ~1u;
            int external = 0;
            uint32_t step = jit_debug_single_step_pc(cur_pc, target, rd, &external);
            if (rd == 0 && rs1 == 1 && imm == 0) {
                snprintf(control_info, sizeof(control_info),
                         "; ra=0x%x(%s) next=0x%x stop=0x%x(%s)\n",
                         target, jit_debug_addr_class(target), cur_pc + 4,
                         step, jit_debug_addr_class(step));
            } else {
                snprintf(control_info, sizeof(control_info),
                         "; base=%s=0x%x imm=%d target=0x%x(%s) next=0x%x stop=0x%x %s\n",
                         jit_debug_reg_name(rs1), base, imm,
                         target, jit_debug_addr_class(target), cur_pc + 4, step,
                         external ? "step-over external call until return" : "step-into");
            }
        } else if (op == 0x63) {
            int rs1 = (cur_insn >> 15) & 31;
            int rs2 = (cur_insn >> 20) & 31;
            uint32_t v1 = jit_debug_reg_value(jit_debug_last.regs, rs1);
            uint32_t v2 = jit_debug_reg_value(jit_debug_last.regs, rs2);
            int32_t imm = jit_debug_sext(((cur_insn >> 31) << 12) | (((cur_insn >> 7) & 1) << 11) |
                                         (((cur_insn >> 25) & 0x3f) << 5) | (((cur_insn >> 8) & 0xf) << 1), 13);
            snprintf(control_info, sizeof(control_info), "; %s=0x%x %s=0x%x %s target=0x%x(%s) fallthrough=0x%x stop=0x%x\n",
                     jit_debug_reg_name(rs1), v1,
                     jit_debug_reg_name(rs2), v2,
                     jit_debug_branch_taken(cur_insn, jit_debug_last.regs) ? "taken" : "fallthrough",
                     cur_pc + imm, jit_debug_addr_class(cur_pc + imm), cur_pc + 4,
                     jit_debug_branch_taken(cur_insn, jit_debug_last.regs) ? cur_pc + imm : cur_pc + 4);
        }
    }
    dbg_append(out, out_max, "=> = current pc; source line is last probe\n");

    {
        uint32_t prev[5];
        int prev_count = 0;
        uint32_t scan = jit_debug_code_lo ? jit_debug_code_lo : pc;
        while (scan < pc && jit_debug_addr_stepable(scan)) {
            int len = jit_debug_insn_len(scan);
            if (len != 2 && len != 4) break;
            prev[prev_count % 5] = scan;
            prev_count++;
            scan += (uint32_t)len;
        }
        for (int i = 5; i >= 1; i--) {
            int idx = prev_count - i;
            uint32_t addr;
            uint32_t insn;
            if (idx < 0) {
                dbg_append(out, out_max, "~\n");
                continue;
            }
            addr = prev[idx % 5];
            if (!jit_debug_addr_stepable(addr)) {
                dbg_append(out, out_max, "~\n");
                continue;
            }
            insn = jit_debug_read_insn(addr);
            if (insn == 0x00100073) {
                dbg_append(out, out_max, "~\n");
                continue;
            }
            jit_debug_hex8(hpc, addr);
            jit_debug_format_insn(addr, insn, insn_text, sizeof(insn_text));
            snprintf(row, sizeof(row), "   0x%s  %s\n", hpc, insn_text);
            dbg_append(out, out_max, row);
        }
    }
    {
        uint32_t insn = jit_debug_read_insn(pc);
        jit_debug_hex8(hpc, pc);
        jit_debug_format_insn(pc, insn, insn_text, sizeof(insn_text));
        snprintf(row, sizeof(row), "=> 0x%s  %s\n", hpc, insn_text);
        dbg_append(out, out_max, row);
    }
    {
        uint32_t addr = pc + (uint32_t)jit_debug_insn_len(pc);
    for (int i = 1; i <= 10; i++) {
        uint32_t insn;
        if (!jit_debug_addr_stepable(addr)) {
            dbg_append(out, out_max, "~\n");
            continue;
        }
        insn = jit_debug_read_insn(addr);
        if (insn == 0x00100073) {
            dbg_append(out, out_max, "~\n");
            continue;
        }
        jit_debug_hex8(hpc, addr);
        jit_debug_format_insn(addr, insn, insn_text, sizeof(insn_text));
        snprintf(row, sizeof(row), "   0x%s  %s\n", hpc, insn_text);
        dbg_append(out, out_max, row);
        addr += (uint32_t)jit_debug_insn_len(addr);
    }
    }
    if (control_info[0]) dbg_append(out, out_max, control_info);
    return 0;
}

static void jit_debug_append_reg_pair(char *out, int out_max,
                                      int x0, const char *n0, uint32_t v0,
                                      int changed0,
                                      int x1, const char *n1, uint32_t v1,
                                      int changed1) {
    char h0[9], h1[9], row[96];
    jit_debug_hex8(h0, v0);
    jit_debug_hex8(h1, v1);
    snprintf(row, sizeof(row), "%c x%d %s 0x%s   x%d %s 0x%s\n",
             (changed0 || changed1) ? '!' : ' ',
             x0, n0, h0, x1, n1, h1);
    dbg_append(out, out_max, row);
}

static void jit_debug_format_watch_dump(char *out, int out_max) {
    char row[128];
    static const char hex[] = "0123456789abcdef";
    uint8_t *p;
    uint32_t len;

    if (!out || out_max <= 0) return;
    out[0] = '\0';
    if (!jit_debug_last.watch_addr || !jit_debug_last.watch_len) {
        snprintf(out, out_max, "watch: none");
        return;
    }

    len = jit_debug_last.watch_len;
    if (len > 96) len = 96;
    snprintf(row, sizeof(row), "watch %x len=%u hits=%u\n",
             jit_debug_last.watch_addr,
             jit_debug_last.watch_len,
             jit_debug_last.watch_hit_count);
    dbg_append(out, out_max, row);
    if (jit_debug_last.watch_hit_count) {
        snprintf(row, sizeof(row), "last %s addr=%x size=%u\n",
                 jit_debug_last.watch_hit_is_store ? "store" : "read",
                 jit_debug_last.watch_hit_addr,
                 jit_debug_last.watch_hit_size);
        dbg_append(out, out_max, row);
    } else {
        dbg_append(out, out_max, "last: no read/store hit yet\n");
    }

    p = (uint8_t *)(uintptr_t)jit_debug_last.watch_addr;
    for (uint32_t off = 0; off < len; off += 16) {
        char bytes[16 * 3 + 1];
        int bp = 0;
        uint32_t n = len - off;
        if (n > 16) n = 16;
        for (uint32_t i = 0; i < n; i++) {
            uint8_t c = p[off + i];
            bytes[bp++] = hex[c >> 4];
            bytes[bp++] = hex[c & 15];
            bytes[bp++] = ' ';
        }
        bytes[bp] = '\0';
        {
            uint32_t line_addr = jit_debug_last.watch_addr + off;
            uint32_t line_end = line_addr + n;
            uint32_t hit_addr = jit_debug_last.watch_hit_addr;
            uint32_t hit_end = hit_addr + jit_debug_last.watch_hit_size;
            const char *mark = "  ";
            if (jit_debug_last.watch_hit_count && jit_debug_last.watch_hit_is_store &&
                hit_end > line_addr && hit_addr < line_end) {
                mark = "* ";
            }
            snprintf(row, sizeof(row), "%s%x: %s\n", mark, line_addr, bytes);
        }
        dbg_append(out, out_max, row);
    }
}

static void jit_debug_format_snapshot(char *out, int out_max,
                                      const char *title,
                                      reg_t epc, const reg_t *r,
                                      reg_t cmp_epc, const reg_t *cmp_r,
                                      int have_cmp) {
    char row[192];
    int hp = have_cmp && cmp_r;
    if (!out || out_max <= 0 || !r) return;
    out[0] = '\0';
    snprintf(row, sizeof(row), "%s\n%c pc=%x\n",
             title ? title : "JIT debug snapshot",
             (hp && cmp_epc != epc) ? '!' : ' ',
             (uint32_t)epc);
    dbg_append(out, out_max, row);
    if (jit_debug_last.file[0] || jit_debug_last.line > 0) {
        snprintf(row, sizeof(row), "source %s:%d\n",
                 jit_debug_last.file[0] ? jit_debug_last.file : "?",
                 jit_debug_last.line);
        dbg_append(out, out_max, row);
    }
    jit_debug_append_reg_pair(out, out_max, 0, "zero", 0u, 0, 1, "ra", (uint32_t)r[0], hp && cmp_r[0] != r[0]);
    jit_debug_append_reg_pair(out, out_max, 2, "sp", (uint32_t)r[1], hp && cmp_r[1] != r[1], 3, "gp", (uint32_t)r[2], hp && cmp_r[2] != r[2]);
    jit_debug_append_reg_pair(out, out_max, 4, "tp", (uint32_t)r[3], hp && cmp_r[3] != r[3], 5, "t0", (uint32_t)r[4], hp && cmp_r[4] != r[4]);
    jit_debug_append_reg_pair(out, out_max, 6, "t1", (uint32_t)r[5], hp && cmp_r[5] != r[5], 7, "t2", (uint32_t)r[6], hp && cmp_r[6] != r[6]);
    jit_debug_append_reg_pair(out, out_max, 8, "s0", (uint32_t)r[7], hp && cmp_r[7] != r[7], 9, "s1", (uint32_t)r[8], hp && cmp_r[8] != r[8]);
    jit_debug_append_reg_pair(out, out_max, 10, "a0", (uint32_t)r[9], hp && cmp_r[9] != r[9], 11, "a1", (uint32_t)r[10], hp && cmp_r[10] != r[10]);
    jit_debug_append_reg_pair(out, out_max, 12, "a2", (uint32_t)r[11], hp && cmp_r[11] != r[11], 13, "a3", (uint32_t)r[12], hp && cmp_r[12] != r[12]);
    jit_debug_append_reg_pair(out, out_max, 14, "a4", (uint32_t)r[13], hp && cmp_r[13] != r[13], 15, "a5", (uint32_t)r[14], hp && cmp_r[14] != r[14]);
    jit_debug_append_reg_pair(out, out_max, 16, "a6", (uint32_t)r[15], hp && cmp_r[15] != r[15], 17, "a7", (uint32_t)r[16], hp && cmp_r[16] != r[16]);
    jit_debug_append_reg_pair(out, out_max, 18, "s2", (uint32_t)r[17], hp && cmp_r[17] != r[17], 19, "s3", (uint32_t)r[18], hp && cmp_r[18] != r[18]);
    jit_debug_append_reg_pair(out, out_max, 20, "s4", (uint32_t)r[19], hp && cmp_r[19] != r[19], 21, "s5", (uint32_t)r[20], hp && cmp_r[20] != r[20]);
    jit_debug_append_reg_pair(out, out_max, 22, "s6", (uint32_t)r[21], hp && cmp_r[21] != r[21], 23, "s7", (uint32_t)r[22], hp && cmp_r[22] != r[22]);
    jit_debug_append_reg_pair(out, out_max, 24, "s8", (uint32_t)r[23], hp && cmp_r[23] != r[23], 25, "s9", (uint32_t)r[24], hp && cmp_r[24] != r[24]);
    jit_debug_append_reg_pair(out, out_max, 26, "s10", (uint32_t)r[25], hp && cmp_r[25] != r[25], 27, "s11", (uint32_t)r[26], hp && cmp_r[26] != r[26]);
    jit_debug_append_reg_pair(out, out_max, 28, "t3", (uint32_t)r[27], hp && cmp_r[27] != r[27], 29, "t4", (uint32_t)r[28], hp && cmp_r[28] != r[28]);
    jit_debug_append_reg_pair(out, out_max, 30, "t5", (uint32_t)r[29], hp && cmp_r[29] != r[29], 31, "t6", (uint32_t)r[30], hp && cmp_r[30] != r[30]);
}

static void jit_debug_capture_break(reg_t pc, reg_t frame) {
    reg_t *r = (reg_t *)frame;
    reg_t display_pc = pc;
    if (!jit_debug_addr_stepable((uint32_t)pc) &&
        jit_debug_addr_stepable(jit_debug_probe_pc)) {
        display_pc = (reg_t)jit_debug_probe_pc;
    }
    if (jit_debug_last.valid) {
        jit_debug_last.prev_epc = jit_debug_last.epc;
        for (int i = 0; i < 31; i++) jit_debug_last.prev_regs[i] = jit_debug_last.regs[i];
        jit_debug_last.have_prev = 1;
    } else {
        jit_debug_last.have_prev = 0;
    }
    jit_debug_last.valid = 1;
    jit_debug_last.paused = 1;
    jit_debug_last.task_id = task_current();
    jit_debug_last.epc = display_pc;
    for (int i = 0; i < 31; i++) {
        jit_debug_last.regs[i] = r[i];
        jit_debug_saved_frame[i] = r[i];
    }
    jit_debug_apply_line_map((uint32_t)display_pc);
    gui_redraw_needed = 1;
}

static void jit_debug_flush_icache(void) {
    asm volatile("fence.i" ::: "memory");
}

static void jit_debug_clear_temp_breaks(void) {
    for (int i = 0; i < 2; i++) {
        jit_debug_temp_breaks[i].active = 0;
        jit_debug_temp_breaks[i].addr = 0;
        jit_debug_temp_breaks[i].orig = 0;
        jit_debug_temp_breaks[i].len = 0;
    }
}

static void jit_debug_update_last(reg_t pc, const reg_t *r) {
    if (jit_debug_last.valid) {
        jit_debug_last.prev_epc = jit_debug_last.epc;
        for (int i = 0; i < 31; i++) jit_debug_last.prev_regs[i] = jit_debug_last.regs[i];
        jit_debug_last.have_prev = 1;
    }
    jit_debug_last.epc = pc;
    for (int i = 0; i < 31; i++) jit_debug_last.regs[i] = r[i];
    jit_debug_last.valid = 1;
    jit_debug_apply_line_map((uint32_t)pc);
    gui_redraw_needed = 1;
}

void jit_debug_set_code_range(uint32_t lo, uint32_t hi) {
    jit_debug_code_lo = lo;
    jit_debug_code_hi = hi;
}

static int jit_debug_addr_in_code(uint32_t addr) {
    if (jit_debug_code_lo && jit_debug_code_hi) {
        if (addr >= jit_debug_code_lo && addr < jit_debug_code_hi) return 1;
    }
    return 0;
}

static int jit_debug_addr_stepable(uint32_t addr) {
    return jit_debug_addr_in_code(addr);
}

static void jit_debug_restore_temp_breaks(void) {
    for (int i = 0; i < 2; i++) {
        if (!jit_debug_temp_breaks[i].active) continue;
        if (jit_debug_temp_breaks[i].len == 2) {
            *(volatile uint16_t *)(uintptr_t)jit_debug_temp_breaks[i].addr = (uint16_t)jit_debug_temp_breaks[i].orig;
        } else {
            *(volatile uint32_t *)(uintptr_t)jit_debug_temp_breaks[i].addr = jit_debug_temp_breaks[i].orig;
        }
        jit_debug_temp_breaks[i].active = 0;
    }
    jit_debug_flush_icache();
}

void jit_debug_cleanup_code_patches(void) {
    jit_debug_restore_temp_breaks();
}

static int jit_debug_temp_break_hit(uint32_t epc) {
    for (int i = 0; i < 2; i++) {
        if (jit_debug_temp_breaks[i].active && jit_debug_temp_breaks[i].addr == epc) return 1;
    }
    return 0;
}

static uint32_t jit_debug_temp_break_orig(uint32_t epc) {
    for (int i = 0; i < 2; i++) {
        if (jit_debug_temp_breaks[i].active && jit_debug_temp_breaks[i].addr == epc) {
            return jit_debug_temp_breaks[i].orig;
        }
    }
    return 0;
}

static void jit_debug_add_temp_break(uint32_t addr) {
    int len;
    if (!jit_debug_addr_stepable(addr)) return;
    for (int i = 0; i < 2; i++) {
        if (jit_debug_temp_breaks[i].active && jit_debug_temp_breaks[i].addr == addr) return;
    }
    len = jit_debug_insn_len(addr);
    if (len == 2) {
        uint16_t h = *(volatile uint16_t *)(uintptr_t)addr;
        if (!jit_debug_c_insn_valid(h)) {
            lib_printf("[JITDBG] temp reject invalid-c addr=%x h=%x\n", addr, (uint32_t)h);
            return;
        }
    } else {
        uint32_t word = *(volatile uint32_t *)(uintptr_t)addr;
        if (!jit_debug_insn_is_rv32_word(word)) {
            lib_printf("[JITDBG] temp reject invalid-rv32 addr=%x word=%x\n", addr, word);
            return;
        }
    }
    for (int i = 0; i < 2; i++) {
        if (jit_debug_temp_breaks[i].active) continue;
        jit_debug_temp_breaks[i].addr = addr;
        jit_debug_temp_breaks[i].len = len;
        if (len == 2) {
            jit_debug_temp_breaks[i].orig = *(volatile uint16_t *)(uintptr_t)addr;
            *(volatile uint16_t *)(uintptr_t)addr = 0x9002;
        } else {
            jit_debug_temp_breaks[i].orig = *(volatile uint32_t *)(uintptr_t)addr;
            *(volatile uint32_t *)(uintptr_t)addr = 0x00100073;
        }
        jit_debug_temp_breaks[i].active = 1;
        lib_printf("[JITDBG] temp set slot=%d addr=%x len=%d orig=%x\n",
                   i, addr, len, jit_debug_temp_breaks[i].orig);
        return;
    }
    lib_printf("[JITDBG] temp table full addr=%x\n", addr);
}

static int jit_debug_install_single_step_breaks(char *out, int out_max) {
    uint32_t pc = (uint32_t)jit_debug_last.epc;
    uint32_t insn;
    uint32_t op = 0;
    int insn_len;
    uint32_t next;
    uint32_t target = 0;
    uint32_t step_pc = 0;
    int has_target = 0;
    int target_required = 0;
    int is_control = 0;
    const reg_t *r = jit_debug_last.regs;

    if (!pc) return -1;
    jit_debug_restore_temp_breaks();
    if (!jit_debug_addr_stepable(pc)) {
        if (out && out_max > 0) snprintf(out, out_max, "JIT debug: si outside step text.");
        return -1;
    }
    insn_len = jit_debug_insn_len(pc);
    next = pc + (uint32_t)insn_len;
    lib_printf("[JITDBG] si begin pc=%x len=%d class=%s jit=%x..%x\n",
               pc, insn_len, jit_debug_addr_class(pc), jit_debug_code_lo, jit_debug_code_hi);
    if (insn_len == 2) {
        uint16_t h = *(volatile uint16_t *)(uintptr_t)pc;
        insn = h;
        if (!jit_debug_c_insn_valid(h)) {
            if (out && out_max > 0) {
                snprintf(out, out_max, "si invalid c pc=%x h=%x", pc, (uint32_t)h);
            }
            lib_printf("[JITDBG] si invalid c pc=%x h=%x\n", pc, (uint32_t)h);
            return -1;
        }
        step_pc = jit_debug_step_target_compressed(pc, h, r, &is_control, &target);
        has_target = is_control && target != 0;
        if (is_control && step_pc == pc) {
            if (out && out_max > 0) {
                snprintf(out, out_max, "si self-loop c pc=%x insn=%x", pc, insn);
            }
            lib_printf("[JITDBG] si self-loop c pc=%x insn=%x\n", pc, insn);
            return -1;
        }
        
        // For compressed branches, try to set breaks on both paths
        uint32_t funct3 = (h >> 13) & 7u;
        uint32_t cop = h & 3u;
        if (cop == 1u && (funct3 == 6u || funct3 == 7u)) {
            // It's a c.beqz or c.bnez
            uint32_t b_target = pc + jit_debug_cb_imm(h);
            if (jit_debug_addr_stepable(b_target)) jit_debug_add_temp_break(b_target);
            if (jit_debug_addr_stepable(next)) jit_debug_add_temp_break(next);
            if (!jit_debug_temp_breaks[0].active && !jit_debug_temp_breaks[1].active) {
                if (out && out_max > 0) snprintf(out, out_max, "JIT debug: si branch target invalid.");
                return -1;
            }
            jit_debug_flush_icache();
            if (out && out_max > 0) {
                snprintf(out, out_max, "si c-branch pc=%x insn=%x next=%x target=%x(%s) tb0=%x tb1=%x",
                         pc, insn, next, b_target, jit_debug_addr_class(b_target),
                         jit_debug_temp_breaks[0].active ? jit_debug_temp_breaks[0].addr : 0,
                         jit_debug_temp_breaks[1].active ? jit_debug_temp_breaks[1].addr : 0);
            }
            lib_printf("[JITDBG] si c-branch armed pc=%x insn=%x next=%x target=%x(%s) tb0=%x tb1=%x\n",
                       pc, insn, next, b_target, jit_debug_addr_class(b_target),
                       jit_debug_temp_breaks[0].active ? jit_debug_temp_breaks[0].addr : 0,
                       jit_debug_temp_breaks[1].active ? jit_debug_temp_breaks[1].addr : 0);
            jit_debug_update_last(pc, r);
            return 0;
        }

        if (step_pc && !jit_debug_addr_stepable(step_pc)) {
            if (has_target && step_pc == target) {
                if (out && out_max > 0) {
                    snprintf(out, out_max, "si c-out: pc=%x insn=%x target=%x(%s)",
                             pc, insn, target, jit_debug_addr_class(target));
                }
                return -1;
            }
            step_pc = next;
        }
        goto install_breaks;
    }
    insn = *(volatile uint32_t *)(uintptr_t)pc;
    if (!jit_debug_insn_is_rv32_word(insn)) {
        if (out && out_max > 0) {
            snprintf(out, out_max, "si invalid/padding pc=%x word=%x", pc, insn);
        }
        lib_printf("[JITDBG] si invalid/padding pc=%x word=%x\n", pc, insn);
        return -1;
    }
    op = insn & 0x7f;
    if (jit_debug_rv32_bare_mem_fault(insn)) {
        if (out && out_max > 0) {
            snprintf(out, out_max, "si suspect bare mem pc=%x insn=%x", pc, insn);
        }
        lib_printf("[JITDBG] si suspect bare mem pc=%x insn=%x\n", pc, insn);
        return -1;
    }
    if (op == 0x6f) {
        int rd = (insn >> 7) & 31;
        int32_t imm = jit_debug_sext(((insn >> 31) << 20) | (((insn >> 12) & 0xff) << 12) |
                                     (((insn >> 20) & 1) << 11) | (((insn >> 21) & 0x3ff) << 1), 21);
        target = pc + imm;
        has_target = 1;
        is_control = 1;
        if (target == pc) {
            if (out && out_max > 0) {
                snprintf(out, out_max, "si self-loop jal pc=%x insn=%x next=%x", pc, insn, next);
            }
            lib_printf("[JITDBG] si self-loop jal pc=%x insn=%x next=%x\n", pc, insn, next);
            return -1;
        }
        if (!rd) {
            if (!jit_debug_addr_stepable(target)) {
                if (out && out_max > 0) snprintf(out, out_max, "si j-out: pc=%x target=%x", pc, target);
                return -1;
            }
            step_pc = target;
        } else {
            step_pc = jit_debug_single_step_pc(pc, target, rd, 0);
        }
    } else if (op == 0x67) {
        int rd = (insn >> 7) & 31;
        int rs1 = (insn >> 15) & 31;
        int32_t imm = jit_debug_sext(insn >> 20, 12);
        target = (jit_debug_reg_value(r, rs1) + imm) & ~1u;
        has_target = 1;
        is_control = 1;
        if (target == pc) {
            if (out && out_max > 0) {
                snprintf(out, out_max, "si self-loop jalr pc=%x insn=%x next=%x", pc, insn, next);
            }
            lib_printf("[JITDBG] si self-loop jalr pc=%x insn=%x next=%x\n", pc, insn, next);
            return -1;
        }
        if (!rd) {
            if (!jit_debug_addr_stepable(target)) {
                if (out && out_max > 0) snprintf(out, out_max, "si jr-out: pc=%x target=%x", pc, target);
                return -1;
            }
            step_pc = target;
        } else {
            step_pc = jit_debug_single_step_pc(pc, target, rd, 0);
        }
    } else if (op == 0x63) {
        int32_t imm = jit_debug_sext(((insn >> 31) << 12) | (((insn >> 7) & 1) << 11) |
                                     (((insn >> 25) & 0x3f) << 5) | (((insn >> 8) & 0xf) << 1), 13);
        target = pc + imm;
        if (target == pc && jit_debug_branch_taken(insn, r)) {
            if (out && out_max > 0) {
                snprintf(out, out_max, "si self-loop branch pc=%x insn=%x next=%x", pc, insn, next);
            }
            lib_printf("[JITDBG] si self-loop branch pc=%x insn=%x next=%x\n", pc, insn, next);
            return -1;
        }
        // For branches, set breaks on BOTH paths to be safe
        if (jit_debug_addr_stepable(target)) jit_debug_add_temp_break(target);
        if (jit_debug_addr_stepable(next)) jit_debug_add_temp_break(next);
        if (!jit_debug_temp_breaks[0].active && !jit_debug_temp_breaks[1].active) {
            if (out && out_max > 0) snprintf(out, out_max, "JIT debug: si branch target invalid.");
            return -1;
        }
        jit_debug_flush_icache();
        if (out && out_max > 0) {
            snprintf(out, out_max, "si branch pc=%x insn=%x next=%x target=%x(%s) tb0=%x tb1=%x",
                     pc, insn, next, target, jit_debug_addr_class(target),
                     jit_debug_temp_breaks[0].active ? jit_debug_temp_breaks[0].addr : 0,
                     jit_debug_temp_breaks[1].active ? jit_debug_temp_breaks[1].addr : 0);
        }
        lib_printf("[JITDBG] si branch armed pc=%x insn=%x next=%x target=%x(%s) tb0=%x tb1=%x\n",
                   pc, insn, next, target, jit_debug_addr_class(target),
                   jit_debug_temp_breaks[0].active ? jit_debug_temp_breaks[0].addr : 0,
                   jit_debug_temp_breaks[1].active ? jit_debug_temp_breaks[1].addr : 0);
        jit_debug_update_last(pc, r); // Ensure UI updates before CPU continues
        return 0;
    } else {
        step_pc = next;
    }

    if (step_pc && !jit_debug_addr_stepable(step_pc)) {
        if (out && out_max > 0) {
            snprintf(out, out_max, "si step out: pc=%x step=%x", pc, step_pc);
        }
        return -1;
    }

install_breaks:
    if (step_pc) {
        jit_debug_add_temp_break(step_pc);
    } else {
        jit_debug_add_temp_break(next);
        if (has_target && jit_debug_addr_stepable(target)) jit_debug_add_temp_break(target);
    }
    if (!jit_debug_temp_breaks[0].active && !jit_debug_temp_breaks[1].active) {
        if (out && out_max > 0) snprintf(out, out_max, "JIT debug: si target invalid.");
        return -1;
    }
    jit_debug_flush_icache();
    if (out && out_max > 0) {
        snprintf(out, out_max, "si pc=%x insn=%x next=%x target=%x(%s) step=%x(%s)",
                 pc, insn, next, target, jit_debug_addr_class(target),
                 step_pc ? step_pc : next, jit_debug_addr_class(step_pc ? step_pc : next));
    }
    lib_printf("[JITDBG] si armed pc=%x insn=%x next=%x target=%x(%s) step=%x(%s) tb0=%x tb1=%x\n",
               pc, insn, next, target, jit_debug_addr_class(target),
               step_pc ? step_pc : next, jit_debug_addr_class(step_pc ? step_pc : next),
               jit_debug_temp_breaks[0].active ? jit_debug_temp_breaks[0].addr : 0,
               jit_debug_temp_breaks[1].active ? jit_debug_temp_breaks[1].addr : 0);
    if (insn_len == 4 && op == 0x67) {
        int rd = (insn >> 7) & 31;
        int rs1 = (insn >> 15) & 31;
        int32_t imm = jit_debug_sext(insn >> 20, 12);
        uint32_t base = jit_debug_reg_value(r, rs1);
        lib_printf("[JITDBG] si jalr detail rd=%s rs1=%s base=%x imm=%d actual=%x(%s) stop=%x(%s) mode=%s\n",
                   jit_debug_reg_name(rd), jit_debug_reg_name(rs1), base, imm,
                   target, jit_debug_addr_class(target),
                   step_pc ? step_pc : next, jit_debug_addr_class(step_pc ? step_pc : next),
                   (rd != 0 && !jit_debug_addr_stepable(target)) ? "step-over-external" : "step-into");
    }
    return 0;
}

int jit_debug_is_paused(void) {
    return jit_debug_last.valid && jit_debug_last.paused;
}

void jit_debug_set_line(int line) {
    if (line < 0) line = 0;
    jit_debug_last.line = line;
}

void jit_debug_set_location(const char *file, int line) {
    if (line < 0) line = 0;
    jit_debug_last.line = line;
    if (jit_debug_file_is_synthetic(file)) return;
    if (!file) file = "";
    strncpy(jit_debug_last.file, file, sizeof(jit_debug_last.file) - 1);
    jit_debug_last.file[sizeof(jit_debug_last.file) - 1] = '\0';
}

void jit_debug_set_pc(uint32_t pc) {
    jit_debug_probe_pc = pc;
}

int jit_debug_probe(const char *file, int line) {
    if (jit_debug_last.step_once) {
        jit_debug_last.step_once = 0;
        return 1;
    }
    return jit_debug_breakpoint_matches(file, line);
}

void jit_debug_begin(void) {
    jit_debug_clear_temp_breaks();
    memset(jit_debug_line_map, 0, sizeof(jit_debug_line_map));
    jit_debug_line_map_next = 0;
    jit_debug_last.valid = 0;
    jit_debug_last.paused = 0;
    jit_debug_last.task_id = -1;
    jit_debug_last.line = 0;
    jit_debug_last.column = 0;
    jit_debug_last.discriminator = 0;
    jit_debug_last.line_flags = 0;
    jit_debug_last.line_isa = 0;
    jit_debug_last.epc = 0;
    jit_debug_last.file[0] = '\0';
    jit_debug_last.step_once = 1;
    jit_debug_probe_pc = 0;
}

int jit_debug_set_watch(uint32_t addr, uint32_t len, char *out, int out_max) {
    if (len == 0) len = 64;
    if (len > 256) len = 256;
    jit_debug_last.watch_addr = addr;
    jit_debug_last.watch_len = len;
    jit_debug_last.watch_hit_addr = 0;
    jit_debug_last.watch_hit_size = 0;
    jit_debug_last.watch_hit_is_store = 0;
    jit_debug_last.watch_hit_count = 0;
    if (out && out_max > 0) snprintf(out, out_max, "watch %x len=%u", addr, len);
    return 0;
}

void jit_watch_access(uint32_t addr, uint32_t size, int is_store) {
    uint32_t w0 = jit_debug_last.watch_addr;
    uint32_t wlen = jit_debug_last.watch_len;
    uint32_t w1;
    uint32_t a1;
    if (!w0 || !wlen || !size) return;
    w1 = w0 + wlen;
    a1 = addr + size;
    if (a1 <= addr) a1 = addr + 1;
    if (w1 <= w0) w1 = w0 + 1;
    if (a1 <= w0 || addr >= w1) return;

    jit_debug_last.watch_hit_addr = addr;
    jit_debug_last.watch_hit_size = size;
    jit_debug_last.watch_hit_is_store = is_store ? 1u : 0u;
    jit_debug_last.watch_hit_count++;
    gui_redraw_needed = 1;
}

int jit_debug_watch_dump(char *out, int out_max) {
    if (!out || out_max <= 0) return -1;
    jit_debug_format_watch_dump(out, out_max);
    return jit_debug_last.watch_addr ? 0 : -1;
}

int jit_debug_current_line(void) {
    return jit_debug_last.line;
}

const char *jit_debug_current_file(void) {
    return jit_debug_last.file;
}

int jit_debug_continue(char *out, int out_max) {
    if (out && out_max > 0) out[0] = '\0';
    if (!jit_debug_last.valid || !jit_debug_last.paused) {
        if (out && out_max > 0) snprintf(out, out_max, "JIT debug: not paused.");
        return -1;
    }
    jit_debug_restore_temp_breaks();
    jit_debug_last.paused = 0;
    jit_debug_last.step_once = 0;
    if (jit_debug_last.task_id >= 0) task_wake(jit_debug_last.task_id);
    if (out && out_max > 0) snprintf(out, out_max, "JIT debug: continue.");
    return 0;
}

int jit_debug_step(char *out, int out_max) {
    if (out && out_max > 0) out[0] = '\0';
    if (!jit_debug_last.valid || !jit_debug_last.paused) {
        if (out && out_max > 0) snprintf(out, out_max, "JIT debug: not paused.");
        return -1;
    }
    jit_debug_last.paused = 0;
    jit_debug_last.step_once = 1;
    if (jit_debug_last.task_id >= 0) task_wake(jit_debug_last.task_id);
    if (out && out_max > 0) snprintf(out, out_max, "JIT debug: step.");
    return 0;
}

int jit_debug_step_instruction(char *out, int out_max) {
    if (out && out_max > 0) out[0] = '\0';
    if (!jit_debug_last.valid || !jit_debug_last.paused) {
        if (out && out_max > 0) snprintf(out, out_max, "JIT debug: not paused.");
        return -1;
    }
    if (jit_debug_install_single_step_breaks(out, out_max) != 0) return -1;
    jit_debug_last.paused = 0;
    jit_debug_last.step_once = 0;
    if (jit_debug_last.task_id >= 0) task_wake(jit_debug_last.task_id);
    return 0;
}

int jit_debug_add_breakpoint(const char *file, int line, char *out, int out_max) {
    if (out && out_max > 0) out[0] = '\0';
    if (!file || !file[0] || line <= 0) {
        if (out && out_max > 0) snprintf(out, out_max, "usage: :b <file.c> <line>");
        return -1;
    }
    for (int i = 0; i < JIT_DEBUG_MAX_BREAKPOINTS; i++) {
        if (!jit_debug_breakpoints[i].used) continue;
        if (jit_debug_breakpoints[i].line == line &&
            strcmp(jit_debug_breakpoints[i].file, file) == 0) {
            if (out && out_max > 0) snprintf(out, out_max, "breakpoint exists: %s:%d", file, line);
            return 0;
        }
    }
    for (int i = 0; i < JIT_DEBUG_MAX_BREAKPOINTS; i++) {
        if (jit_debug_breakpoints[i].used) continue;
        jit_debug_breakpoints[i].used = 1;
        jit_debug_breakpoints[i].line = line;
        strncpy(jit_debug_breakpoints[i].file, file, sizeof(jit_debug_breakpoints[i].file) - 1);
        jit_debug_breakpoints[i].file[sizeof(jit_debug_breakpoints[i].file) - 1] = '\0';
        if (out && out_max > 0) snprintf(out, out_max, "breakpoint %s:%d", file, line);
        return 0;
    }
    if (out && out_max > 0) snprintf(out, out_max, "breakpoint table full");
    return -1;
}

int jit_debug_snapshot(char *out, int out_max) {
    if (!out || out_max <= 0) return -1;
    out[0] = '\0';
    if (!jit_debug_last.valid) {
        snprintf(out, out_max, "JIT debug: no breakpoint captured.");
        return -1;
    }
    jit_debug_format_snapshot(out, out_max, "JIT current snapshot",
                              jit_debug_last.epc, jit_debug_last.regs,
                              jit_debug_last.prev_epc, jit_debug_last.prev_regs,
                              jit_debug_last.have_prev);
    return 0;
}

int jit_debug_snapshot_prev(char *out, int out_max) {
    if (!out || out_max <= 0) return -1;
    out[0] = '\0';
    if (!jit_debug_last.valid || !jit_debug_last.have_prev) {
        snprintf(out, out_max, "JIT debug: no previous snapshot.");
        return -1;
    }
    jit_debug_format_snapshot(out, out_max, "JIT previous snapshot (p)",
                              jit_debug_last.prev_epc, jit_debug_last.prev_regs,
                              jit_debug_last.epc, jit_debug_last.regs,
                              1);
    return 0;
}

void trap_early_init(void) {
    setup_mscratch_for_hart();
    w_mtvec((reg_t)trap_vector);
    w_mstatus(r_mstatus() & ~MSTATUS_MIE);
}

void trap_init() {
    setup_mscratch_for_hart();
    w_pmpaddr0(((reg_t)(uintptr_t)app_l2_pt) >> 2);
    w_pmpaddr1((((reg_t)(uintptr_t)app_root_pt) + sizeof(uint32_t) * 1024) >> 2);
    w_pmpaddr2(((reg_t)(uintptr_t)APP_START) >> 2);
    w_pmpaddr3(((reg_t)(uintptr_t)APP_END) >> 2);
    w_pmpcfg0(0x0f000b00);
    w_mtvec((reg_t)trap_vector);
    w_mstatus(r_mstatus() | MSTATUS_MIE); 
}

void external_handler() {
    int irq = plic_claim();
    if (irq == 1) { // Disk
        virtio_disk_isr();
    } else if (irq == 2) { // Net
        virtio_net_interrupt_handler();
    } else if (irq == 3) { // Keyboard (Slot 2)
        virtio_keyboard_isr();
    } else if (irq == 4) { // Mouse (Slot 3)
        virtio_mouse_isr();
    } else if (irq == 10) { // UART
        lib_isr();
    }
    if (irq) plic_complete(irq);
}

static void external_drain_pending(void) {
    while (1) {
        int irq = plic_claim();
        if (!irq) break;
        if (irq == 1) {
            virtio_disk_isr();
        } else if (irq == 2) {
            virtio_net_interrupt_handler();
        } else if (irq == 3) {
            virtio_keyboard_isr();
        } else if (irq == 4) {
            virtio_mouse_isr();
        } else if (irq == 10) {
            lib_isr();
        }
        plic_complete(irq);
    }
}

reg_t trap_handler(reg_t epc, reg_t cause, reg_t frame) {
    reg_t return_pc = epc;
    int advance_pc = 1;
    reg_t cause_code = cause & 0xfff;
    if (cause & 0x80000000) {
        if (cause_code == 7) {
            w_mie(r_mie() & ~(1 << 7));
            timer_handler();
            need_resched = 1;
            external_drain_pending();
            virtio_keyboard_isr();
            virtio_mouse_isr();
            virtio_input_poll();
            if (gui_key == 3 &&
                active_win_idx >= 0 && active_win_idx < MAX_WINDOWS &&
                wins[active_win_idx].active &&
                wins[active_win_idx].kind == WINDOW_KIND_TERMINAL) {
                struct Window *w = &wins[active_win_idx];
                if (w->submit_locked || w->executing_cmd || w->waiting_wget || os_jit_owner_active(w->id)) {
                    int killed = 0;
                    CTRLDBG_PRINTF("[CTRLDBG] trap win=%d exec=%d wget=%d jit=%d\n",
                                   w->id, w->executing_cmd, w->waiting_wget, os_jit_owner_active(w->id));
                    w->cancel_requested = 1;
                    killed = os_jit_cancel_running_owner_from_trap(w->id);
                    CTRLDBG_PRINTF("[CTRLDBG] trap win=%d killed=%d\n", w->id, killed);
                    if (killed > 0) {
                        w->mailbox = 1;
                        wake_terminal_worker_for_window(w->id);
                        gui_key = 0;
                        return_pc = (reg_t)(uintptr_t)os_jit_cancel_trampoline;
                        advance_pc = 0;
                    }
                    gui_key = 0;
                }
            }
            w_mie(r_mie() | MIE_MTIE);
        } else if (cause_code == 11) {
            external_handler();
        }
    } else if (cause_code == 3) {
        if (jit_debug_temp_break_hit((uint32_t)epc)) {
            uint32_t tb0 = jit_debug_temp_breaks[0].active ? jit_debug_temp_breaks[0].addr : 0;
            uint32_t tb1 = jit_debug_temp_breaks[1].active ? jit_debug_temp_breaks[1].addr : 0;
            uint32_t orig = jit_debug_temp_break_orig((uint32_t)epc);
            lib_printf("[JITDBG] trap temp-hit epc=%lx class=%s tb0=%x tb1=%x\n",
                       (unsigned long)epc, jit_debug_addr_class((uint32_t)epc), tb0, tb1);
            jit_debug_restore_temp_breaks();
            if (orig == 0x00100073u) {
                lib_printf("[JITDBG] trap temp-hit reused probe epc=%lx resume=%lx\n",
                           (unsigned long)epc, (unsigned long)(epc + 4));
                jit_debug_capture_break(epc + 4, frame);
                jit_debug_resume_pc = epc + 4;
            } else {
                jit_debug_capture_break(epc, frame);
                jit_debug_resume_pc = epc;
            }
        } else {
            lib_printf("[JITDBG] trap probe-hit epc=%lx resume=%lx class=%s jitpc=%x\n",
                       (unsigned long)epc, (unsigned long)(epc + 4),
                       jit_debug_addr_class((uint32_t)epc), jit_debug_probe_pc);
            jit_debug_capture_break(epc + 4, frame);
            jit_debug_resume_pc = epc + 4;
        }
        trap_skip_restore = 1;
        return_pc = (reg_t)(uintptr_t)os_jit_debug_pause_trampoline;
        advance_pc = 0;
    } else if (cause_code == 8 || cause_code == 11) {
        reg_t *regs = (reg_t *)frame;
        reg_t sysno = regs[16];
        if (sysno == 1) {
            if (app_owner_win_id >= 0) terminal_app_stdout_putc(app_owner_win_id, (char)regs[9]);
            else lib_putc((char)regs[9]);
        } else if (sysno == 2) {
            if (app_owner_win_id >= 0) terminal_app_stdout_puts(app_owner_win_id, (char *)regs[9]);
            else lib_puts((char *)regs[9]);
        } else if (sysno == 3) {
            regs[9] = app_heap_alloc(regs[9]);
        } else if (sysno == 4) {
            regs[9] = 0;
        } else if (sysno == 5) {
            regs[9] = (reg_t)get_millisecond_timer();
        } else if (sysno == 6) {
            need_resched = 1;
        } else if (sysno == 7) {
            regs[9] = (reg_t)appfs_open((const char *)(uintptr_t)regs[9], (int)regs[10]);
        } else if (sysno == 8) {
            regs[9] = (reg_t)appfs_read((int)regs[9], (void *)(uintptr_t)regs[10], (size_t)regs[11]);
        } else if (sysno == 9) {
            regs[9] = (reg_t)appfs_write((int)regs[9], (const void *)(uintptr_t)regs[10], (size_t)regs[11]);
        } else if (sysno == 10) {
            extern int loaded_app_exit_code;
            loaded_app_exit_code = (int)regs[9];
            regs[1] = app_exit_stack_top();
            w_satp(0);
            sfence_vma();
            w_mstatus((r_mstatus() & ~MSTATUS_MPP_MASK) | MSTATUS_MPP_M);
            return_pc = app_exit_resume_pc();
            advance_pc = 0;
        } else if (sysno == 11) {
            regs[9] = (reg_t)appfs_close((int)regs[9]);
        } else {
            panic("bad ecall");
        }
        if (advance_pc) {
            return_pc += 4;
        }
    } else {
        jit_debug_restore_temp_breaks();
        lib_printf("trap fault cause=%lu epc=%lx mtval=%lx\n",
                   (unsigned long)cause_code,
                   (unsigned long)epc,
                   (unsigned long)r_mtval());
        panic("trap fault");
    }
    return return_pc;
}
