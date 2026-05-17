    .option norvc
    .section .text, "ax", @progbits
    .global _start

_start:
    csrwi mie, 0
    lui  t0, 0x10004
    li   t1, 0x441
    sw   t1, 60(t0)
    csrci mstatus, 8
    li   t1, 0x442
    sw   t1, 60(t0)
    lui  t2, 0x80000
    addi t2, t2, 64
    csrw mtvec, t2
    li   t1, 0x443
    sw   t1, 60(t0)
1:
    wfi
    j    1b
