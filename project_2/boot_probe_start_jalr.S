    .option norvc
    .section .text, "ax", @progbits
    .global _start

_start:
    csrwi mie, 0
    csrci mstatus, 8
    lui  t0, %hi(early_trap)
    addi t0, t0, %lo(early_trap)
    csrw mtvec, t0
    lui  sp, 0x80cd3
    addi sp, sp, 0
    andi sp, sp, -16
    auipc ra, 0
    jalr  ra, 40(ra)
    j     park

early_trap:
    csrr t1, mepc
    csrr t2, mcause
    lui  t0, 0x10004
    sw   t1, 32(t0)
    sw   t2, 36(t0)
    sw   t1, 60(t0)
    j    park

marker:
    lui  t0, 0x10004
    li   t1, 0x555
    sw   t1, 60(t0)

park:
    wfi
    j    park
