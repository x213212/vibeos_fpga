.equ STACK_SIZE, 65536 # 提升到 64KB

.section .text.init, "ax", @progbits
.global _start

_start:
#ifdef FPGA_MINIMAL
    csrwi mie, 0
    csrci mstatus, 8
    lui  t0, %hi(early_trap)
    addi t0, t0, %lo(early_trap)
    csrw mtvec, t0
#endif

#ifndef FPGA_MINIMAL
    csrr a0, mhartid
    bnez a0, park
#endif

#ifdef FPGA_MINIMAL
    lui  sp, %hi(stack_top)
    addi sp, sp, %lo(stack_top)
#else
    la   sp, stacks + STACK_SIZE
#endif
    andi sp, sp, -16

#ifdef FPGA_MINIMAL
    /*
     * The FPGA loader downloads os.bin as a flat image.  Because .apppt is a
     * later LOAD section, objcopy fills the .bss hole with zero bytes in the
     * binary.  Re-clearing the multi-megabyte BSS here causes long AXI write
     * bursts before the OS is alive, which has produced early mepc=0 traps on
     * the FPGA target.  Keep the normal C runtime clear for non-FPGA builds.
     */
    j skip_bss
#else
    la a0, _bss_start
    la a1, _bss_end
#endif
    bgeu a0, a1, skip_bss
bss_loop:
    sw zero, 0(a0)
    addi a0, a0, 4
    bltu a0, a1, bss_loop
skip_bss:
#ifdef FPGA_MINIMAL
    call os_start
    j    park
#else
    la   t0, os_main
    jr   t0
#endif

#ifdef FPGA_MINIMAL
.extern fpga_boot_trace
early_trap:
    csrr t1, mepc
    csrr t2, mcause
    csrr t3, mtval
    csrr t4, mstatus
    lui  t0, 0x10004
    sw   t1, 32(t0)
    sw   t2, 36(t0)
    sw   t3, 40(t0)
    sw   t4, 44(t0)
    sw   t1, 60(t0)
    j    park
#endif

park:
    wfi
    j park

.section .boot_stack, "aw", @nobits
.align 4
stacks:
    .skip STACK_SIZE
stack_top:
    .skip 256
