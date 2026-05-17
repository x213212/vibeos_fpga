ENTRY(_start)

SECTIONS
{
    . = 0x87000000;

    .text.start : ALIGN(4)
    {
        *(.text.start)
    }

    .text : ALIGN(4)
    {
        *(.text .text.*)
    }

    .rodata : ALIGN(4)
    {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(4)
    {
        *(.data .data.*)
    }

    .bss : ALIGN(4)
    {
        *(.bss .bss.*)
        *(COMMON)
    }
}
