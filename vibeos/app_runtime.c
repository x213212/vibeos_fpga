#include "user.h"
#include "riscv.h"

extern uint32_t APP_START, APP_END, APP_SIZE;

// 這些變數現在由 user.c 定義
extern void (*loaded_app_entry)(void);
extern reg_t loaded_app_resume_pc, loaded_app_user_sp, loaded_app_heap_lo, loaded_app_heap_hi, loaded_app_heap_cur, loaded_app_satp;
extern int loaded_app_exit_code, app_task_id, app_running;
extern uint32_t app_root_pt[1024], app_l2_pt[4][1024];

static uint8_t app_exit_stack[4096] __attribute__((aligned(16)));

static reg_t align_up_reg(reg_t v, reg_t align) {
    return (v + align - 1) & ~(align - 1);
}

reg_t app_exit_stack_top(void) {
    return (reg_t)(uintptr_t)(app_exit_stack + sizeof(app_exit_stack));
}

void app_vm_reset(void) {
    uint32_t app_start = (uint32_t)(uintptr_t)APP_START;
    uint32_t app_end = (uint32_t)(uintptr_t)APP_END;
    uint32_t app_pa;
    uint32_t app_page;

    memset(app_root_pt, 0, sizeof(app_root_pt));
    memset(app_l2_pt, 0, sizeof(app_l2_pt));

    app_pa = app_start;
    app_page = 0;
    while (app_pa < app_end) {
        uint32_t root_idx = (app_pa >> 22) & 0x3ffu;
        uint32_t l2_idx = (app_pa >> 12) & 0x3ffu;
        uint32_t chunk = app_page >> 10;
        uint32_t l2_pa = (uint32_t)(uintptr_t)&app_l2_pt[chunk][0];
        if ((app_pa & 0x3fffffU) == 0) {
            app_root_pt[root_idx] = (((l2_pa >> 12) << 10) | PTE_V);
        }
        app_l2_pt[chunk][l2_idx] = (((app_pa >> 12) << 10) |
                                    PTE_V | PTE_R | PTE_W | PTE_X |
                                    PTE_U | PTE_A | PTE_D);
        app_pa += PGSIZE;
        app_page++;
        if (app_page >= (sizeof(app_l2_pt) / sizeof(app_l2_pt[0])) * 1024) {
            break;
        }
    }
    loaded_app_satp = SATP_MODE_SV32 | ((reg_t)(uintptr_t)&app_root_pt[0] >> 12);
}

reg_t app_heap_alloc(reg_t size) {
    reg_t need = align_up_reg(size, 16);
    reg_t cur = align_up_reg(loaded_app_heap_cur, 16);
    if (need == 0) {
        need = 16;
    }
    if (cur < loaded_app_heap_lo) {
        cur = align_up_reg(loaded_app_heap_lo, 16);
    }
    if (loaded_app_heap_hi <= cur || need > loaded_app_heap_hi - cur) {
        return 0;
    }
    loaded_app_heap_cur = cur + need;
    return cur;
}

void app_heap_reset(reg_t lo, reg_t hi) {
    loaded_app_heap_lo = align_up_reg(lo, 16);
    loaded_app_heap_hi = hi & ~(reg_t)0xF;
    loaded_app_heap_cur = loaded_app_heap_lo;
}

int app_load_elf_image(const unsigned char *buf, uint32_t size, char *out) {
    const struct elf32_ehdr *eh;
    const struct elf32_phdr *ph;
    uint32_t app_start = (uint32_t)(uintptr_t)APP_START;
    uint32_t app_end = (uint32_t)(uintptr_t)APP_END;

    if (size < sizeof(*eh)) {
        lib_strcpy(out, "ERR: Bad ELF.");
        return -11;
    }
    eh = (const struct elf32_ehdr *)buf;
    if (eh->e_ident[0] != ELF32_MAG0 || eh->e_ident[1] != ELF32_MAG1 ||
        eh->e_ident[2] != ELF32_MAG2 || eh->e_ident[3] != ELF32_MAG3 ||
        eh->e_ident[4] != ELF32_CLASS_32 ||
        eh->e_ident[5] != ELF32_DATA_LSB ||
        eh->e_ident[6] != ELF32_VERSION_CURRENT ||
        eh->e_type != ELF32_ET_EXEC ||
        eh->e_machine != ELF32_EM_RISCV ||
        eh->e_version != ELF32_VERSION_CURRENT) {
        lib_strcpy(out, "ERR: Bad ELF.");
        return -11;
    }
    if (eh->e_phoff == 0 || eh->e_phentsize < sizeof(struct elf32_phdr)) {
        lib_strcpy(out, "ERR: Bad ELF PH.");
        return -12;
    }
    if ((uint64_t)eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize > (uint64_t)size) {
        lib_strcpy(out, "ERR: Bad ELF PH.");
        return -12;
    }
    if (eh->e_entry < app_start || eh->e_entry >= app_end) {
        lib_strcpy(out, "ERR: Bad ELF Entry.");
        return -13;
    }

    for (uint32_t i = 0; i < eh->e_phnum; i++) {
        ph = (const struct elf32_phdr *)(buf + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type != ELF32_PT_LOAD) continue;
        if ((uint64_t)ph->p_offset + (uint64_t)ph->p_filesz > (uint64_t)size) {
            lib_strcpy(out, "ERR: Bad ELF Seg.");
            return -14;
        }
        if (ph->p_memsz < ph->p_filesz) {
            lib_strcpy(out, "ERR: Bad ELF Seg.");
            return -14;
        }
        if (ph->p_memsz == 0) continue;
        if (ph->p_vaddr < app_start || (uint64_t)ph->p_vaddr + (uint64_t)ph->p_memsz > (uint64_t)app_end) {
            lib_strcpy(out, "ERR: App Too Large.");
            return -7;
        }
        memcpy((void *)(uintptr_t)ph->p_vaddr, buf + ph->p_offset, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz) {
            memset((void *)(uintptr_t)(ph->p_vaddr + ph->p_filesz), 0, ph->p_memsz - ph->p_filesz);
        }
    }
    loaded_app_entry = (void (*)(void))(uintptr_t)eh->e_entry;
    return 0;
}

int app_prepare_argv_stack(const char *app_name, const char *argstr, reg_t *user_sp_io) {
    char arg_copy[COLS];
    const char *argv_src[APP_MAX_ARGS];
    char *argv_ptrs[APP_MAX_ARGS];
    int argc = 0;
    uint32_t str_bytes = 0;
    reg_t frame_size;
    reg_t frame_base;
    uint32_t *argc_slot;
    char **argv_slot;
    char *str_slot;

    if (!user_sp_io || !app_name) return -1;

    argv_src[argc++] = app_name;
    if (argstr && argstr[0]) {
        int len = 0;
        while (argstr[len] && len < COLS - 1) {
            arg_copy[len] = argstr[len];
            len++;
        }
        arg_copy[len] = '\0';
        char *p = arg_copy;
        while (*p) {
            while (*p == ' ') p++;
            if (*p == '\0') break;
            if (argc >= APP_MAX_ARGS) return -2;
            argv_src[argc++] = p;
            while (*p && *p != ' ') p++;
            if (*p == ' ') {
                *p++ = '\0';
            }
        }
    }

    for (int i = 0; i < argc; i++) {
        str_bytes += (uint32_t)strlen(argv_src[i]) + 1U;
    }

    frame_size = align_up_reg(4U + (reg_t)(argc + 1) * 4U + (reg_t)str_bytes, 16);
    if (frame_size > *user_sp_io || *user_sp_io - frame_size <= (reg_t)(uintptr_t)APP_START + APP_HEAP_GUARD) {
        return -3;
    }
    frame_base = *user_sp_io - frame_size;
    argc_slot = (uint32_t *)(uintptr_t)frame_base;
    argv_slot = (char **)(uintptr_t)(frame_base + 4U);
    str_slot = (char *)(uintptr_t)(frame_base + 4U + (argc + 1) * 4U);

    for (int i = 0; i < argc; i++) {
        uint32_t len = (uint32_t)strlen(argv_src[i]) + 1U;
        argv_ptrs[i] = str_slot;
        memcpy(str_slot, argv_src[i], len);
        str_slot += len;
    }
    argv_ptrs[argc] = 0;
    *argc_slot = (uint32_t)argc;
    for (int i = 0; i <= argc; i++) {
        argv_slot[i] = argv_ptrs[i];
    }
    *user_sp_io = frame_base;
    return 0;
}

extern void app_exit_trampoline(void);
static void app_enter_user(void (*entry)(void), reg_t user_sp) {
    reg_t epc = (reg_t)(uintptr_t)entry;
    w_mepc(epc);
    w_mstatus((r_mstatus() & ~MSTATUS_MPP_MASK) | MSTATUS_MPP_U);
    w_satp(loaded_app_satp);
    sfence_vma();
    asm volatile("mv sp, %0\n\tmret\n\t" : : "r"(user_sp) : "memory");
    panic("app_enter_user");
}

void app_bootstrap(void) {
    while (loaded_app_entry == 0) {
        task_sleep_current();
    }
    void (*entry)(void) = loaded_app_entry;
    loaded_app_entry = 0;
    app_running = 1;
    loaded_app_resume_pc = (reg_t)(uintptr_t)app_exit_trampoline;
    if (entry) {
        app_enter_user(entry, loaded_app_user_sp);
    }
    app_exit_trampoline();
}

reg_t app_exit_resume_pc(void) {
    return loaded_app_resume_pc;
}

void app_mark_exited(void) {
    app_running = 0;
    loaded_app_entry = 0;
}
