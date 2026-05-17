    .option norvc
    .section .text, "ax", @progbits
    .global _start

_start:
    lui  t0, 0x10004
    li   t1, 0x111
    sw   t1, 60(t0)
1:
    j    1b
