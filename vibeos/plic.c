// plic.c
#include "os.h"
#include <stdint.h>

// ---------- PLIC definitions ----------
#define PLIC_BASE             0x0c000000UL
#define PLIC_PRIORITY(id)     (PLIC_BASE + ((id) * 4))
#define PLIC_PENDING(id)      (PLIC_BASE + 0x1000 + (((id) / 32) * 4))
#define PLIC_MENABLE(hart)    (PLIC_BASE + 0x2000 + (hart) * 0x80)
#define PLIC_MTHRESHOLD(hart) (PLIC_BASE + 0x200000 + (hart) * 0x1000)
#define PLIC_MCLAIM(hart)     (PLIC_BASE + 0x200004 + (hart) * 0x1000)
#define PLIC_MCOMPLETE(hart)  PLIC_MCLAIM(hart) // Write back to the same register to complete

// helpers
static inline void write32(uintptr_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
    asm volatile("" ::: "memory");
}
static inline uint32_t read32(uintptr_t addr) {
    return *(volatile uint32_t *)addr;
}

// ---------- mscratch setup (xv6-style) ----------
#define MAX_HARTS 4
#define SCRATCH_PER_HART 256  // 256 bytes per hart, enough for reg_save/reg_load

__attribute__((aligned(16))) static uint8_t scratch_space[MAX_HARTS * SCRATCH_PER_HART];

static inline int current_hart_id(void) {
#ifdef FPGA_MINIMAL
    return 0;
#else
    return r_mhartid();
#endif
}

void setup_mscratch_for_hart(void) {
    int hart = current_hart_id();
    uintptr_t base = (uintptr_t)(scratch_space + hart * SCRATCH_PER_HART);
    // optional: zero it out to avoid stale data
    for (int i = 0; i < SCRATCH_PER_HART; i += 8) {
        *(uint64_t *)(base + i) = 0;
    }
    w_mscratch(base);
}

// PLIC priority limits: read from your platform (hardcoded here based on qemu virt)
#define PLIC_MAX_PRIORITY 7

static void plic_enable_irq(int hart, uint32_t irq, uint32_t priority) {
    if (irq == 0) return;

    // clamp priority
    if (priority == 0) priority = 1;
    if (priority > PLIC_MAX_PRIORITY) priority = PLIC_MAX_PRIORITY;
    write32(PLIC_PRIORITY(irq), priority);

    // enable in the appropriate word
    uintptr_t enable_word = PLIC_MENABLE(hart) + (irq / 32) * 4;
    uint32_t v = read32(enable_word);
    v |= (1u << (irq % 32));
    write32(enable_word, v);
}

void plic_init(void)
{
    int hart = current_hart_id();

    // Make sure mscratch is set if your trap vector uses it
    setup_mscratch_for_hart();

    // Set threshold = 0 to accept priority >=1
    write32(PLIC_MTHRESHOLD(hart), 0);

    // Enable relevant sources with priorities (example list)
    struct { uint32_t irq; uint32_t prio; } sources[] = {
        { UART0_IRQ, 1 },
        { VIRTIO_IRQ, 2 },
        { VIRTIO_IRQ2, 3 },
        { VIRTIO_IRQ3, 4 },
        { VIRTIO_IRQ4, 5 },
        { VIRTIO_IRQ5, 7 },
        { VIRTIO_IRQ6, 7 },
        { VIRTIO_IRQ7, 7 },
        { VIRTIO_IRQ8, 7 },
        { VIRTIO_IRQ9, 7 },
    };
    int n = sizeof(sources) / sizeof(sources[0]);
    for (int i = 0; i < n; i++) {
        plic_enable_irq(hart, sources[i].irq, sources[i].prio);
    }

    // Enable external interrupts in mie & global
    w_mie(r_mie() | MIE_MEIE);
    w_mstatus(r_mstatus() | MSTATUS_MIE);
}

int plic_claim()
{
    int hart = current_hart_id();
    return (int)read32(PLIC_MCLAIM(hart));
}

void plic_complete(int irq)
{
    if (irq == 0) return;
    int hart = current_hart_id();
    write32(PLIC_MCOMPLETE(hart), (uint32_t)irq);
}
