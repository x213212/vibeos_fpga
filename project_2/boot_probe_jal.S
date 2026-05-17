    .option norvc
    .section .text, "ax", @progbits
    .global _start

_start:
    call marker
    j    park

marker:
    lui  t0, 0x10004
    li   t1, 0x333
    sw   t1, 60(t0)

park:
    wfi
    j    park
