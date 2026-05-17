#include "os.h"
#include "vga.h"
#include "timer.h"
#include "libtcc.h"
#include "tcc.h"
#undef free
#undef malloc
#undef realloc
#undef stab_section
#undef stabstr_section
#undef tcov_section
#undef dwarf_info_section
#undef dwarf_abbrev_section
#undef dwarf_line_section
#undef dwarf_aranges_section
#undef dwarf_str_section
#undef dwarf_line_str_section
#include "setjmp.h"
#include "unistd.h"
#include "sys/time.h"
#include <stdarg.h>

// --- 使用核心現有的符號 (由 user_utils.c 提供) ---
extern void *realloc(void *ptr, size_t size);
extern void *calloc(uint32_t nmemb, uint32_t size);
extern int fflush(FILE *stream);
extern int puts(const char *s);
extern char *strcpy(char *dst, const char *src);
extern char *strcat(char *dst, const char *src);
extern char *strncpy(char *dst, const char *src, uint32_t n);
extern void lib_strncat(char *dst, const char *src, int n);
extern int strncmp(const char *s1, const char *s2, uint32_t n);
extern int atoi(const char *nptr);
extern int abs(int n);

// --- 符號與 Linker 修復 ---
FILE *stdout = (FILE*)1;
int fputs(const char *s, FILE *stream) { (void)stream; lib_printf("%s", s); return 0; }
char *getcwd(char *buf, size_t size) { if (buf) lib_strcpy(buf, "/root"); return buf; }

extern int jit_debug_is_paused(void);
extern void jit_debug_set_line(int line);
extern void jit_debug_set_location(const char *file, int line);
extern void jit_debug_set_pc(uint32_t pc);
extern int jit_debug_probe(const char *file, int line);
extern void jit_debug_record_line_pc(uint32_t pc, const char *file, int line);
extern void jit_debug_record_line_pc_probe(uint32_t pc, const char *file, int line);
extern void jit_debug_record_line_range(uint32_t start_pc, uint32_t end_pc, const char *file, int line);
extern void jit_debug_record_line_range_ex(uint32_t start_pc, uint32_t end_pc,
                                           const char *file, int line,
                                           uint32_t column, uint32_t discriminator,
                                           uint16_t flags, uint16_t isa);
extern void jit_debug_begin(void);
extern void jit_debug_cleanup_code_patches(void);
extern void jit_debug_set_code_range(uint32_t lo, uint32_t hi);
extern reg_t jit_debug_saved_frame[31];
extern void jit_watch_access(uint32_t addr, uint32_t size, int is_store);

__attribute__((noinline)) void debug_break(void) {
    asm volatile("ebreak");
    while (jit_debug_is_paused()) {
        task_sleep_current();
    }
}

void debug_line(int line) {
    jit_debug_set_line(line);
}

void debug_loc(const char *file, int line) {
    jit_debug_set_location(file, line);
}

int debug_probe(const char *file, int line) {
    uint32_t caller_pc;
    asm volatile("mv %0, ra" : "=r"(caller_pc));
    jit_debug_set_pc(caller_pc);
    jit_debug_record_line_pc_probe(caller_pc, file, line);
    jit_debug_set_location(file, line);
    if (jit_debug_probe(file, line)) {
        debug_break();
    }
    return 0;
}

// 基礎字串/數字轉換
unsigned long strtoul(const char *nptr, char **endptr, int base) {
    unsigned long res = 0; const char *p = nptr;
    if (base == 0) { if (*p == '0') { p++; if (*p == 'x' || *p == 'X') { base = 16; p++; } else base = 8; } else base = 10; }
    while (*p) {
        int v = -1;
        if (*p >= '0' && *p <= '9') v = *p - '0';
        else if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') v = *p - 'A' + 10;
        if (v < 0 || v >= base) break;
        res = res * base + v; p++;
    }
    if (endptr) *endptr = (char *)p; return res;
}
long strtol(const char *nptr, char **endptr, int base) {
    if (*nptr == '-') return -(long)strtoul(nptr + 1, endptr, base);
    return (long)strtoul(nptr, endptr, base);
}
unsigned long long strtoull(const char *nptr, char **endptr, int base) { return (unsigned long long)strtoul(nptr, endptr, base); }
long long strtoll(const char *nptr, char **endptr, int base) { return (long long)strtol(nptr, endptr, base); }

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {}
int sscanf(const char *str, const char *format, ...) { return 0; }
double strtod(const char *nptr, char **endptr) { return 0.0; }
float strtof(const char *nptr, char **endptr) { return 0.0f; }
long double strtold(const char *nptr, char **endptr) { return 0.0L; }
double ldexp(double x, int exp) { return x; }
void *localtime(const long *timer) { return NULL; }

int sem_init(int *sem, int pshared, unsigned int value) { return 0; }
int sem_wait(int *sem) { return 0; }
int sem_post(int *sem) { return 0; }

static void *jit_watch_memset(void *dst, int c, size_t n) {
    void *rc = memset(dst, c, n);
    if (dst && n) jit_watch_access((uint32_t)(uintptr_t)dst, (uint32_t)n, 1);
    return rc;
}

static void *jit_watch_memcpy(void *dst, const void *src, size_t n) {
    void *rc = memcpy(dst, src, n);
    if (src && n) jit_watch_access((uint32_t)(uintptr_t)src, (uint32_t)n, 0);
    if (dst && n) jit_watch_access((uint32_t)(uintptr_t)dst, (uint32_t)n, 1);
    return rc;
}

static void *jit_watch_memmove(void *dst, const void *src, size_t n) {
    void *rc = memmove(dst, src, n);
    if (src && n) jit_watch_access((uint32_t)(uintptr_t)src, (uint32_t)n, 0);
    if (dst && n) jit_watch_access((uint32_t)(uintptr_t)dst, (uint32_t)n, 1);
    return rc;
}

// --- 檔案系統 (對接到 appfs，確保符號可見) ---
static int jit_stdio_mode_to_flags(const char *mode) {
    int plus = 0;
    char base;
    if (!mode || !mode[0]) return -1;
    base = mode[0];
    for (const char *p = mode; *p; p++) {
        if (*p == '+') plus = 1;
    }
    if (base == 'r') return plus ? (0x01 | 0x02) : 0x01;
    if (base == 'w') return plus ? (0x01 | 0x02 | 0x100 | 0x200) : (0x02 | 0x100 | 0x200);
    if (base == 'a') return plus ? (0x01 | 0x02 | 0x100) : (0x02 | 0x100);
    return -1;
}

FILE *fopen(const char *path, const char *mode) {
    int flags = jit_stdio_mode_to_flags(mode);
    int fd;
    if (flags < 0) return NULL;
    fd = appfs_open(path, flags);
    if (fd < 0) return NULL;
    if (mode && mode[0] == 'a' && appfs_seek(fd, 0, SEEK_END) < 0) {
        appfs_close(fd);
        return NULL;
    }
    int *pfd = malloc(sizeof(int));
    if (!pfd) {
        appfs_close(fd);
        return NULL;
    }
    *pfd = fd;
    return (FILE*)pfd;
}
int fclose(FILE *fp) {
    if (!fp) return -1;
    appfs_close(*(int*)fp);
    free(fp);
    return 0;
}
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp) {
    if (!fp) return 0;
    int rc = appfs_read(*(int*)fp, ptr, size * nmemb);
    if (rc > 0 && ptr) jit_watch_access((uint32_t)(uintptr_t)ptr, (uint32_t)rc, 1);
    return (rc > 0) ? (size_t)rc / size : 0;
}
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp) {
    if (!fp) return 0;
    if (ptr && size && nmemb) jit_watch_access((uint32_t)(uintptr_t)ptr, (uint32_t)(size * nmemb), 0);
    int rc = appfs_write(*(int*)fp, ptr, size * nmemb);
    return (rc > 0) ? (size_t)rc / size : 0;
}
int fseek(FILE *fp, long offset, int whence) {
    if (!fp) return -1;
    return appfs_seek(*(int*)fp, (int)offset, whence) < 0 ? -1 : 0;
}
long ftell(FILE *fp) {
    if (!fp) return -1;
    return (long)appfs_tell(*(int*)fp);
}

// 基礎系統調用
int open(const char *pathname, int flags, ...) { return appfs_open(pathname, flags); }
int close(int fd) { return appfs_close(fd); }
ssize_t read(int fd, void *buf, size_t count) { return appfs_read(fd, buf, count); }
ssize_t write(int fd, const void *buf, size_t count) { return appfs_write(fd, buf, count); }
off_t lseek(int fd, off_t offset, int whence) { return (off_t)appfs_seek(fd, (int)offset, whence); }
int unlink(const char *pathname) { return appfs_unlink(pathname); }
int remove(const char *pathname) { return appfs_unlink(pathname); }
int getpagesize(void) { return 4096; }
void *mmap(void *addr, size_t length, int prot, int flags, int fd, size_t offset) { return malloc(length); }
int munmap(void *addr, size_t length) { free(addr); return 0; }
int mprotect(void *addr, size_t length, int prot) { return 0; }

int setjmp(jmp_buf env) {
    asm volatile ("sw ra, 0(a0); sw sp, 4(a0); sw s0, 8(a0); sw s1, 12(a0); sw s2, 16(a0); sw s3, 20(a0); sw s4, 24(a0); sw s5, 28(a0); sw s6, 32(a0); sw s7, 36(a0); sw s8, 40(a0); sw s9, 44(a0); sw s10, 48(a0); sw s11, 52(a0); li a0, 0; ret");
    return 0;
}
void longjmp(jmp_buf env, int val) {
    asm volatile ("lw ra, 0(a0); lw sp, 4(a0); lw s0, 8(a0); lw s1, 12(a0); lw s2, 16(a0); lw s3, 20(a0); lw s4, 24(a0); lw s5, 28(a0); lw s6, 32(a0); lw s7, 36(a0); lw s8, 40(a0); lw s9, 44(a0); lw s10, 48(a0); lw s11, 52(a0); mv a0, a1; ret");
}

char *empty_environ[] = { NULL };
char **environ = empty_environ;
void tcc_error_report(void *opaque, const char *msg) { lib_printf("TCC Error: %s\n", msg); }

#ifdef FPGA_MINIMAL
#define JIT_UHEAP_SIZE (1024u * 1024u)
#define JIT_MAX_JOBS 1
#define JIT_SHARED_SIZE (128u * 1024u)
#define JIT_SOURCE_MAX_SIZE (128u * 1024u)
#else
#define JIT_UHEAP_SIZE (16u * 1024u * 1024u)
#define JIT_MAX_JOBS 4
#define JIT_SHARED_SIZE (1024u * 1024u)
#define JIT_SOURCE_MAX_SIZE (512u * 1024u)
#endif
#define JIT_UHEAP_MAGIC 0x55484d30u /* "UHM0" */
#define JIT_MAX_KPTRS 32
#define JIT_MAX_FDS 32
#define JIT_TIMESLICE_MS 10u
#define JIT_INCLUDE_MAX_DEPTH 16
#define JIT_SEEN_MAX 64
#define JIT_DEBUG_PROBE_MAX 512

enum {
    JIT_JOB_EMPTY = 0,
    JIT_JOB_COMPILING = 1,
    JIT_JOB_READY = 2,
    JIT_JOB_RUNNING = 3,
    JIT_JOB_DONE = 4,
    JIT_JOB_FAILED = 5
};

struct jit_ublock {
    uint32_t magic;
    uint32_t size;
    uint32_t free;
    struct jit_ublock *next;
};

struct jit_job {
    int id;
    int state;
    int bg;
    int owner_win_id;
    int task_id;
    int waiter_task_id;
    int cancel_requested;
    int exit_code;
    unsigned int slice_start_ms;
    TCCState *tcc;
    int (*main_func)(void);
    uint32_t text_lo;
    uint32_t text_hi;
    uint8_t *uheap;
    struct jit_ublock *uheap_head;
    void *kptrs[JIT_MAX_KPTRS];
    int fds[JIT_MAX_FDS];
};

struct jit_debug_probe_site {
    int line;
    char file[128];
};

static uint8_t jit_user_heaps[JIT_MAX_JOBS][JIT_UHEAP_SIZE];
static uint8_t jit_shared_area[JIT_SHARED_SIZE];
static struct jit_job jit_jobs[JIT_MAX_JOBS];
static int jit_next_id = 1;
static int jit_initialized = 0;
int jit_debug_codegen_watch_enabled = 0;
volatile reg_t jit_debug_resume_pc = 0;
static struct jit_debug_probe_site jit_debug_probe_sites[JIT_DEBUG_PROBE_MAX];
static int jit_debug_probe_site_count = 0;

struct jit_source_buf {
    char *data;
    size_t len;
    size_t cap;
};

void os_jit_init(void);
int os_jit_cancel_by_owner(int owner_win_id);
static void jit_yield(void);
static void jit_maybe_yield(void);

static void jit_debug_probe_sites_reset(void) {
    jit_debug_probe_site_count = 0;
}

static void jit_debug_probe_sites_add(const char *path, int line) {
    if (!path || line <= 0) return;
    if (jit_debug_probe_site_count >= JIT_DEBUG_PROBE_MAX) return;
    jit_debug_probe_sites[jit_debug_probe_site_count].line = line;
    strncpy(jit_debug_probe_sites[jit_debug_probe_site_count].file, path,
            sizeof(jit_debug_probe_sites[jit_debug_probe_site_count].file) - 1);
    jit_debug_probe_sites[jit_debug_probe_site_count].file[
        sizeof(jit_debug_probe_sites[jit_debug_probe_site_count].file) - 1] = '\0';
    jit_debug_probe_site_count++;
}

static void jit_debug_build_line_map_from_ebreaks(uint32_t lo, uint32_t hi) {
    int idx = 0;
    if (!lo || !hi || hi <= lo) return;
    for (uint32_t pc = lo; pc + 4 <= hi && idx < jit_debug_probe_site_count; pc += 4) {
        uint32_t insn = *(volatile uint32_t *)(uintptr_t)pc;
        if (insn != 0x00100073) continue;
        jit_debug_record_line_pc_probe(pc + 4,
                                       jit_debug_probe_sites[idx].file,
                                       jit_debug_probe_sites[idx].line);
        idx++;
    }
}

#define JIT_DWARF_DIR_TABLE_MAX 64
#define JIT_DWARF_FILE_TABLE_MAX 256
#define JIT_DEBUG_LINE_FLAG_IS_STMT         0x0001u
#define JIT_DEBUG_LINE_FLAG_BASIC_BLOCK     0x0002u
#define JIT_DEBUG_LINE_FLAG_PROLOGUE_END    0x0004u
#define JIT_DEBUG_LINE_FLAG_EPILOGUE_BEGIN  0x0008u
#define JIT_DEBUG_LINE_FLAG_END_SEQUENCE    0x0010u

struct dwarf_entry_format {
    unsigned type;
    unsigned form;
};

struct dwarf_filename_entry {
    unsigned dir_index;
    char *name;
};

struct jit_dwarf_line_state {
    addr_t address;
    unsigned file_index;
    unsigned column;
    unsigned op_index;
    unsigned isa;
    unsigned discriminator;
    int line;
    unsigned is_stmt;
    unsigned basic_block;
    unsigned prologue_end;
    unsigned epilogue_begin;
    unsigned end_sequence;
    unsigned file_index_is_default;
};

struct jit_dwarf_pending_row {
    int valid;
    uint32_t start_pc;
    int line;
    uint32_t column;
    uint32_t discriminator;
    uint16_t flags;
    uint16_t isa;
    char file[256];
};

struct jit_dwarf_line_stats {
    unsigned rows;
    unsigned ranges;
    unsigned sequences;
    unsigned stmt_rows;
    unsigned prologue_rows;
    unsigned epilogue_rows;
    unsigned synthetic_rows;
    unsigned logged_ranges;
    unsigned dropped_logs;
};

static unsigned long long jit_dwarf_read_uleb128(unsigned char **p, unsigned char *end) {
    unsigned long long value = 0;
    unsigned shift = 0;
    while (*p < end) {
        unsigned char byte = *(*p)++;
        value |= (unsigned long long)(byte & 0x7f) << shift;
        if (!(byte & 0x80)) break;
        shift += 7;
    }
    return value;
}

static long long jit_dwarf_read_sleb128(unsigned char **p, unsigned char *end) {
    unsigned long long value = 0;
    unsigned shift = 0;
    unsigned char byte = 0;
    while (*p < end) {
        byte = *(*p)++;
        value |= (unsigned long long)(byte & 0x7f) << shift;
        shift += 7;
        if (!(byte & 0x80)) break;
    }
    if ((shift < 64) && (byte & 0x40)) value |= ~0ULL << shift;
    return (long long)value;
}

static unsigned jit_dwarf_read_1(unsigned char **p, unsigned char *end) {
    if (*p >= end) return 0;
    return *(*p)++;
}

static unsigned jit_dwarf_read_2(unsigned char **p, unsigned char *end) {
    unsigned v0 = jit_dwarf_read_1(p, end);
    unsigned v1 = jit_dwarf_read_1(p, end);
    return v0 | (v1 << 8);
}

static unsigned long long jit_dwarf_read_4(unsigned char **p, unsigned char *end) {
    unsigned long long v = 0;
    for (int i = 0; i < 4; i++) v |= (unsigned long long)jit_dwarf_read_1(p, end) << (i * 8);
    return v;
}

static unsigned long long jit_dwarf_read_8(unsigned char **p, unsigned char *end) {
    unsigned long long v = 0;
    for (int i = 0; i < 8; i++) v |= (unsigned long long)jit_dwarf_read_1(p, end) << (i * 8);
    return v;
}

static void jit_dwarf_skip_form(unsigned char **p, unsigned char *end, unsigned form, unsigned length) {
    switch (form) {
    case DW_FORM_string:
        while (*p < end && **p) (*p)++;
        if (*p < end) (*p)++;
        break;
    case DW_FORM_data1:
        jit_dwarf_read_1(p, end);
        break;
    case DW_FORM_data2:
        jit_dwarf_read_2(p, end);
        break;
    case DW_FORM_data4:
    case DW_FORM_line_strp:
    case DW_FORM_strp:
    case DW_FORM_sec_offset:
        jit_dwarf_read_4(p, end);
        break;
    case DW_FORM_data8:
        jit_dwarf_read_8(p, end);
        break;
    case DW_FORM_udata:
        jit_dwarf_read_uleb128(p, end);
        break;
    case DW_FORM_flag_present:
        break;
    default:
        if (length == 8) jit_dwarf_read_8(p, end);
        else jit_dwarf_read_4(p, end);
        break;
    }
}

static addr_t jit_dwarf_read_addr(unsigned char **p, unsigned char *end, unsigned addr_size) {
    addr_t value = 0;
    if (!addr_size) addr_size = sizeof(addr_t);
    if (addr_size > sizeof(addr_t)) addr_size = sizeof(addr_t);
    for (unsigned i = 0; i < addr_size && *p < end; i++) {
        value |= (addr_t)jit_dwarf_read_1(p, end) << (i * 8);
    }
    return value;
}

static char *jit_dwarf_read_path_form(unsigned char **p, unsigned char *end, unsigned form,
                                      unsigned length, unsigned char *line_str, unsigned char *debug_str) {
    unsigned long long off;
    char *path = NULL;
    switch (form) {
    case DW_FORM_string:
        path = (char *)*p;
        while (*p < end && **p) (*p)++;
        if (*p < end) (*p)++;
        break;
    case DW_FORM_line_strp:
        off = (length == 8) ? jit_dwarf_read_8(p, end) : jit_dwarf_read_4(p, end);
        if (line_str) path = (char *)line_str + off;
        break;
    case DW_FORM_strp:
        off = (length == 8) ? jit_dwarf_read_8(p, end) : jit_dwarf_read_4(p, end);
        if (debug_str) path = (char *)debug_str + off;
        break;
    default:
        jit_dwarf_skip_form(p, end, form, length);
        break;
    }
    return path;
}

static void jit_dwarf_reset_line_state(struct jit_dwarf_line_state *state, unsigned default_is_stmt) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->file_index = 1;
    state->line = 1;
    state->is_stmt = default_is_stmt ? 1u : 0u;
    state->file_index_is_default = 1;
}

static void jit_dwarf_advance_addr(struct jit_dwarf_line_state *state,
                                   unsigned long long op_advance,
                                   unsigned min_insn_length,
                                   unsigned max_ops_per_insn) {
    if (!state) return;
    if (!max_ops_per_insn) max_ops_per_insn = 1;
    if (max_ops_per_insn == 1) {
        state->address += (addr_t)(op_advance * min_insn_length);
        return;
    }
    state->address += (addr_t)(((state->op_index + op_advance) / max_ops_per_insn) * min_insn_length);
    state->op_index = (unsigned)((state->op_index + op_advance) % max_ops_per_insn);
}

static uint16_t jit_dwarf_line_flags(const struct jit_dwarf_line_state *state) {
    uint16_t flags = 0;
    if (!state) return 0;
    if (state->is_stmt) flags |= JIT_DEBUG_LINE_FLAG_IS_STMT;
    if (state->basic_block) flags |= JIT_DEBUG_LINE_FLAG_BASIC_BLOCK;
    if (state->prologue_end) flags |= JIT_DEBUG_LINE_FLAG_PROLOGUE_END;
    if (state->epilogue_begin) flags |= JIT_DEBUG_LINE_FLAG_EPILOGUE_BEGIN;
    if (state->end_sequence) flags |= JIT_DEBUG_LINE_FLAG_END_SEQUENCE;
    return flags;
}

static void jit_debug_join_path(char *out, int out_max, const char *dir, const char *name) {
    if (!out || out_max <= 0) return;
    out[0] = '\0';
    if (!name || !name[0]) return;
    if (name[0] == '/' || !dir || !dir[0]) {
        strncpy(out, name, out_max - 1);
        out[out_max - 1] = '\0';
        return;
    }
    strncpy(out, dir, out_max - 1);
    out[out_max - 1] = '\0';
    if (out[0] && out[strlen(out) - 1] != '/') lib_strncat(out, "/", out_max - (int)strlen(out) - 1);
    lib_strncat(out, name, out_max - (int)strlen(out) - 1);
}

static int jit_debug_path_is_synthetic(const char *path) {
    const char *base = path;
    size_t len;
    if (!path || !path[0]) return 1;
    while (*path) {
        if (*path == '/' || *path == '\\') base = path + 1;
        path++;
    }
    len = strlen(base);
    if (strcmp(base, "<string>") == 0) return 1;
    if (len >= 2 && base[0] == '<' && base[len - 1] == '>') return 1;
    return 0;
}

static const char *jit_debug_path_for_index(char *out, int out_max,
                                            struct dwarf_filename_entry *files,
                                            unsigned file_count,
                                            char **dirs, unsigned dir_count,
                                            unsigned file_index) {
    const char *dir = NULL;
    if (!out || out_max <= 0) return NULL;
    out[0] = '\0';
    if (!file_count || file_index >= file_count) return NULL;
    if (!files[file_index].name || !files[file_index].name[0]) return NULL;
    if (files[file_index].dir_index < dir_count) {
        dir = dirs[files[file_index].dir_index];
    } else if (files[file_index].dir_index > 0 &&
               files[file_index].dir_index - 1 < dir_count) {
        dir = dirs[files[file_index].dir_index - 1];
    }
    jit_debug_join_path(out, out_max, dir, files[file_index].name);
    return out[0] ? out : files[file_index].name;
}

static const char *jit_debug_current_fullpath(char *out, int out_max,
                                              struct dwarf_filename_entry *files,
                                              unsigned file_count,
                                              char **dirs, unsigned dir_count,
                                              unsigned file_index,
                                              int file_index_is_default) {
    char direct[256];
    char fallback[256];
    const char *primary = NULL;
    const char *secondary = NULL;
    if (!out || out_max <= 0) return NULL;
    out[0] = '\0';
    if (!file_count) return NULL;
    if (file_index_is_default) {
        if (file_index > 0) {
            primary = jit_debug_path_for_index(fallback, sizeof(fallback),
                                               files, file_count, dirs, dir_count,
                                               file_index - 1);
        }
        secondary = jit_debug_path_for_index(direct, sizeof(direct),
                                             files, file_count, dirs, dir_count,
                                             file_index);
    } else {
        primary = jit_debug_path_for_index(direct, sizeof(direct),
                                           files, file_count, dirs, dir_count,
                                           file_index);
        if (file_index > 0) {
            secondary = jit_debug_path_for_index(fallback, sizeof(fallback),
                                                 files, file_count, dirs, dir_count,
                                                 file_index - 1);
        }
    }
    if (primary && primary[0] && !jit_debug_path_is_synthetic(primary)) {
        strncpy(out, primary, out_max - 1);
        out[out_max - 1] = '\0';
        return out;
    }
    if (secondary && secondary[0] && !jit_debug_path_is_synthetic(secondary)) {
        strncpy(out, secondary, out_max - 1);
        out[out_max - 1] = '\0';
        return out;
    }
    if (primary && primary[0]) {
        strncpy(out, primary, out_max - 1);
        out[out_max - 1] = '\0';
        return out;
    }
    if (secondary && secondary[0]) {
        strncpy(out, secondary, out_max - 1);
        out[out_max - 1] = '\0';
        return out;
    }
    return NULL;
}

static void jit_dwarf_emit_pending_range(struct jit_dwarf_pending_row *pending,
                                         uint32_t end_pc,
                                         struct jit_dwarf_line_stats *stats) {
    if (!pending || !pending->valid) return;
    if (end_pc <= pending->start_pc) {
        pending->valid = 0;
        return;
    }
    jit_debug_record_line_range_ex(pending->start_pc, end_pc,
                                   pending->file, pending->line,
                                   pending->column, pending->discriminator,
                                   pending->flags, pending->isa);
    if (stats) {
        stats->ranges++;
        if (stats->logged_ranges < 96) {
            lib_printf("[JITDBG] dwarf row %x..%x %s:%d col=%d flags=%x discr=%d isa=%d\n",
                       pending->start_pc, end_pc,
                       pending->file, pending->line,
                       pending->column, pending->flags,
                       pending->discriminator, pending->isa);
            stats->logged_ranges++;
        } else {
            stats->dropped_logs++;
        }
    }
    pending->valid = 0;
}

static void jit_dwarf_commit_row(struct jit_dwarf_pending_row *pending,
                                 const struct jit_dwarf_line_state *state,
                                 struct dwarf_filename_entry *files,
                                 unsigned file_count,
                                 char **dirs, unsigned dir_count,
                                 uint32_t text_lo,
                                 struct jit_dwarf_line_stats *stats) {
    char fullpath[256];
    const char *full;
    uint16_t flags;
    uint32_t row_pc;
    if (!pending || !state) return;
    row_pc = (uint32_t)state->address;
    jit_dwarf_emit_pending_range(pending, row_pc, stats);
    if (stats) {
        stats->rows++;
        if (state->is_stmt) stats->stmt_rows++;
        if (state->prologue_end) stats->prologue_rows++;
        if (state->epilogue_begin) stats->epilogue_rows++;
        if (state->end_sequence) stats->sequences++;
    }
    if (state->end_sequence) return;
    if (row_pc < text_lo) return;
    if (state->line <= 0) return;
    full = jit_debug_current_fullpath(fullpath, sizeof(fullpath),
                                      files, file_count, dirs, dir_count,
                                      state->file_index,
                                      state->file_index_is_default);
    if (!full || !full[0]) return;
    if (jit_debug_path_is_synthetic(full)) {
        if (stats) stats->synthetic_rows++;
        return;
    }
    flags = jit_dwarf_line_flags(state);
    pending->valid = 1;
    pending->start_pc = row_pc;
    pending->line = state->line;
    pending->column = state->column;
    pending->discriminator = state->discriminator;
    pending->flags = flags;
    pending->isa = (uint16_t)state->isa;
    strncpy(pending->file, full, sizeof(pending->file) - 1);
    pending->file[sizeof(pending->file) - 1] = '\0';
}

static void jit_dwarf_clear_row_flags(struct jit_dwarf_line_state *state) {
    if (!state) return;
    state->basic_block = 0;
    state->prologue_end = 0;
    state->epilogue_begin = 0;
    state->discriminator = 0;
}

static int jit_debug_build_line_map_from_dwarf(TCCState *s) {
    unsigned char *ln;
    unsigned char *end_all;
    unsigned char *line_str = NULL;
    unsigned char *debug_str = NULL;
    int mapped = 0;
    struct jit_dwarf_line_stats total_stats;

    memset(&total_stats, 0, sizeof(total_stats));

    if (!s || !s->dwarf_line_section || !s->dwarf_line_section->data) return 0;
    if (s->dwarf_line_str_section && s->dwarf_line_str_section->data) line_str = s->dwarf_line_str_section->data;
    if (s->dwarf_str_section && s->dwarf_str_section->data) debug_str = s->dwarf_str_section->data;

    ln = s->dwarf_line_section->data;
    end_all = s->dwarf_line_section->data + s->dwarf_line_section->data_offset;
    while (ln < end_all) {
        unsigned char *unit = ln;
        unsigned char *end;
        unsigned char *opcode_lengths;
        unsigned long long size = jit_dwarf_read_4(&ln, end_all);
        unsigned length = 4;
        unsigned version;
        unsigned address_size = sizeof(addr_t);
        unsigned segment_selector_size = 0;
        unsigned long long header_length = 0;
        unsigned min_insn_length;
        unsigned max_ops_per_insn = 1;
        unsigned default_is_stmt;
        int line_base;
        unsigned line_range;
        unsigned opcode_base;
        char *dirs[JIT_DWARF_DIR_TABLE_MAX];
        struct dwarf_filename_entry files[JIT_DWARF_FILE_TABLE_MAX];
        unsigned dir_count = 0;
        unsigned file_count = 0;
        struct jit_dwarf_line_state state;
        struct jit_dwarf_pending_row pending;
        struct jit_dwarf_line_stats stats;

        memset(dirs, 0, sizeof(dirs));
        memset(files, 0, sizeof(files));
        memset(&pending, 0, sizeof(pending));
        memset(&stats, 0, sizeof(stats));

        if (size == 0xffffffffu) {
            length = 8;
            size = jit_dwarf_read_8(&ln, end_all);
        }
        end = ln + size;
        if (end < unit || end > end_all) break;

        version = jit_dwarf_read_2(&ln, end);
        if (version >= 5) {
            address_size = jit_dwarf_read_1(&ln, end);
            segment_selector_size = jit_dwarf_read_1(&ln, end);
            header_length = (length == 8) ? jit_dwarf_read_8(&ln, end) : jit_dwarf_read_4(&ln, end);
        } else {
            header_length = (length == 8) ? jit_dwarf_read_8(&ln, end) : jit_dwarf_read_4(&ln, end);
        }
        (void)segment_selector_size;
        if (ln + header_length > end) {
            ln = end;
            continue;
        }
        min_insn_length = jit_dwarf_read_1(&ln, end);
        if (version >= 4) max_ops_per_insn = jit_dwarf_read_1(&ln, end);
        default_is_stmt = jit_dwarf_read_1(&ln, end);
        line_base = (int)(signed char)jit_dwarf_read_1(&ln, end);
        line_range = jit_dwarf_read_1(&ln, end);
        opcode_base = jit_dwarf_read_1(&ln, end);
        if (!line_range || !opcode_base || ln + (opcode_base - 1) > end) {
            ln = end;
            continue;
        }
        opcode_lengths = ln;
        ln += opcode_base - 1;

        if (version >= 5) {
            struct dwarf_entry_format dir_fmt[16];
            struct dwarf_entry_format file_fmt[16];
            unsigned actual_dir_count;
            unsigned actual_file_count;
            unsigned fmt_count = jit_dwarf_read_1(&ln, end);
            for (unsigned i = 0; i < fmt_count && i < 16; i++) {
                dir_fmt[i].type = jit_dwarf_read_uleb128(&ln, end);
                dir_fmt[i].form = jit_dwarf_read_uleb128(&ln, end);
            }
            actual_dir_count = (unsigned)jit_dwarf_read_uleb128(&ln, end);
            dir_count = actual_dir_count;
            if (dir_count > JIT_DWARF_DIR_TABLE_MAX) dir_count = JIT_DWARF_DIR_TABLE_MAX;
            for (unsigned i = 0; i < actual_dir_count; i++) {
                for (unsigned j = 0; j < fmt_count && j < 16; j++) {
                    if (dir_fmt[j].type == DW_LNCT_path) {
                        char *path = jit_dwarf_read_path_form(&ln, end, dir_fmt[j].form, length,
                                                              line_str, debug_str);
                        if (i < dir_count) dirs[i] = path;
                    } else {
                        jit_dwarf_skip_form(&ln, end, dir_fmt[j].form, length);
                    }
                }
            }

            fmt_count = jit_dwarf_read_1(&ln, end);
            for (unsigned i = 0; i < fmt_count && i < 16; i++) {
                file_fmt[i].type = jit_dwarf_read_uleb128(&ln, end);
                file_fmt[i].form = jit_dwarf_read_uleb128(&ln, end);
            }
            actual_file_count = (unsigned)jit_dwarf_read_uleb128(&ln, end);
            file_count = actual_file_count;
            if (file_count > JIT_DWARF_FILE_TABLE_MAX) file_count = JIT_DWARF_FILE_TABLE_MAX;
            for (unsigned i = 0; i < actual_file_count; i++) {
                for (unsigned j = 0; j < fmt_count && j < 16; j++) {
                    if (file_fmt[j].type == DW_LNCT_path) {
                        char *path = jit_dwarf_read_path_form(&ln, end, file_fmt[j].form, length,
                                                              line_str, debug_str);
                        if (i < file_count) files[i].name = path;
                    } else if (file_fmt[j].type == DW_LNCT_directory_index) {
                        unsigned dir_index;
                        if (file_fmt[j].form == DW_FORM_data1) dir_index = jit_dwarf_read_1(&ln, end);
                        else if (file_fmt[j].form == DW_FORM_data2) dir_index = jit_dwarf_read_2(&ln, end);
                        else if (file_fmt[j].form == DW_FORM_data4) dir_index = (unsigned)jit_dwarf_read_4(&ln, end);
                        else dir_index = (unsigned)jit_dwarf_read_uleb128(&ln, end);
                        if (i < file_count) files[i].dir_index = dir_index;
                    } else {
                        jit_dwarf_skip_form(&ln, end, file_fmt[j].form, length);
                    }
                }
            }
        } else {
            while (ln < end && *ln) {
                if (dir_count < JIT_DWARF_DIR_TABLE_MAX) dirs[dir_count++] = (char *)ln;
                while (ln < end && *ln) ln++;
                if (ln < end) ln++;
            }
            if (ln < end) ln++;
            while (ln < end && *ln) {
                if (file_count < JIT_DWARF_FILE_TABLE_MAX) {
                    files[file_count].name = (char *)ln;
                    while (ln < end && *ln) ln++;
                    if (ln < end) ln++;
                    files[file_count].dir_index = (unsigned)jit_dwarf_read_uleb128(&ln, end);
                    jit_dwarf_read_uleb128(&ln, end);
                    jit_dwarf_read_uleb128(&ln, end);
                    file_count++;
                } else {
                    while (ln < end && *ln) ln++;
                    if (ln < end) ln++;
                    jit_dwarf_read_uleb128(&ln, end);
                    jit_dwarf_read_uleb128(&ln, end);
                    jit_dwarf_read_uleb128(&ln, end);
                }
            }
            if (ln < end) ln++;
        }

        jit_dwarf_reset_line_state(&state, default_is_stmt);
        while (ln < end) {
            unsigned op = jit_dwarf_read_1(&ln, end);
            if (op >= opcode_base) {
                unsigned adj = op - opcode_base;
                jit_dwarf_advance_addr(&state, adj / line_range, min_insn_length, max_ops_per_insn);
                state.line += (int)(adj % line_range) + line_base;
                jit_dwarf_commit_row(&pending, &state, files, file_count, dirs, dir_count,
                                     (uint32_t)s->text_addr, &stats);
                jit_dwarf_clear_row_flags(&state);
                continue;
            }

            switch (op) {
            case 0: {
                unsigned len = (unsigned)jit_dwarf_read_uleb128(&ln, end);
                unsigned char *cp = ln;
                unsigned subop;
                if (!len || ln + len > end) {
                    ln = end;
                    break;
                }
                subop = jit_dwarf_read_1(&cp, end);
                if (subop == DW_LNE_end_sequence) {
                    state.end_sequence = 1;
                    jit_dwarf_commit_row(&pending, &state, files, file_count, dirs, dir_count,
                                         (uint32_t)s->text_addr, &stats);
                    jit_dwarf_reset_line_state(&state, default_is_stmt);
                } else if (subop == DW_LNE_set_address) {
                    state.address = jit_dwarf_read_addr(&cp, end, address_size);
                    state.op_index = 0;
                } else if (subop == DW_LNE_set_discriminator) {
                    state.discriminator = (unsigned)jit_dwarf_read_uleb128(&cp, end);
                }
                ln += len;
                break;
            }
            case DW_LNS_copy:
                jit_dwarf_commit_row(&pending, &state, files, file_count, dirs, dir_count,
                                     (uint32_t)s->text_addr, &stats);
                jit_dwarf_clear_row_flags(&state);
                break;
            case DW_LNS_advance_pc:
                jit_dwarf_advance_addr(&state, jit_dwarf_read_uleb128(&ln, end),
                                       min_insn_length, max_ops_per_insn);
                break;
            case DW_LNS_advance_line:
                state.line += (int)jit_dwarf_read_sleb128(&ln, end);
                break;
            case DW_LNS_set_file: {
                unsigned idx = (unsigned)jit_dwarf_read_uleb128(&ln, end);
                if ((idx < file_count) || (idx > 0 && idx - 1 < file_count)) {
                    state.file_index = idx;
                    state.file_index_is_default = 0;
                }
                break;
            }
            case DW_LNS_set_column:
                state.column = (unsigned)jit_dwarf_read_uleb128(&ln, end);
                break;
            case DW_LNS_negate_stmt:
                state.is_stmt = state.is_stmt ? 0u : 1u;
                break;
            case DW_LNS_set_basic_block:
                state.basic_block = 1;
                break;
            case DW_LNS_const_add_pc: {
                unsigned off = (255 - opcode_base) / line_range;
                jit_dwarf_advance_addr(&state, off, min_insn_length, max_ops_per_insn);
                break;
            }
            case DW_LNS_fixed_advance_pc:
                state.address += (addr_t)jit_dwarf_read_2(&ln, end);
                state.op_index = 0;
                break;
            case DW_LNS_set_prologue_end:
                state.prologue_end = 1;
                break;
            case DW_LNS_set_epilogue_begin:
                state.epilogue_begin = 1;
                break;
            case DW_LNS_set_isa:
                state.isa = (unsigned)jit_dwarf_read_uleb128(&ln, end);
                break;
            default:
                for (unsigned j = 0; j < opcode_lengths[op - 1]; j++) {
                    jit_dwarf_read_uleb128(&ln, end);
                }
                break;
            }
        }
        if (pending.valid) {
            jit_dwarf_emit_pending_range(&pending, (uint32_t)state.address, &stats);
        }
        total_stats.synthetic_rows += stats.synthetic_rows;
        total_stats.dropped_logs += stats.dropped_logs;
        if (stats.ranges) {
            mapped = 1;
            total_stats.rows += stats.rows;
            total_stats.ranges += stats.ranges;
            total_stats.sequences += stats.sequences;
            total_stats.stmt_rows += stats.stmt_rows;
            total_stats.prologue_rows += stats.prologue_rows;
            total_stats.epilogue_rows += stats.epilogue_rows;
        }
        ln = end;
    }
    if (mapped) {
        lib_printf("[JITDBG] dwarf line rows=%u ranges=%u seq=%u stmt=%u pro=%u epi=%u\n",
                   total_stats.rows, total_stats.ranges, total_stats.sequences,
                   total_stats.stmt_rows, total_stats.prologue_rows, total_stats.epilogue_rows);
        if (total_stats.synthetic_rows) {
            lib_printf("[JITDBG] dwarf line skip synthetic=%u\n",
                       total_stats.synthetic_rows);
        }
        if (total_stats.dropped_logs) {
            lib_printf("[JITDBG] dwarf row logs truncated=%u\n", total_stats.dropped_logs);
        }
    }
    return mapped;
}

static struct jit_job *jit_current_job(void) {
    int tid = task_current();
    for (int i = 0; i < JIT_MAX_JOBS; i++) {
        if (jit_jobs[i].state != JIT_JOB_EMPTY && jit_jobs[i].task_id == tid) return &jit_jobs[i];
    }
    return NULL;
}

static int jit_check_cancel(void) {
    struct jit_job *job = jit_current_job();
    if (!job) return 0;
    return job->cancel_requested != 0;
}

extern void jit_debugger_console_write(int owner_win_id, const char *s);

static int jit_console_printf(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    struct jit_job *job = jit_current_job();
    int owner = job ? job->owner_win_id : -1;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lib_printf("%s", buf);
    jit_debugger_console_write(owner, buf);
    return strlen(buf);
}

static int jit_console_puts(const char *s) {
    struct jit_job *job = jit_current_job();
    int owner = job ? job->owner_win_id : -1;
    if (!s) s = "";
    lib_printf("%s\n", s);
    jit_debugger_console_write(owner, s);
    jit_debugger_console_write(owner, "\n");
    return 0;
}

static int jit_console_putchar(int c) {
    char s[2];
    struct jit_job *job = jit_current_job();
    int owner = job ? job->owner_win_id : -1;
    s[0] = (char)c;
    s[1] = '\0';
    lib_putc((char)c);
    jit_debugger_console_write(owner, s);
    return c;
}

static void jit_dump_code_bytes(const char *tag, uint32_t center) {
    uint32_t start = center >= 16 ? center - 16 : center;
    lib_printf("[JITCODE] %s center=%x\n", tag ? tag : "code", center);
    for (uint32_t off = 0; off < 64; off += 4) {
        uint32_t addr = start + off;
        uint16_t h0 = *(volatile uint16_t *)(uintptr_t)addr;
        uint16_t h1 = *(volatile uint16_t *)(uintptr_t)(addr + 2);
        uint32_t w = *(volatile uint32_t *)(uintptr_t)addr;
        lib_printf("[JITCODE] addr=%x h0=%x h1=%x w=%x mark=%d\n",
                   addr, h0, h1, w, addr == center);
    }
}

static int32_t jit_rv32_i_imm(uint32_t insn) {
    return (int32_t)insn >> 20;
}

static int32_t jit_rv32_s_imm(uint32_t insn) {
    uint32_t imm = ((insn >> 25) << 5) | ((insn >> 7) & 0x1fu);
    return (imm & 0x800u) ? (int32_t)(imm | 0xfffff000u) : (int32_t)imm;
}

static uint32_t jit_rv32_encode_sw(uint32_t rs1, uint32_t rs2, int32_t imm) {
    uint32_t uimm = (uint32_t)imm & 0xfffu;
    return 0x23u | (2u << 12) | (rs1 << 15) | (rs2 << 20) |
           ((uimm & 0x1fu) << 7) | ((uimm >> 5) << 25);
}

static int jit_rv32_bare_mem_fault(uint32_t w) {
    uint32_t op = w & 0x7fu;
    uint32_t rs1 = (w >> 15) & 31u;
    int32_t imm;
    if (op == 0x03u) {
        imm = jit_rv32_i_imm(w);
    } else if (op == 0x23u) {
        imm = jit_rv32_s_imm(w);
    } else {
        return 0;
    }
    return rs1 == 0u && imm >= -16 && imm < 4096;
}

static void jit_repair_main_prologue(uint32_t main_addr) {
    uint32_t w0 = *(volatile uint32_t *)(uintptr_t)(main_addr + 0);
    uint32_t w4 = *(volatile uint32_t *)(uintptr_t)(main_addr + 4);
    uint32_t w8 = *(volatile uint32_t *)(uintptr_t)(main_addr + 8);
    uint32_t w12 = *(volatile uint32_t *)(uintptr_t)(main_addr + 12);
    int32_t frame = -jit_rv32_i_imm(w0);
    int32_t ra_imm = jit_rv32_s_imm(w4);
    int32_t s0_imm = frame - 8;
    uint32_t patched;

    lib_printf("[JITPROLOG] main=%x w0=%x w4=%x w8=%x w12=%x frame=%d ra_imm=%d\n",
               main_addr, w0, w4, w8, w12, frame, ra_imm);

    if ((w0 & 0x707f) != 0x13u || ((w0 >> 7) & 31u) != 2u || ((w0 >> 15) & 31u) != 2u) return;
    if ((w4 & 0x707f) != 0x2023u || ((w4 >> 15) & 31u) != 2u || ((w4 >> 20) & 31u) != 1u) return;
    if ((w12 & 0x707f) != 0x13u || ((w12 >> 7) & 31u) != 8u || ((w12 >> 15) & 31u) != 2u) return;
    if (w8 != 0x23u) return;
    if (frame <= 0 || frame > 2048) return;
    if (ra_imm != frame - 4) return;

    patched = jit_rv32_encode_sw(2, 8, s0_imm);
    *(volatile uint32_t *)(uintptr_t)(main_addr + 8) = patched;
    asm volatile("fence.i" ::: "memory");
    lib_printf("[JITPATCH] fixed main sw-s0 addr=%x old=%x new=%x imm=%d\n",
               main_addr + 8, w8, patched, s0_imm);
}

static void jit_check_broken_text(uint32_t lo, uint32_t hi) {
    if (!lo || hi <= lo) return;
    for (uint32_t pc = lo; pc + 4 <= hi; pc += 4) {
        uint32_t w = *(volatile uint32_t *)(uintptr_t)pc;
        if (w == 0x0000006fu) {
            lib_printf("[JITERR] suspicious self-loop jal addr=%x word=%x prev=%x next=%x\n",
                       pc, w,
                       pc >= lo + 4 ? *(volatile uint32_t *)(uintptr_t)(pc - 4) : 0,
                       pc + 8 <= hi ? *(volatile uint32_t *)(uintptr_t)(pc + 4) : 0);
        } else if (w == 0x00000023u) {
            lib_printf("[JITERR] suspicious bare store addr=%x word=%x prev=%x next=%x\n",
                       pc, w,
                       pc >= lo + 4 ? *(volatile uint32_t *)(uintptr_t)(pc - 4) : 0,
                       pc + 8 <= hi ? *(volatile uint32_t *)(uintptr_t)(pc + 4) : 0);
        } else if (jit_rv32_bare_mem_fault(w)) {
            lib_printf("[JITERR] suspicious bare mem addr=%x word=%x prev=%x next=%x\n",
                       pc, w,
                       pc >= lo + 4 ? *(volatile uint32_t *)(uintptr_t)(pc - 4) : 0,
                       pc + 8 <= hi ? *(volatile uint32_t *)(uintptr_t)(pc + 4) : 0);
        } else if (w == 0x00000063u) {
            lib_printf("[JITERR] suspicious bare branch addr=%x word=%x prev=%x next=%x\n",
                       pc, w,
                       pc >= lo + 4 ? *(volatile uint32_t *)(uintptr_t)(pc - 4) : 0,
                       pc + 8 <= hi ? *(volatile uint32_t *)(uintptr_t)(pc + 4) : 0);
        } else if (w == 0x00000003u || w == 0x00000067u) {
            lib_printf("[JITWARN] suspicious bare insn addr=%x word=%x prev=%x next=%x\n",
                       pc, w,
                       pc >= lo + 4 ? *(volatile uint32_t *)(uintptr_t)(pc - 4) : 0,
                       pc + 8 <= hi ? *(volatile uint32_t *)(uintptr_t)(pc + 4) : 0);
        }
    }
}

static void jit_maybe_yield(void) {
    struct jit_job *job = jit_current_job();
    if (!job) return;
    if (job->cancel_requested) {
        jit_yield();
        return;
    }
    unsigned int now = get_millisecond_timer();
    if ((unsigned int)(now - job->slice_start_ms) < JIT_TIMESLICE_MS) return;
    job->slice_start_ms = now;
    jit_yield();
}

static void jit_yield(void) {
    struct jit_job *job = jit_current_job();
    if (job && job->cancel_requested) {
        job->exit_code = -2;
        job->state = JIT_JOB_FAILED;
        lib_printf("[JITDBG] job=%d cancelled at yield\n", job->id);
        if (job->bg) task_sleep_current();
        return;
    }
    if (job) job->slice_start_ms = get_millisecond_timer();
    if (task_current() >= 0) task_os();
}

static unsigned int jit_get_ticks(void) {
    jit_maybe_yield();
    return get_millisecond_timer();
}

static unsigned int jit_sys_now(void) {
    jit_maybe_yield();
    return sys_now();
}

void os_delay(unsigned int ms) {
    unsigned int start = get_millisecond_timer();
    while (get_millisecond_timer() - start < ms) {
        if (jit_check_cancel()) {
            jit_yield();
            return;
        }
        for (volatile int i = 0; i < 2000; i++) asm volatile("nop");
        jit_yield();
    }
}

static struct jit_job *jit_alloc_job(int bg, int owner_win_id) {
    if (!jit_initialized) os_jit_init();
    for (int i = 0; i < JIT_MAX_JOBS; i++) {
        if (jit_jobs[i].state == JIT_JOB_EMPTY || jit_jobs[i].state == JIT_JOB_DONE || jit_jobs[i].state == JIT_JOB_FAILED) {
            int old_task_id = jit_jobs[i].task_id;
            if (jit_jobs[i].tcc) {
                tcc_delete(jit_jobs[i].tcc);
                jit_jobs[i].tcc = NULL;
            }
            memset(&jit_jobs[i], 0, sizeof(jit_jobs[i]));
            jit_jobs[i].id = jit_next_id++;
            jit_jobs[i].state = JIT_JOB_COMPILING;
            jit_jobs[i].bg = bg;
            jit_jobs[i].owner_win_id = owner_win_id;
            jit_jobs[i].task_id = old_task_id;
            jit_jobs[i].waiter_task_id = -1;
            jit_jobs[i].uheap = jit_user_heaps[i];
            for (int j = 0; j < JIT_MAX_FDS; j++) jit_jobs[i].fds[j] = -1;
            return &jit_jobs[i];
        }
    }
    return NULL;
}

static uint32_t jit_align8(uint32_t n) {
    return (n + 7u) & ~7u;
}

static void jit_cleanup_resources(struct jit_job *job) {
    if (!job) return;
    for (int i = 0; i < JIT_MAX_FDS; i++) {
        if (job->fds[i] >= 0) {
            appfs_close(job->fds[i]);
            job->fds[i] = -1;
        }
    }
    for (int i = 0; i < JIT_MAX_KPTRS; i++) {
        if (job->kptrs[i]) {
            free(job->kptrs[i]);
            job->kptrs[i] = NULL;
        }
    }
}

static int jit_track_kptr(struct jit_job *job, void *p) {
    if (!job || !p) return -1;
    for (int i = 0; i < JIT_MAX_KPTRS; i++) {
        if (!job->kptrs[i]) {
            job->kptrs[i] = p;
            return 0;
        }
    }
    return -1;
}

static int jit_untrack_kptr(struct jit_job *job, void *p) {
    if (!job || !p) return -1;
    for (int i = 0; i < JIT_MAX_KPTRS; i++) {
        if (job->kptrs[i] == p) {
            job->kptrs[i] = NULL;
            return 0;
        }
    }
    return -1;
}

static int jit_track_fd(struct jit_job *job, int fd) {
    if (!job || fd < 0) return -1;
    for (int i = 0; i < JIT_MAX_FDS; i++) {
        if (job->fds[i] < 0) {
            job->fds[i] = fd;
            return 0;
        }
    }
    return -1;
}

static int jit_untrack_fd(struct jit_job *job, int fd) {
    if (!job || fd < 0) return -1;
    for (int i = 0; i < JIT_MAX_FDS; i++) {
        if (job->fds[i] == fd) {
            job->fds[i] = -1;
            return 0;
        }
    }
    return -1;
}

static void *jit_kmalloc(size_t size) {
    struct jit_job *job = jit_current_job();
    if (!job || job->cancel_requested) return NULL;
    jit_maybe_yield();
    void *p = malloc(size);
    if (!p) return NULL;
    if (jit_track_kptr(job, p) != 0) {
        free(p);
        return NULL;
    }
    return p;
}

static void jit_kfree(void *p) {
    struct jit_job *job = jit_current_job();
    if (!p) return;
    jit_maybe_yield();
    if (job && jit_untrack_kptr(job, p) == 0) {
        free(p);
    }
}

static int jit_appfs_open(const char *path, int flags) {
    struct jit_job *job = jit_current_job();
    if (!job || job->cancel_requested) return -1;
    jit_maybe_yield();
    int fd = appfs_open(path, flags);
    if (fd < 0) return fd;
    if (jit_track_fd(job, fd) != 0) {
        appfs_close(fd);
        return -1;
    }
    return fd;
}

static int jit_appfs_close(int fd) {
    struct jit_job *job = jit_current_job();
    jit_maybe_yield();
    if (job) jit_untrack_fd(job, fd);
    return appfs_close(fd);
}

static int jit_appfs_read(int fd, void *buf, size_t size) {
    if (jit_check_cancel()) return -1;
    jit_maybe_yield();
    int rc = appfs_read(fd, buf, size);
    if (rc > 0 && buf) jit_watch_access((uint32_t)(uintptr_t)buf, (uint32_t)rc, 1);
    jit_maybe_yield();
    return rc;
}

static int jit_appfs_write(int fd, const void *buf, size_t size) {
    if (jit_check_cancel()) return -1;
    jit_maybe_yield();
    if (buf && size) jit_watch_access((uint32_t)(uintptr_t)buf, (uint32_t)size, 0);
    int rc = appfs_write(fd, buf, size);
    jit_maybe_yield();
    return rc;
}

static int jit_appfs_seek(int fd, int offset, int whence) {
    if (jit_check_cancel()) return -1;
    jit_maybe_yield();
    return appfs_seek(fd, offset, whence);
}

static int jit_appfs_tell(int fd) {
    if (jit_check_cancel()) return -1;
    return appfs_tell(fd);
}

static void jit_uheap_reset(struct jit_job *job) {
    if (!job || !job->uheap) return;
    job->uheap_head = (struct jit_ublock *)job->uheap;
    job->uheap_head->magic = JIT_UHEAP_MAGIC;
    job->uheap_head->size = JIT_UHEAP_SIZE - sizeof(struct jit_ublock);
    job->uheap_head->free = 1;
    job->uheap_head->next = NULL;
}

static void jit_uheap_coalesce(struct jit_job *job) {
    struct jit_ublock *b = job ? job->uheap_head : NULL;
    while (b && b->next) {
        if (b->free && b->next->free) {
            b->size += sizeof(struct jit_ublock) + b->next->size;
            b->next = b->next->next;
        } else {
            b = b->next;
        }
    }
}

static void *umalloc(size_t size) {
    struct jit_job *job = jit_current_job();
    if (!job) return NULL;
    if (job->cancel_requested) return NULL;
    jit_maybe_yield();
    if (!job->uheap_head) jit_uheap_reset(job);
    uint32_t need = jit_align8(size ? (uint32_t)size : 1u);
    struct jit_ublock *b = job->uheap_head;
    while (b) {
        if (b->magic != JIT_UHEAP_MAGIC) return NULL;
        if (b->free && b->size >= need) {
            uint32_t remain = b->size - need;
            if (remain > sizeof(struct jit_ublock) + 8u) {
                struct jit_ublock *n = (struct jit_ublock *)((uint8_t *)b + sizeof(struct jit_ublock) + need);
                n->magic = JIT_UHEAP_MAGIC;
                n->size = remain - sizeof(struct jit_ublock);
                n->free = 1;
                n->next = b->next;
                b->next = n;
                b->size = need;
            }
            b->free = 0;
            return (uint8_t *)b + sizeof(struct jit_ublock);
        }
        b = b->next;
    }
    return NULL;
}

static void ufree(void *p) {
    if (!p) return;
    struct jit_job *job = jit_current_job();
    if (!job) return;
    if (job->cancel_requested) return;
    jit_maybe_yield();
    uint8_t *addr = (uint8_t *)p;
    if (addr < job->uheap + sizeof(struct jit_ublock) ||
        addr >= job->uheap + JIT_UHEAP_SIZE) {
        lib_printf("[JITDBG] ufree reject ptr=%lx\n", (unsigned long)(uintptr_t)p);
        return;
    }
    struct jit_ublock *b = (struct jit_ublock *)(addr - sizeof(struct jit_ublock));
    if (b->magic != JIT_UHEAP_MAGIC || b->free) {
        lib_printf("[JITDBG] ufree invalid ptr=%lx\n", (unsigned long)(uintptr_t)p);
        return;
    }
    b->free = 1;
    jit_uheap_coalesce(job);
}

static void *ucalloc(uint32_t nmemb, uint32_t size) {
    if (jit_check_cancel()) return NULL;
    jit_maybe_yield();
    uint32_t total = nmemb * size;
    if (size && total / size != nmemb) return NULL;
    void *p = umalloc(total ? total : 1u);
    if (p) memset(p, 0, total);
    return p;
}

static void *urealloc(void *ptr, size_t size) {
    if (jit_check_cancel()) return NULL;
    jit_maybe_yield();
    if (!ptr) return umalloc(size);
    if (size == 0) {
        ufree(ptr);
        return NULL;
    }
    struct jit_job *job = jit_current_job();
    if (!job) return NULL;
    uint8_t *addr = (uint8_t *)ptr;
    if (addr < job->uheap + sizeof(struct jit_ublock) ||
        addr >= job->uheap + JIT_UHEAP_SIZE) {
        return NULL;
    }
    struct jit_ublock *b = (struct jit_ublock *)(addr - sizeof(struct jit_ublock));
    if (b->magic != JIT_UHEAP_MAGIC || b->free) return NULL;
    if (b->size >= size) return ptr;
    void *np = umalloc(size);
    if (np) {
        memcpy(np, ptr, b->size);
        ufree(ptr);
    }
    return np;
}

void jit_uheap_info(uint32_t *base, uint32_t *size, uint32_t *used, uint32_t *free_bytes, uint32_t *blocks) {
    uint32_t u = 0;
    uint32_t f = 0;
    uint32_t n = 0;
    if (base) *base = (uint32_t)(uintptr_t)jit_user_heaps[0];
    if (size) *size = JIT_UHEAP_SIZE * JIT_MAX_JOBS;
    for (int i = 0; i < JIT_MAX_JOBS; i++) {
        if (jit_jobs[i].uheap_head) {
            struct jit_ublock *b = jit_jobs[i].uheap_head;
            while (b) {
                if (b->magic != JIT_UHEAP_MAGIC) break;
                if (b->free) f += b->size;
                else u += b->size;
                n++;
                b = b->next;
            }
        }
    }
    if (used) *used = u;
    if (free_bytes) *free_bytes = f;
    if (blocks) *blocks = n;
}

static void *jit_shared_mem(void) {
    return jit_shared_area;
}

static uint32_t jit_shared_size(void) {
    return JIT_SHARED_SIZE;
}

void os_jit_shared_reset(void) {
    memset(jit_shared_area, 0, sizeof(jit_shared_area));
}

static const char jit_prelude[] =
    "typedef unsigned int size_t;\n"
    "typedef unsigned int uint32_t;\n"
    "typedef unsigned char uint8_t;\n"
    "typedef struct __FILE FILE;\n"
    "enum { SEEK_SET = 0, SEEK_CUR = 1, SEEK_END = 2 };\n"
    "int printf(const char *fmt, ...);\n"
    "int print(const char *fmt, ...);\n"
    "int puts(const char *s);\n"
    "int putchar(int c);\n"
    "int snprintf(char *out, size_t n, const char *fmt, ...);\n"
    "void debug_line(int line);\n"
    "void debug_loc(const char *file, int line);\n"
    "int debug_probe(const char *file, int line);\n"
    "void debug_break(void);\n"
    "FILE *fopen(const char *pathname, const char *mode);\n"
    "int fclose(FILE *stream);\n"
    "size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);\n"
    "size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);\n"
    "int fseek(FILE *stream, long offset, int whence);\n"
    "long ftell(FILE *stream);\n"
    "int unlink(const char *pathname);\n"
    "int remove(const char *pathname);\n"
    "void *umalloc(size_t size);\n"
    "void ufree(void *p);\n"
    "void *ucalloc(uint32_t nmemb, uint32_t size);\n"
    "void *urealloc(void *ptr, size_t size);\n"
    "void *kmalloc(size_t size);\n"
    "void kfree(void *p);\n"
    "void yield(void);\n"
    "void jit_poll(void);\n"
    "int cancelled(void);\n"
    "void *shared_mem(void);\n"
    "uint32_t shared_size(void);\n"
    "#define malloc umalloc\n"
    "#define free ufree\n"
    "#define calloc ucalloc\n"
    "#define realloc urealloc\n"
    "void *memset(void *dst, int c, size_t n);\n"
    "void *memcpy(void *dst, const void *src, size_t n);\n"
    "void *memmove(void *dst, const void *src, size_t n);\n"
    "size_t strlen(const char *s);\n"
    "int strcmp(const char *a, const char *b);\n"
    "int strncmp(const char *a, const char *b, uint32_t n);\n"
    "char *strcpy(char *dst, const char *src);\n"
    "char *strcat(char *dst, const char *src);\n"
    "char *strncpy(char *dst, const char *src, uint32_t n);\n"
    "char *strchr(const char *s, int c);\n"
    "int atoi(const char *s);\n"
    "int abs(int n);\n"
    "unsigned int get_ticks(void);\n"
    "unsigned int millis(void);\n"
    "unsigned int sys_now(void);\n"
    "unsigned int get_wall_clock_seconds(void);\n"
    "void delay(unsigned int ms);\n"
    "void putpixel(int x, int y, int color);\n"
    "void vga_update(void);\n"
    "void draw_char(int x, int y, char c, int color);\n"
    "void draw_text(int x, int y, const char *s, int color);\n"
    "void draw_text_scaled(int x, int y, const char *s, int color, int scale);\n"
    "void draw_rect_fill(int x, int y, int w, int h, int color);\n"
    "void draw_hline(int x, int y, int w, int color);\n"
    "void draw_vline(int x, int y, int h, int color);\n"
    "void draw_vertical_gradient(int x, int y, int w, int h, int top_color, int bottom_color);\n"
    "void draw_bevel_rect(int x, int y, int w, int h, int fill, int light, int dark);\n"
    "void draw_round_rect_fill(int x, int y, int w, int h, int r, int color);\n"
    "void draw_round_rect_wire(int x, int y, int w, int h, int r, int color);\n"
    "const char *clipboard_get(void);\n"
    "void clipboard_set(const char *text);\n"
    "int appfs_open(const char *path, int flags);\n"
    "int appfs_read(int fd, void *buf, size_t size);\n"
    "int appfs_write(int fd, const void *buf, size_t size);\n"
    "int appfs_seek(int fd, int offset, int whence);\n"
    "int appfs_tell(int fd);\n"
    "int appfs_unlink(const char *path);\n"
    "int appfs_close(int fd);\n"
    "enum { APP_O_READ = 0x01, APP_O_WRITE = 0x02, APP_O_CREAT = 0x100, APP_O_TRUNC = 0x200 };\n"
    "enum { WIDTH = 1024, HEIGHT = 768 };\n";

enum jit_instrument_state {
    JIT_INST_NONE = 0,
    JIT_INST_WAIT_PAREN,
    JIT_INST_IN_HEADER,
    JIT_INST_WAIT_BLOCK,
    JIT_INST_WAIT_DO_BLOCK
};

static int jit_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

static int jit_is_ident_char(char c) {
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

static char *jit_instrument_loops(const char *source) {
    static const char inject[] = "{ jit_poll(); ";
    size_t src_len = source ? strlen(source) : 0;
    char *out = (char *)malloc(src_len * 2 + 64);
    if (!out) return NULL;
    size_t oi = 0;
    int state = JIT_INST_NONE;
    int paren_depth = 0;
    int in_line_comment = 0;
    int in_block_comment = 0;
    int in_string = 0;
    int in_char = 0;

    for (size_t i = 0; i < src_len; i++) {
        char c = source[i];
        char n = (i + 1 < src_len) ? source[i + 1] : '\0';

        if (in_line_comment) {
            out[oi++] = c;
            if (c == '\n') in_line_comment = 0;
            continue;
        }
        if (in_block_comment) {
            out[oi++] = c;
            if (c == '*' && n == '/') {
                out[oi++] = n;
                i++;
                in_block_comment = 0;
            }
            continue;
        }
        if (in_string) {
            out[oi++] = c;
            if (c == '\\' && n) {
                out[oi++] = n;
                i++;
                continue;
            }
            if (c == '"') in_string = 0;
            continue;
        }
        if (in_char) {
            out[oi++] = c;
            if (c == '\\' && n) {
                out[oi++] = n;
                i++;
                continue;
            }
            if (c == '\'') in_char = 0;
            continue;
        }

        if (c == '/' && n == '/') {
            out[oi++] = c;
            out[oi++] = n;
            i++;
            in_line_comment = 1;
            continue;
        }
        if (c == '/' && n == '*') {
            out[oi++] = c;
            out[oi++] = n;
            i++;
            in_block_comment = 1;
            continue;
        }
        if (c == '"') {
            out[oi++] = c;
            in_string = 1;
            continue;
        }
        if (c == '\'') {
            out[oi++] = c;
            in_char = 1;
            continue;
        }

        if ((c == 'f' && i + 2 < src_len && !strncmp(source + i, "for", 3) &&
             (i == 0 || !jit_is_ident_char(source[i - 1])) &&
             !jit_is_ident_char(source[i + 3])) ||
            (c == 'w' && i + 4 < src_len && !strncmp(source + i, "while", 5) &&
             (i == 0 || !jit_is_ident_char(source[i - 1])) &&
             !jit_is_ident_char(source[i + 5]))) {
            size_t kw_len = (c == 'f') ? 3 : 5;
            for (size_t k = 0; k < kw_len; k++) out[oi++] = source[i + k];
            i += kw_len - 1;
            state = JIT_INST_WAIT_PAREN;
            paren_depth = 0;
            continue;
        }
        if (c == 'd' && i + 1 < src_len && !strncmp(source + i, "do", 2) &&
            (i == 0 || !jit_is_ident_char(source[i - 1])) &&
            !jit_is_ident_char(source[i + 2])) {
            out[oi++] = 'd';
            out[oi++] = 'o';
            i += 1;
            state = JIT_INST_WAIT_DO_BLOCK;
            continue;
        }

        out[oi++] = c;

        if (state == JIT_INST_WAIT_PAREN) {
            if (c == '(') {
                state = JIT_INST_IN_HEADER;
                paren_depth = 1;
            } else if (!jit_is_space(c)) {
                state = JIT_INST_NONE;
            }
            continue;
        }
        if (state == JIT_INST_IN_HEADER) {
            if (c == '(') paren_depth++;
            else if (c == ')') {
                paren_depth--;
                if (paren_depth == 0) state = JIT_INST_WAIT_BLOCK;
            }
            continue;
        }
        if (state == JIT_INST_WAIT_BLOCK) {
            if (jit_is_space(c)) continue;
            if (c == '{') {
                memcpy(out + oi, inject + 1, sizeof(inject) - 2);
                oi += sizeof(inject) - 2;
            }
            state = JIT_INST_NONE;
            continue;
        }
        if (state == JIT_INST_WAIT_DO_BLOCK) {
            if (jit_is_space(c)) continue;
            if (c == '{') {
                memcpy(out + oi, inject + 1, sizeof(inject) - 2);
                oi += sizeof(inject) - 2;
            }
            state = JIT_INST_NONE;
            continue;
        }
    }
    out[oi] = '\0';
    return out;
}

static void jit_add_symbol_logged(TCCState *s, const char *name, const void *val) {
    tcc_add_symbol(s, name, val);
}

static void jit_add_os_symbols(TCCState *s) {
    jit_add_symbol_logged(s, "printf", (void*)jit_console_printf);
    jit_add_symbol_logged(s, "print", (void*)jit_console_printf);
    jit_add_symbol_logged(s, "puts", (void*)jit_console_puts);
    jit_add_symbol_logged(s, "putchar", (void*)jit_console_putchar);
    jit_add_symbol_logged(s, "snprintf", (void*)snprintf);
    jit_add_symbol_logged(s, "debug_line", (void*)debug_line);
    jit_add_symbol_logged(s, "debug_loc", (void*)debug_loc);
    jit_add_symbol_logged(s, "debug_probe", (void*)debug_probe);
    jit_add_symbol_logged(s, "debug_break", (void*)debug_break);

    jit_add_symbol_logged(s, "umalloc", (void*)umalloc);
    jit_add_symbol_logged(s, "ufree", (void*)ufree);
    jit_add_symbol_logged(s, "ucalloc", (void*)ucalloc);
    jit_add_symbol_logged(s, "urealloc", (void*)urealloc);
    jit_add_symbol_logged(s, "kmalloc", (void*)jit_kmalloc);
    jit_add_symbol_logged(s, "kfree", (void*)jit_kfree);
    jit_add_symbol_logged(s, "yield", (void*)jit_yield);
    jit_add_symbol_logged(s, "jit_poll", (void*)jit_maybe_yield);
    jit_add_symbol_logged(s, "cancelled", (void*)jit_check_cancel);
    jit_add_symbol_logged(s, "shared_mem", (void*)jit_shared_mem);
    jit_add_symbol_logged(s, "shared_size", (void*)jit_shared_size);
    jit_add_symbol_logged(s, "memset", (void*)jit_watch_memset);
    jit_add_symbol_logged(s, "memcpy", (void*)jit_watch_memcpy);
    jit_add_symbol_logged(s, "memmove", (void*)jit_watch_memmove);
    jit_add_symbol_logged(s, "strlen", (void*)strlen);
    jit_add_symbol_logged(s, "strcmp", (void*)strcmp);
    jit_add_symbol_logged(s, "strncmp", (void*)strncmp);
    jit_add_symbol_logged(s, "strcpy", (void*)strcpy);
    jit_add_symbol_logged(s, "strcat", (void*)strcat);
    jit_add_symbol_logged(s, "strncpy", (void*)strncpy);
    jit_add_symbol_logged(s, "strchr", (void*)strchr);
    jit_add_symbol_logged(s, "atoi", (void*)atoi);
    jit_add_symbol_logged(s, "abs", (void*)abs);

    jit_add_symbol_logged(s, "get_ticks", (void*)jit_get_ticks);
    jit_add_symbol_logged(s, "millis", (void*)jit_get_ticks);
    jit_add_symbol_logged(s, "sys_now", (void*)jit_sys_now);
    jit_add_symbol_logged(s, "get_wall_clock_seconds", (void*)get_wall_clock_seconds);
    jit_add_symbol_logged(s, "delay", (void*)os_delay);

    jit_add_symbol_logged(s, "vga_update", (void*)vga_update);
    jit_add_symbol_logged(s, "putpixel", (void*)putpixel);
    jit_add_symbol_logged(s, "draw_char", (void*)draw_char);
    jit_add_symbol_logged(s, "draw_text", (void*)draw_text);
    jit_add_symbol_logged(s, "draw_text_scaled", (void*)draw_text_scaled);
    jit_add_symbol_logged(s, "draw_rect_fill", (void*)draw_rect_fill);
    jit_add_symbol_logged(s, "draw_hline", (void*)draw_hline);
    jit_add_symbol_logged(s, "draw_vline", (void*)draw_vline);
    jit_add_symbol_logged(s, "draw_vertical_gradient", (void*)draw_vertical_gradient);
    jit_add_symbol_logged(s, "draw_bevel_rect", (void*)draw_bevel_rect);
    jit_add_symbol_logged(s, "draw_round_rect_fill", (void*)draw_round_rect_fill);
    jit_add_symbol_logged(s, "draw_round_rect_wire", (void*)draw_round_rect_wire);

    extern const char *clipboard_get(void);
    extern void clipboard_set(const char *text);
    jit_add_symbol_logged(s, "clipboard_get", (void*)clipboard_get);
    jit_add_symbol_logged(s, "clipboard_set", (void*)clipboard_set);

    jit_add_symbol_logged(s, "fopen", (void*)fopen);
    jit_add_symbol_logged(s, "fclose", (void*)fclose);
    jit_add_symbol_logged(s, "fread", (void*)fread);
    jit_add_symbol_logged(s, "fwrite", (void*)fwrite);
    jit_add_symbol_logged(s, "fseek", (void*)fseek);
    jit_add_symbol_logged(s, "ftell", (void*)ftell);
    jit_add_symbol_logged(s, "unlink", (void*)unlink);
    jit_add_symbol_logged(s, "remove", (void*)remove);
    jit_add_symbol_logged(s, "open", (void*)open);
    jit_add_symbol_logged(s, "close", (void*)close);
    jit_add_symbol_logged(s, "read", (void*)read);
    jit_add_symbol_logged(s, "write", (void*)write);
    jit_add_symbol_logged(s, "lseek", (void*)lseek);

    jit_add_symbol_logged(s, "appfs_open", (void*)jit_appfs_open);
    jit_add_symbol_logged(s, "appfs_read", (void*)jit_appfs_read);
    jit_add_symbol_logged(s, "appfs_write", (void*)jit_appfs_write);
    jit_add_symbol_logged(s, "appfs_seek", (void*)jit_appfs_seek);
    jit_add_symbol_logged(s, "appfs_tell", (void*)jit_appfs_tell);
    jit_add_symbol_logged(s, "appfs_unlink", (void*)appfs_unlink);
    jit_add_symbol_logged(s, "appfs_close", (void*)jit_appfs_close);
}

static char *jit_make_source_with_prelude(const char *source) {
    size_t prelude_len = strlen(jit_prelude);
    char *instrumented = jit_instrument_loops(source ? source : "");
    if (!instrumented) return NULL;
    size_t source_len = strlen(instrumented);
    char *combined = (char *)malloc(prelude_len + source_len + 2);
    if (!combined) {
        free(instrumented);
        return NULL;
    }
    memcpy(combined, jit_prelude, prelude_len);
    combined[prelude_len] = '\n';
    if (source_len) memcpy(combined + prelude_len + 1, instrumented, source_len);
    combined[prelude_len + 1 + source_len] = '\0';
    free(instrumented);
    return combined;
}

static const char *jit_state_name(int state) {
    switch (state) {
    case JIT_JOB_EMPTY: return "empty";
    case JIT_JOB_COMPILING: return "compiling";
    case JIT_JOB_READY: return "ready";
    case JIT_JOB_RUNNING: return "running";
    case JIT_JOB_DONE: return "done";
    case JIT_JOB_FAILED: return "failed";
    default: return "?";
    }
}

static int jit_source_buf_reserve(struct jit_source_buf *buf, size_t need) {
    if (!buf) return -1;
    if (need <= buf->cap) return 0;
    size_t new_cap = buf->cap ? buf->cap : 1024u;
    while (new_cap < need) new_cap *= 2u;
    if (new_cap > JIT_SOURCE_MAX_SIZE) new_cap = JIT_SOURCE_MAX_SIZE;
    if (new_cap < need) return -1;
    char *np = (char *)realloc(buf->data, new_cap);
    if (!np) return -1;
    buf->data = np;
    buf->cap = new_cap;
    return 0;
}

static int jit_source_buf_append_n(struct jit_source_buf *buf, const char *s, size_t n) {
    if (!buf || !s) return -1;
    if (buf->len + n + 1 > JIT_SOURCE_MAX_SIZE) return -1;
    if (jit_source_buf_reserve(buf, buf->len + n + 1) != 0) return -1;
    memcpy(buf->data + buf->len, s, n);
    buf->len += n;
    buf->data[buf->len] = '\0';
    return 0;
}

static int jit_source_buf_append(struct jit_source_buf *buf, const char *s) {
    return jit_source_buf_append_n(buf, s, s ? strlen(s) : 0);
}

static int jit_path_dirname(const char *path, char *out, size_t out_size) {
    size_t len = 0;
    size_t slash = 0;
    if (!path || !out || out_size == 0) return -1;
    while (path[len]) {
        if (path[len] == '/') slash = len;
        len++;
    }
    if (slash == 0) {
        if (path[0] == '/') {
            if (out_size < 2) return -1;
            out[0] = '/';
            out[1] = '\0';
            return 0;
        }
        if (out_size < 6) return -1;
        strcpy(out, "/root");
        return 0;
    }
    if (slash + 1 >= out_size) return -1;
    memcpy(out, path, slash);
    out[slash] = '\0';
    return 0;
}

static int jit_path_join(char *out, size_t out_size, const char *base_dir, const char *name) {
    size_t pos = 0;
    size_t i = 0;
    if (!out || out_size == 0 || !name || !name[0]) return -1;
    if (name[0] == '/') {
        while (name[i] && pos + 1 < out_size) out[pos++] = name[i++];
        out[pos] = '\0';
        return name[i] ? -1 : 0;
    }
    if (!base_dir || !base_dir[0]) base_dir = "/root";
    while (base_dir[i] && pos + 1 < out_size) out[pos++] = base_dir[i++];
    if (pos == 0 || out[pos - 1] != '/') {
        if (pos + 1 >= out_size) return -1;
        out[pos++] = '/';
    }
    i = 0;
    while (name[i] && pos + 1 < out_size) out[pos++] = name[i++];
    out[pos] = '\0';
    return name[i] ? -1 : 0;
}

static int jit_read_text_file(const char *path, char **out, size_t *out_size, char *msg, size_t msg_size) {
    int fd = -1;
    struct jit_source_buf buf;
    char tmp[512];
    int rc = 0;
    if (msg && msg_size) msg[0] = '\0';
    if (!path || !out) return -1;
    *out = NULL;
    if (out_size) *out_size = 0;
    memset(&buf, 0, sizeof(buf));
    fd = appfs_open(path, 1);
    if (fd < 0) {
        if (msg && msg_size) snprintf(msg, msg_size, "JIT: open failed: %s", path);
        return -1;
    }
    while (1) {
        int n = appfs_read(fd, tmp, sizeof(tmp));
        if (n < 0) {
            rc = -1;
            if (msg && msg_size) snprintf(msg, msg_size, "JIT: read failed: %s", path);
            break;
        }
        if (n == 0) break;
        if (jit_source_buf_append_n(&buf, tmp, (size_t)n) != 0) {
            rc = -1;
            if (msg && msg_size) snprintf(msg, msg_size, "JIT: source too large: %s", path);
            break;
        }
    }
    appfs_close(fd);
    if (rc != 0) {
        free(buf.data);
        return -1;
    }
    if (!buf.data) {
        buf.data = (char *)malloc(1);
        if (!buf.data) {
            if (msg && msg_size) snprintf(msg, msg_size, "JIT: alloc failed");
            return -1;
        }
        buf.data[0] = '\0';
    }
    *out = buf.data;
    if (out_size) *out_size = buf.len;
    return 0;
}

static int jit_line_is_local_include(const char *line, size_t len, char *name, size_t name_size) {
    size_t i = 0;
    size_t j = 0;
    if (!line || !name || name_size == 0) return 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= len || line[i] != '#') return 0;
    i++;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i + 7 > len || strncmp(line + i, "include", 7) != 0) return 0;
    i += 7;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= len || line[i] != '"') return 0;
    i++;
    while (i < len && line[i] != '"' && j + 1 < name_size) name[j++] = line[i++];
    if (i >= len || line[i] != '"') return 0;
    name[j] = '\0';
    return name[0] != '\0';
}

static int jit_stack_contains(const char **stack, int depth, const char *path) {
    for (int i = 0; i < depth; i++) {
        if (stack[i] && strcmp(stack[i], path) == 0) return 1;
    }
    return 0;
}

static int jit_seen_contains(char seen[JIT_SEEN_MAX][256], int seen_count, const char *path) {
    for (int i = 0; i < seen_count; i++) {
        if (seen[i][0] && strcmp(seen[i], path) == 0) return 1;
    }
    return 0;
}

static int jit_seen_add(char seen[JIT_SEEN_MAX][256], int *seen_count, const char *path) {
    if (!seen || !seen_count || !path || !path[0]) return -1;
    if (jit_seen_contains(seen, *seen_count, path)) return 0;
    if (*seen_count >= JIT_SEEN_MAX) return -1;
    strncpy(seen[*seen_count], path, sizeof(seen[*seen_count]) - 1);
    seen[*seen_count][sizeof(seen[*seen_count]) - 1] = '\0';
    (*seen_count)++;
    return 0;
}

static int jit_header_to_source_path(char *out, size_t out_size, const char *header_path) {
    size_t len = 0;
    if (!out || out_size == 0 || !header_path) return -1;
    len = strlen(header_path);
    if (len < 3 || out_size <= len) return -1;
    if (strcmp(header_path + len - 2, ".h") != 0) return -1;
    memcpy(out, header_path, len - 2);
    out[len - 2] = '.';
    out[len - 1] = 'c';
    out[len] = '\0';
    return 0;
}

static int jit_expand_source_recursive(struct jit_source_buf *out,
                                       const char *source,
                                       const char *base_dir,
                                       const char **stack,
                                       int depth,
                                       char seen[JIT_SEEN_MAX][256],
                                       int *seen_count,
                                       char *msg,
                                       size_t msg_size) {
    size_t pos = 0;
    int in_block_comment = 0;
    if (!out || !source || !base_dir) return -1;
    if (depth >= JIT_INCLUDE_MAX_DEPTH) {
        if (msg && msg_size) snprintf(msg, msg_size, "JIT: include depth exceeded");
        return -1;
    }
    while (source[pos]) {
        size_t line_start = pos;
        size_t line_len = 0;
        char inc_name[128];
        while (source[pos] && source[pos] != '\n') pos++;
        line_len = pos - line_start;
        if (!in_block_comment && jit_line_is_local_include(source + line_start, line_len, inc_name, sizeof(inc_name))) {
            char inc_path[256];
            char inc_dir[256];
            char auto_src_path[256];
            char *inc_src = NULL;
            char *auto_src = NULL;
            if (jit_path_join(inc_path, sizeof(inc_path), base_dir, inc_name) != 0) {
                if (msg && msg_size) snprintf(msg, msg_size, "JIT: bad include path: %s", inc_name);
                return -1;
            }
            if (jit_stack_contains(stack, depth, inc_path)) {
                if (msg && msg_size) snprintf(msg, msg_size, "JIT: recursive include: %s", inc_path);
                return -1;
            }
            if (!jit_seen_contains(seen, seen_count ? *seen_count : 0, inc_path)) {
                if (jit_read_text_file(inc_path, &inc_src, NULL, msg, msg_size) != 0) return -1;
                if (jit_path_dirname(inc_path, inc_dir, sizeof(inc_dir)) != 0) {
                    free(inc_src);
                    if (msg && msg_size) snprintf(msg, msg_size, "JIT: bad include dir: %s", inc_path);
                    return -1;
                }
                if (jit_seen_add(seen, seen_count, inc_path) != 0) {
                    free(inc_src);
                    if (msg && msg_size) snprintf(msg, msg_size, "JIT: include cache full");
                    return -1;
                }
                stack[depth] = inc_path;
                if (jit_expand_source_recursive(out, inc_src, inc_dir, stack, depth + 1, seen, seen_count, msg, msg_size) != 0) {
                    free(inc_src);
                    return -1;
                }
                stack[depth] = NULL;
                free(inc_src);
            }
            if (jit_header_to_source_path(auto_src_path, sizeof(auto_src_path), inc_path) == 0 &&
                !jit_seen_contains(seen, seen_count ? *seen_count : 0, auto_src_path)) {
                if (jit_read_text_file(auto_src_path, &auto_src, NULL, NULL, 0) == 0 && auto_src) {
                    if (jit_path_dirname(auto_src_path, inc_dir, sizeof(inc_dir)) != 0) {
                        free(auto_src);
                        if (msg && msg_size) snprintf(msg, msg_size, "JIT: bad source dir: %s", auto_src_path);
                        return -1;
                    }
                    if (jit_seen_add(seen, seen_count, auto_src_path) != 0) {
                        free(auto_src);
                        if (msg && msg_size) snprintf(msg, msg_size, "JIT: include cache full");
                        return -1;
                    }
                    stack[depth] = auto_src_path;
                    if (jit_expand_source_recursive(out, auto_src, inc_dir, stack, depth + 1, seen, seen_count, msg, msg_size) != 0) {
                        free(auto_src);
                        return -1;
                    }
                    stack[depth] = NULL;
                    free(auto_src);
                }
            }
        } else {
            if (jit_source_buf_append_n(out, source + line_start, line_len) != 0) {
                if (msg && msg_size) snprintf(msg, msg_size, "JIT: expanded source too large");
                return -1;
            }
        }
        if (source[pos] == '\n') {
            if (jit_source_buf_append_n(out, "\n", 1) != 0) {
                if (msg && msg_size) snprintf(msg, msg_size, "JIT: expanded source too large");
                return -1;
            }
            pos++;
        }
        for (size_t i = line_start; i < line_start + line_len; i++) {
            if (!in_block_comment && source[i] == '/' && source[i + 1] == '*') {
                in_block_comment = 1;
                i++;
            } else if (in_block_comment && source[i] == '*' && source[i + 1] == '/') {
                in_block_comment = 0;
                i++;
            }
        }
    }
    return 0;
}

static int jit_debug_line_should_break(const char *line, size_t len, int brace_depth) {
    size_t i = 0;
    if (brace_depth <= 0) return 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= len) return 0;
    if (line[i] == '#') return 0;
    if (line[i] == '/' && i + 1 < len && (line[i + 1] == '/' || line[i + 1] == '*')) return 0;
    if (line[i] == '{' || line[i] == '}') return 0;
    if (strncmp(line + i, "else", 4) == 0) return 0;
    if (strncmp(line + i, "case", 4) == 0) return 0;
    if (strncmp(line + i, "default", 7) == 0) return 0;
    return 1;
}

static int jit_debug_append_quoted_path(struct jit_source_buf *out, const char *path) {
    if (jit_source_buf_append_n(out, "\"", 1) != 0) return -1;
    for (size_t i = 0; path && path[i]; i++) {
        if (path[i] == '\\' || path[i] == '"') {
            char esc[2] = { '\\', path[i] };
            if (jit_source_buf_append_n(out, esc, 2) != 0) return -1;
        } else {
            if (jit_source_buf_append_n(out, path + i, 1) != 0) return -1;
        }
    }
    if (jit_source_buf_append_n(out, "\"", 1) != 0) return -1;
    return 0;
}

static int jit_debug_append_line_directive(struct jit_source_buf *out, const char *path, int line_no) {
    char row[64];
    if (!out || !path || !path[0] || line_no <= 0) return -1;
    if (jit_source_buf_append_n(out, "#line ", 6) != 0) return -1;
    snprintf(row, sizeof(row), "%d ", line_no);
    if (jit_source_buf_append_n(out, row, strlen(row)) != 0) return -1;
    if (jit_debug_append_quoted_path(out, path) != 0) return -1;
    if (jit_source_buf_append_n(out, "\n", 1) != 0) return -1;
    return 0;
}

static int jit_debug_append_probe(struct jit_source_buf *out, const char *path, int line_no) {
    char row[64];
    jit_debug_probe_sites_add(path, line_no);
    if (jit_source_buf_append_n(out, "debug_probe(", 12) != 0) return -1;
    if (jit_debug_append_quoted_path(out, path ? path : "") != 0) return -1;
    snprintf(row, sizeof(row), ",%d);\n", line_no);
    if (jit_source_buf_append_n(out, row, strlen(row)) != 0) return -1;
    return 0;
}

static int jit_debug_instrument_source(struct jit_source_buf *out, const char *source, const char *path, char *msg, size_t msg_size) {
    size_t pos = 0;
    int brace_depth = 0;
    int line_no = 1;
    int in_block_comment = 0;
    if (!out || !source) return -1;
    while (source[pos]) {
        size_t line_start = pos;
        size_t line_len = 0;
        int depth_before;
        while (source[pos] && source[pos] != '\n') pos++;
        line_len = pos - line_start;
        depth_before = brace_depth;
        if (jit_debug_append_line_directive(out, path ? path : "", line_no) != 0) {
            if (msg && msg_size) snprintf(msg, msg_size, "JIT: debug source too large");
            return -1;
        }
        if (!in_block_comment && jit_debug_line_should_break(source + line_start, line_len, depth_before)) {
            if (jit_debug_append_probe(out, path, line_no) != 0) {
                if (msg && msg_size) snprintf(msg, msg_size, "JIT: debug source too large");
                return -1;
            }
        }
        if (jit_source_buf_append_n(out, source + line_start, line_len) != 0) {
            if (msg && msg_size) snprintf(msg, msg_size, "JIT: debug source too large");
            return -1;
        }
        if (source[pos] == '\n') {
            if (jit_source_buf_append_n(out, "\n", 1) != 0) {
                if (msg && msg_size) snprintf(msg, msg_size, "JIT: debug source too large");
                return -1;
            }
            pos++;
        }
        for (size_t i = line_start; i < line_start + line_len; i++) {
            if (!in_block_comment && source[i] == '/' && source[i + 1] == '*') {
                in_block_comment = 1;
                i++;
            } else if (in_block_comment && source[i] == '*' && source[i + 1] == '/') {
                in_block_comment = 0;
                i++;
            } else if (!in_block_comment && source[i] == '/' && source[i + 1] == '/') {
                break;
            } else if (!in_block_comment && source[i] == '{') {
                brace_depth++;
            } else if (!in_block_comment && source[i] == '}' && brace_depth > 0) {
                brace_depth--;
            }
        }
        line_no++;
    }
    return 0;
}

static int jit_expand_debug_source_recursive(struct jit_source_buf *out,
                                             const char *source,
                                             const char *source_path,
                                             const char *base_dir,
                                             const char **stack,
                                             int depth,
                                             char seen[JIT_SEEN_MAX][256],
                                             int *seen_count,
                                             char *msg,
                                             size_t msg_size) {
    size_t pos = 0;
    int in_block_comment = 0;
    struct jit_source_buf current;
    memset(&current, 0, sizeof(current));
    if (!out || !source || !base_dir) return -1;
    if (depth >= JIT_INCLUDE_MAX_DEPTH) {
        if (msg && msg_size) snprintf(msg, msg_size, "JIT: include depth exceeded");
        return -1;
    }
    if (jit_debug_instrument_source(&current, source, source_path, msg, msg_size) != 0) {
        free(current.data);
        return -1;
    }
    source = current.data ? current.data : "";
    while (source[pos]) {
        size_t line_start = pos;
        size_t line_len = 0;
        char inc_name[128];
        while (source[pos] && source[pos] != '\n') pos++;
        line_len = pos - line_start;
        if (!in_block_comment && jit_line_is_local_include(source + line_start, line_len, inc_name, sizeof(inc_name))) {
            char inc_path[256];
            char inc_dir[256];
            char auto_src_path[256];
            char *inc_src = NULL;
            char *auto_src = NULL;
            if (jit_path_join(inc_path, sizeof(inc_path), base_dir, inc_name) != 0) {
                free(current.data);
                if (msg && msg_size) snprintf(msg, msg_size, "JIT: bad include path: %s", inc_name);
                return -1;
            }
            if (jit_stack_contains(stack, depth, inc_path)) {
                free(current.data);
                if (msg && msg_size) snprintf(msg, msg_size, "JIT: recursive include: %s", inc_path);
                return -1;
            }
            if (!jit_seen_contains(seen, seen_count ? *seen_count : 0, inc_path)) {
                if (jit_read_text_file(inc_path, &inc_src, NULL, msg, msg_size) != 0) {
                    free(current.data);
                    return -1;
                }
                if (jit_path_dirname(inc_path, inc_dir, sizeof(inc_dir)) != 0) {
                    free(inc_src);
                    free(current.data);
                    if (msg && msg_size) snprintf(msg, msg_size, "JIT: bad include dir: %s", inc_path);
                    return -1;
                }
                if (jit_seen_add(seen, seen_count, inc_path) != 0) {
                    free(inc_src);
                    free(current.data);
                    if (msg && msg_size) snprintf(msg, msg_size, "JIT: include cache full");
                    return -1;
                }
                stack[depth] = inc_path;
                if (jit_expand_debug_source_recursive(out, inc_src, inc_path, inc_dir, stack, depth + 1, seen, seen_count, msg, msg_size) != 0) {
                    free(inc_src);
                    free(current.data);
                    return -1;
                }
                stack[depth] = NULL;
                free(inc_src);
            }
            if (jit_header_to_source_path(auto_src_path, sizeof(auto_src_path), inc_path) == 0 &&
                !jit_seen_contains(seen, seen_count ? *seen_count : 0, auto_src_path)) {
                if (jit_read_text_file(auto_src_path, &auto_src, NULL, NULL, 0) == 0 && auto_src) {
                    if (jit_path_dirname(auto_src_path, inc_dir, sizeof(inc_dir)) != 0) {
                        free(auto_src);
                        free(current.data);
                        if (msg && msg_size) snprintf(msg, msg_size, "JIT: bad source dir: %s", auto_src_path);
                        return -1;
                    }
                    if (jit_seen_add(seen, seen_count, auto_src_path) != 0) {
                        free(auto_src);
                        free(current.data);
                        if (msg && msg_size) snprintf(msg, msg_size, "JIT: include cache full");
                        return -1;
                    }
                    stack[depth] = auto_src_path;
                    if (jit_expand_debug_source_recursive(out, auto_src, auto_src_path, inc_dir, stack, depth + 1, seen, seen_count, msg, msg_size) != 0) {
                        free(auto_src);
                        free(current.data);
                        return -1;
                    }
                    stack[depth] = NULL;
                    free(auto_src);
                }
            }
        } else {
            if (jit_source_buf_append_n(out, source + line_start, line_len) != 0) {
                free(current.data);
                if (msg && msg_size) snprintf(msg, msg_size, "JIT: expanded source too large");
                return -1;
            }
        }
        if (source[pos] == '\n') {
            if (jit_source_buf_append_n(out, "\n", 1) != 0) {
                free(current.data);
                if (msg && msg_size) snprintf(msg, msg_size, "JIT: expanded source too large");
                return -1;
            }
            pos++;
        }
        for (size_t i = line_start; i < line_start + line_len; i++) {
            if (!in_block_comment && source[i] == '/' && source[i + 1] == '*') {
                in_block_comment = 1;
                i++;
            } else if (in_block_comment && source[i] == '*' && source[i + 1] == '/') {
                in_block_comment = 0;
                i++;
            }
        }
    }
    free(current.data);
    return 0;
}

static char *jit_load_source_from_path(const char *path, char *msg, size_t msg_size) {
    char *src = NULL;
    char dir[256];
    struct jit_source_buf out;
    const char *stack[JIT_INCLUDE_MAX_DEPTH];
    char seen[JIT_SEEN_MAX][256];
    int seen_count = 0;
    memset(&out, 0, sizeof(out));
    memset(stack, 0, sizeof(stack));
    memset(seen, 0, sizeof(seen));
    if (msg && msg_size) msg[0] = '\0';
    if (jit_read_text_file(path, &src, NULL, msg, msg_size) != 0) return NULL;
    if (jit_path_dirname(path, dir, sizeof(dir)) != 0) {
        free(src);
        if (msg && msg_size) snprintf(msg, msg_size, "JIT: bad source path");
        return NULL;
    }
    stack[0] = path;
    if (jit_seen_add(seen, &seen_count, path) != 0) {
        free(src);
        if (msg && msg_size) snprintf(msg, msg_size, "JIT: include cache full");
        return NULL;
    }
    if (jit_expand_source_recursive(&out, src, dir, stack, 1, seen, &seen_count, msg, msg_size) != 0) {
        free(src);
        free(out.data);
        return NULL;
    }
    free(src);
    if (!out.data) {
        out.data = (char *)malloc(1);
        if (!out.data) {
            if (msg && msg_size) snprintf(msg, msg_size, "JIT: alloc failed");
            return NULL;
        }
        out.data[0] = '\0';
    }
    return out.data;
}

static char *jit_load_debug_source_from_path(const char *path, char *msg, size_t msg_size) {
    char *src = NULL;
    char dir[256];
    struct jit_source_buf expanded;
    const char *stack[JIT_INCLUDE_MAX_DEPTH];
    char seen[JIT_SEEN_MAX][256];
    int seen_count = 0;
    memset(&expanded, 0, sizeof(expanded));
    memset(stack, 0, sizeof(stack));
    memset(seen, 0, sizeof(seen));
    jit_debug_probe_sites_reset();
    if (msg && msg_size) msg[0] = '\0';
    if (jit_read_text_file(path, &src, NULL, msg, msg_size) != 0) return NULL;
    if (jit_path_dirname(path, dir, sizeof(dir)) != 0) {
        free(src);
        if (msg && msg_size) snprintf(msg, msg_size, "JIT: bad source path");
        return NULL;
    }
    stack[0] = path;
    if (jit_seen_add(seen, &seen_count, path) != 0) {
        free(src);
        if (msg && msg_size) snprintf(msg, msg_size, "JIT: include cache full");
        return NULL;
    }
    if (jit_expand_debug_source_recursive(&expanded, src, path, dir, stack, 1, seen, &seen_count, msg, msg_size) != 0) {
        free(src);
        free(expanded.data);
        return NULL;
    }
    free(src);
    if (!expanded.data) {
        expanded.data = (char *)malloc(1);
        if (!expanded.data) {
            if (msg && msg_size) snprintf(msg, msg_size, "JIT: alloc failed");
            return NULL;
        }
        expanded.data[0] = '\0';
    }
    return expanded.data;
}

static int jit_compile_job(struct jit_job *job, const char *source, char *msg, size_t msg_size) {
    if (msg && msg_size) msg[0] = '\0';
    if (!job) return -1;
    lib_printf("[JITDBG] job=%d source=%lx first='%c'\n",
               job->id, (unsigned long)(uintptr_t)source, source ? source[0] : '?');
    job->tcc = tcc_new();
    if (!job->tcc) {
        job->state = JIT_JOB_FAILED;
        if (msg && msg_size) snprintf(msg, msg_size, "JIT: init failed");
        return -1;
    }
    jit_uheap_reset(job);
    lib_printf("[JITDBG] job=%d uheap=%lx size=%u\n",
               job->id, (unsigned long)(uintptr_t)job->uheap, JIT_UHEAP_SIZE);
    lib_printf("[JITDBG] tcc_new s=%lx\n", (unsigned long)(uintptr_t)job->tcc);
    tcc_set_error_func(job->tcc, NULL, tcc_error_report);
    tcc_set_options(job->tcc, "-nostdlib -gdwarf");
    lib_printf("[JITDBG] options=-nostdlib -gdwarf\n");
    lib_printf("[JITDBG] set_output_type rc=%d\n", tcc_set_output_type(job->tcc, TCC_OUTPUT_MEMORY));
    lib_printf("[JITDBG] sym printf=%lx print=%lx get_ticks=%lx\n",
               (unsigned long)(uintptr_t)lib_printf,
               (unsigned long)(uintptr_t)lib_printf,
               (unsigned long)(uintptr_t)get_millisecond_timer);
    jit_add_os_symbols(job->tcc);
    char *compile_source = jit_make_source_with_prelude(source);
    if (!compile_source) {
        job->state = JIT_JOB_FAILED;
        if (msg && msg_size) snprintf(msg, msg_size, "JIT: source alloc failed");
        return -1;
    }
    lib_printf("[JITDBG] compile start\n");
    int compile_rc = tcc_compile_string(job->tcc, compile_source);
    free(compile_source);
    lib_printf("[JITDBG] compile rc=%d\n", compile_rc);
    if (compile_rc == -1) {
        job->state = JIT_JOB_FAILED;
        if (msg && msg_size) snprintf(msg, msg_size, "JIT: compile failed");
        return -1;
    }
    lib_printf("[JITDBG] relocate start\n");
    int reloc_rc = tcc_relocate(job->tcc, TCC_RELOCATE_AUTO);
    lib_printf("[JITDBG] relocate rc=%d\n", reloc_rc);
    if (reloc_rc != 0) {
        job->state = JIT_JOB_FAILED;
        if (msg && msg_size) snprintf(msg, msg_size, "JIT: relocate failed");
        return -1;
    }
    job->main_func = (int (*)(void))tcc_get_symbol(job->tcc, "main");
    lib_printf("[JITDBG] main=%lx\n", (unsigned long)(uintptr_t)job->main_func);
    if (!job->main_func) {
        job->state = JIT_JOB_FAILED;
        if (msg && msg_size) snprintf(msg, msg_size, "JIT: main not found");
        return -1;
    }
    {
        unsigned long text_lo = 0;
        unsigned long text_hi = 0;
        uint32_t code_lo = (uint32_t)(uintptr_t)job->main_func;
        uint32_t code_hi = code_lo + 4u;
        if (tcc_get_text_bounds(job->tcc, &text_lo, &text_hi) == 0) {
            code_lo = (uint32_t)text_lo;
            code_hi = (uint32_t)text_hi;
        }
        lib_printf("[JITDBG] text=%lx..%lx\n",
                   (unsigned long)code_lo, (unsigned long)code_hi);
        job->text_lo = code_lo;
        job->text_hi = code_hi;
        jit_debug_set_code_range(code_lo, code_hi);
        if (!jit_debug_build_line_map_from_dwarf(job->tcc) && jit_debug_probe_site_count > 0) {
            jit_debug_build_line_map_from_ebreaks(code_lo, code_hi);
        }
    }
    job->state = JIT_JOB_READY;
    if (msg && msg_size) snprintf(msg, msg_size, "JIT job %d ready.", job->id);
    return 0;
}

static void jit_job_cleanup(struct jit_job *job) {
    if (!job) return;
    jit_debug_cleanup_code_patches();
    jit_cleanup_resources(job);
    if (job->tcc) {
        tcc_delete(job->tcc);
        job->tcc = NULL;
    }
}

static void jit_job_execute(struct jit_job *job) {
    if (!job || !job->main_func) return;
    job->task_id = task_current();
    job->state = JIT_JOB_RUNNING;
    job->cancel_requested = 0;
    job->slice_start_ms = get_millisecond_timer();
    asm volatile("fence.i");
    jit_repair_main_prologue((uint32_t)(uintptr_t)job->main_func);
    jit_check_broken_text(job->text_lo, job->text_hi);
    jit_dump_code_bytes("before-main", (uint32_t)(uintptr_t)job->main_func);
    {
        uint32_t main_addr = (uint32_t)(uintptr_t)job->main_func;
        uint32_t main_word = *(volatile uint32_t *)(uintptr_t)main_addr;
        if ((main_addr & 3u) != 0 || (main_word & 3u) != 3u) {
            lib_printf("[JITERR] invalid main entry addr=%x word=%x low2=%x\n",
                       main_addr, main_word, main_word & 3u);
            job->exit_code = -3;
            job->state = JIT_JOB_FAILED;
            jit_job_cleanup(job);
            if (!job->bg && job->waiter_task_id >= 0) {
                task_wake(job->waiter_task_id);
                task_sleep_current();
            }
            return;
        }
    }
    lib_printf("[JITDBG] job=%d call main\n", job->id);
    job->exit_code = job->main_func();
    lib_printf("[JITDBG] job=%d main returned rc=%d\n", job->id, job->exit_code);
    if (job->cancel_requested || job->state == JIT_JOB_FAILED) {
        job->exit_code = -2;
        job->state = JIT_JOB_FAILED;
        lib_printf("[JITDBG] job=%d cancelled\n", job->id);
    } else {
        job->state = JIT_JOB_DONE;
    }
    jit_job_cleanup(job);
    if (!job->bg && job->waiter_task_id >= 0) {
        task_wake(job->waiter_task_id);
        task_sleep_current();
    }
}

static void jit_task_entry_idx(int idx) {
    while (1) {
        if (idx >= 0 && idx < JIT_MAX_JOBS && jit_jobs[idx].state == JIT_JOB_READY) {
            jit_job_execute(&jit_jobs[idx]);
        }
        task_sleep_current();
    }
}

static void jit_task0(void) { jit_task_entry_idx(0); }
static void jit_task1(void) { jit_task_entry_idx(1); }
static void jit_task2(void) { jit_task_entry_idx(2); }
static void jit_task3(void) { jit_task_entry_idx(3); }

static void (*jit_task_entries[JIT_MAX_JOBS])(void) = {
#if JIT_MAX_JOBS == 1
    jit_task0
#else
    jit_task0, jit_task1, jit_task2, jit_task3
#endif
};

__attribute__((noreturn)) void os_jit_cancel_trampoline(void) {
    task_sleep_current();
    for (;;) {
        task_os();
    }
}

uint8_t jit_debug_pause_stack[4096] __attribute__((aligned(16)));

void jit_debug_pause_wait(void) {
    task_sleep_current();
}

__attribute__((naked, noreturn)) void os_jit_debug_pause_trampoline(void) {
    asm volatile(
        "la sp, jit_debug_pause_stack\n"
        "li t0, 4096\n"
        "add sp, sp, t0\n"
        "andi sp, sp, -16\n"
        "call jit_debug_pause_wait\n"
        "la t5, jit_debug_resume_pc\n"
        "lw t5, 0(t5)\n"
        "csrw mepc, t5\n"
        "csrr t0, mstatus\n"
        "li t1, ~(3 << 11)\n"
        "and t0, t0, t1\n"
        "li t1, (3 << 11)\n"
        "or t0, t0, t1\n"
        "csrw mstatus, t0\n"
        "la t6, jit_debug_saved_frame\n"
        "lw ra, 0(t6)\n"
        "lw sp, 4(t6)\n"
        "lw gp, 8(t6)\n"
        "lw tp, 12(t6)\n"
        "lw t0, 16(t6)\n"
        "lw t1, 20(t6)\n"
        "lw t2, 24(t6)\n"
        "lw s0, 28(t6)\n"
        "lw s1, 32(t6)\n"
        "lw a0, 36(t6)\n"
        "lw a1, 40(t6)\n"
        "lw a2, 44(t6)\n"
        "lw a3, 48(t6)\n"
        "lw a4, 52(t6)\n"
        "lw a5, 56(t6)\n"
        "lw a6, 60(t6)\n"
        "lw a7, 64(t6)\n"
        "lw s2, 68(t6)\n"
        "lw s3, 72(t6)\n"
        "lw s4, 76(t6)\n"
        "lw s5, 80(t6)\n"
        "lw s6, 84(t6)\n"
        "lw s7, 88(t6)\n"
        "lw s8, 92(t6)\n"
        "lw s9, 96(t6)\n"
        "lw s10, 100(t6)\n"
        "lw s11, 104(t6)\n"
        "lw t3, 108(t6)\n"
        "lw t4, 112(t6)\n"
        "lw t5, 116(t6)\n"
        "lw t6, 120(t6)\n"
        "mret\n"
    );
}

void os_jit_init(void) {
    if (jit_initialized) return;
    for (int i = 0; i < JIT_MAX_JOBS; i++) {
        memset(&jit_jobs[i], 0, sizeof(jit_jobs[i]));
        jit_jobs[i].task_id = -1;
        jit_jobs[i].waiter_task_id = -1;
        jit_jobs[i].uheap = jit_user_heaps[i];
        for (int j = 0; j < JIT_MAX_FDS; j++) jit_jobs[i].fds[j] = -1;
    }
    memset(jit_shared_area, 0, sizeof(jit_shared_area));
    jit_initialized = 1;
}

int os_jit_run(const char *source, int owner_win_id) {
    struct jit_job *job = jit_alloc_job(0, owner_win_id);
    char msg[96];
    if (!job) {
        lib_printf("JIT: no free job slot\n");
        return -1;
    }
    int slot = (int)(job - jit_jobs);
    if (jit_compile_job(job, source, msg, sizeof(msg)) != 0) {
        lib_printf("%s\n", msg[0] ? msg : "JIT: failed");
        jit_job_cleanup(job);
        return -1;
    }
    job->bg = 0;
    job->owner_win_id = owner_win_id;
    job->waiter_task_id = task_current();
    if (job->task_id < 0 || job->task_id == job->waiter_task_id) {
        job->task_id = task_create(jit_task_entries[slot], 0, 1);
    } else {
        task_reset(job->task_id, jit_task_entries[slot], 0, 1);
    }
    task_wake(job->task_id);
    while (job->state == JIT_JOB_READY || job->state == JIT_JOB_RUNNING) {
        task_sleep_current();
    }
    lib_printf("[CTRLDBG] os_jit_run return owner=%d job=%d rc=%d state=%d waiter=%d task=%d\n",
               owner_win_id, job->id, job->exit_code, job->state,
               job->waiter_task_id, job->task_id);
    return job->exit_code;
}

int os_jit_run_file(const char *path, int owner_win_id) {
    char msg[128];
    char *source = NULL;
    int rc;
    source = jit_load_source_from_path(path, msg, sizeof(msg));
    if (!source) {
        lib_printf("%s\n", msg[0] ? msg : "JIT: failed");
        return -1;
    }
    rc = os_jit_run(source, owner_win_id);
    free(source);
    return rc;
}

int os_jit_run_bg(const char *source, int owner_win_id, char *msg, size_t msg_size) {
    struct jit_job *job = jit_alloc_job(1, owner_win_id);
    if (!job) {
        if (msg && msg_size) snprintf(msg, msg_size, "ERR: no free JIT job slot.");
        return -1;
    }
    int slot = (int)(job - jit_jobs);
    if (jit_compile_job(job, source, msg, msg_size) != 0) {
        jit_job_cleanup(job);
        return -1;
    }
    if (job->task_id < 0) {
        job->task_id = task_create(jit_task_entries[slot], 0, 1);
    } else {
        task_reset(job->task_id, jit_task_entries[slot], 0, 1);
    }
    task_wake(job->task_id);
    if (msg && msg_size) snprintf(msg, msg_size, "JIT job %d started in background.", job->id);
    return job->id;
}

int os_jit_run_bg_file(const char *path, int owner_win_id, char *msg, size_t msg_size) {
    char local_msg[128];
    char *source = jit_load_source_from_path(path, local_msg, sizeof(local_msg));
    int rc;
    if (!source) {
        if (msg && msg_size) snprintf(msg, msg_size, "%s", local_msg[0] ? local_msg : "JIT: failed");
        return -1;
    }
    rc = os_jit_run_bg(source, owner_win_id, msg, msg_size);
    free(source);
    return rc;
}

int os_jit_run_bg_debug_file(const char *path, int owner_win_id, char *msg, size_t msg_size) {
    char local_msg[128];
    char *source;
    int rc;
    if (owner_win_id >= 0) os_jit_cancel_by_owner(owner_win_id);
    source = jit_load_debug_source_from_path(path, local_msg, sizeof(local_msg));
    if (!source) {
        if (msg && msg_size) snprintf(msg, msg_size, "%s", local_msg[0] ? local_msg : "JIT: failed");
        return -1;
    }
    jit_debug_begin();
    /*
     * The RISC-V load/store watch codegen path injects a large save/call/restore
     * sequence around memory operations. It currently emits illegal halfwords in
     * JIT text, which breaks instruction stepping. Keep debugger stepping stable;
     * explicit watch state and libc/appfs wrapper hits still work.
     */
    jit_debug_codegen_watch_enabled = 0;
    rc = os_jit_run_bg(source, owner_win_id, msg, msg_size);
    jit_debug_codegen_watch_enabled = 0;
    free(source);
    return rc;
}

void os_jit_ps(char *out, size_t out_size) {
    int pos = 0;
    if (!out || out_size == 0) return;
    out[0] = '\0';
    pos += snprintf(out + pos, out_size - pos, "JIT jobs:\n");
    for (int i = 0; i < JIT_MAX_JOBS && pos < (int)out_size - 1; i++) {
        if (jit_jobs[i].state == JIT_JOB_EMPTY) continue;
        uint32_t used = 0, freeb = 0, blocks = 0;
        if (jit_jobs[i].uheap_head) {
            struct jit_ublock *b = jit_jobs[i].uheap_head;
            while (b) {
                if (b->magic != JIT_UHEAP_MAGIC) break;
                if (b->free) freeb += b->size;
                else used += b->size;
                blocks++;
                b = b->next;
            }
        }
        pos += snprintf(out + pos, out_size - pos,
                        "#%d slot=%d %s task=%d u=%uKB f=%uKB blocks=%u rc=%d\n",
                        jit_jobs[i].id, i, jit_state_name(jit_jobs[i].state),
                        jit_jobs[i].task_id, used / 1024, freeb / 1024,
                        blocks, jit_jobs[i].exit_code);
    }
}

int os_jit_kill(int id, char *msg, size_t msg_size) {
    for (int i = 0; i < JIT_MAX_JOBS; i++) {
        if (jit_jobs[i].state != JIT_JOB_EMPTY && jit_jobs[i].id == id) {
            jit_jobs[i].cancel_requested = 1;
            if (jit_jobs[i].task_id >= 0) {
                task_reset(jit_jobs[i].task_id, jit_task_entries[i], 0, 1);
                task_sleep(jit_jobs[i].task_id);
            }
            jit_jobs[i].state = JIT_JOB_FAILED;
            jit_jobs[i].exit_code = -1;
            jit_job_cleanup(&jit_jobs[i]);
            if (!jit_jobs[i].bg && jit_jobs[i].waiter_task_id >= 0) {
                task_wake(jit_jobs[i].waiter_task_id);
            }
            if (msg && msg_size) snprintf(msg, msg_size, "JIT job %d killed.", id);
            return 0;
        }
    }
    if (msg && msg_size) snprintf(msg, msg_size, "ERR: JIT job %d not found.", id);
    return -1;
}

int os_jit_cancel_task(int task_id) {
    for (int i = 0; i < JIT_MAX_JOBS; i++) {
        if (jit_jobs[i].state != JIT_JOB_EMPTY &&
            (jit_jobs[i].task_id == task_id || jit_jobs[i].waiter_task_id == task_id)) {
            jit_jobs[i].cancel_requested = 1;
            if (jit_jobs[i].task_id >= 0) {
                task_reset(jit_jobs[i].task_id, jit_task_entries[i], 0, 1);
                task_sleep(jit_jobs[i].task_id);
            }
            jit_jobs[i].state = JIT_JOB_FAILED;
            jit_jobs[i].exit_code = -2;
            int waiter = jit_jobs[i].waiter_task_id;
            int job_id = jit_jobs[i].id;
            jit_job_cleanup(&jit_jobs[i]);
            if (waiter >= 0 && waiter != task_current()) {
                task_wake(waiter);
            }
            return job_id;
        }
    }
    return -1;
}

int os_jit_cancel_by_owner(int owner_win_id) {
    int count = 0;
    for (int i = 0; i < JIT_MAX_JOBS; i++) {
        if (jit_jobs[i].owner_win_id != owner_win_id) continue;
        if (jit_jobs[i].state != JIT_JOB_COMPILING &&
            jit_jobs[i].state != JIT_JOB_READY &&
            jit_jobs[i].state != JIT_JOB_RUNNING) continue;
        {
            lib_printf("[CTRLDBG] cancel owner=%d job=%d slot=%d state=%d task=%d waiter=%d\n",
                       owner_win_id, jit_jobs[i].id, i, jit_jobs[i].state,
                       jit_jobs[i].task_id, jit_jobs[i].waiter_task_id);
            jit_jobs[i].cancel_requested = 1;
            if (jit_jobs[i].task_id >= 0) {
                task_reset(jit_jobs[i].task_id, jit_task_entries[i], 0, 1);
                task_sleep(jit_jobs[i].task_id);
            }
            jit_jobs[i].state = JIT_JOB_FAILED;
            jit_jobs[i].exit_code = -2;
            int waiter = jit_jobs[i].waiter_task_id;
            jit_job_cleanup(&jit_jobs[i]);
            if (waiter >= 0 && waiter != task_current()) {
                task_wake(waiter);
            }
            count++;
        }
    }
    return count;
}

int os_jit_cancel_running_owner_from_trap(int owner_win_id) {
    int current = task_current();
    for (int i = 0; i < JIT_MAX_JOBS; i++) {
        if (jit_jobs[i].state == JIT_JOB_EMPTY) continue;
        if (jit_jobs[i].state != JIT_JOB_RUNNING) continue;
        if (jit_jobs[i].owner_win_id != owner_win_id) continue;
        if (jit_jobs[i].task_id != current) continue;
        lib_printf("[CTRLDBG] trap-cancel owner=%d job=%d slot=%d state=%d task=%d waiter=%d\n",
                   owner_win_id, jit_jobs[i].id, i, jit_jobs[i].state,
                   jit_jobs[i].task_id, jit_jobs[i].waiter_task_id);
        jit_jobs[i].cancel_requested = 1;
        jit_jobs[i].state = JIT_JOB_FAILED;
        jit_jobs[i].exit_code = -2;
        int waiter = jit_jobs[i].waiter_task_id;
        jit_job_cleanup(&jit_jobs[i]);
        if (waiter >= 0 && waiter != current) {
            task_wake(waiter);
        }
        return 1;
    }
    return 0;
}

int os_jit_owner_active(int owner_win_id) {
    for (int i = 0; i < JIT_MAX_JOBS; i++) {
        if (jit_jobs[i].owner_win_id != owner_win_id) continue;
        if (jit_jobs[i].state == JIT_JOB_COMPILING ||
            jit_jobs[i].state == JIT_JOB_READY ||
            jit_jobs[i].state == JIT_JOB_RUNNING) {
            lib_printf("[CTRLDBG] owner_active owner=%d job=%d slot=%d state=%d\n",
                       owner_win_id, jit_jobs[i].id, i, jit_jobs[i].state);
            return 1;
        }
    }
    return 0;
}
