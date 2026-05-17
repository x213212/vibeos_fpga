# This Code derived from xv6-riscv (64bit)
# -- https://github.com/mit-pdos/xv6-riscv/blob/riscv/kernel/swtch.S

# ============ MACRO ==================
.macro ctx_save base
        sw ra, 0(\base)
        sw sp, 4(\base)
        sw s0, 8(\base)
        sw s1, 12(\base)
        sw s2, 16(\base)
        sw s3, 20(\base)
        sw s4, 24(\base)
        sw s5, 28(\base)
        sw s6, 32(\base)
        sw s7, 36(\base)
        sw s8, 40(\base)
        sw s9, 44(\base)
        sw s10, 48(\base)
        sw s11, 52(\base)
.endm

.macro ctx_load base
        lw ra, 0(\base)
        lw sp, 4(\base)
        lw s0, 8(\base)
        lw s1, 12(\base)
        lw s2, 16(\base)
        lw s3, 20(\base)
        lw s4, 24(\base)
        lw s5, 28(\base)
        lw s6, 32(\base)
        lw s7, 36(\base)
        lw s8, 40(\base)
        lw s9, 44(\base)
        lw s10, 48(\base)
        lw s11, 52(\base)
.endm

.macro reg_save base
        # save the registers.
        sw ra, 0(\base)
        sw sp, 4(\base)
        sw gp, 8(\base)
        sw tp, 12(\base)
        sw t0, 16(\base)
        sw t1, 20(\base)
        sw t2, 24(\base)
        sw s0, 28(\base)
        sw s1, 32(\base)
        sw a0, 36(\base)
        sw a1, 40(\base)
        sw a2, 44(\base)
        sw a3, 48(\base)
        sw a4, 52(\base)
        sw a5, 56(\base)
        sw a6, 60(\base)
        sw a7, 64(\base)
        sw s2, 68(\base)
        sw s3, 72(\base)
        sw s4, 76(\base)
        sw s5, 80(\base)
        sw s6, 84(\base)
        sw s7, 88(\base)
        sw s8, 92(\base)
        sw s9, 96(\base)
        sw s10, 100(\base)
        sw s11, 104(\base)
        sw t3, 108(\base)
        sw t4, 112(\base)
        sw t5, 116(\base)
        sw t6, 120(\base)
.endm

.macro reg_load base
        # restore registers.
        lw ra, 0(\base)
        lw sp, 4(\base)
        lw gp, 8(\base)
        # not this, in case we moved CPUs: lw tp, 12(\base)
        lw t0, 16(\base)
        lw t1, 20(\base)
        lw t2, 24(\base)
        lw s0, 28(\base)
        lw s1, 32(\base)
        lw a0, 36(\base)
        lw a1, 40(\base)
        lw a2, 44(\base)
        lw a3, 48(\base)
        lw a4, 52(\base)
        lw a5, 56(\base)
        lw a6, 60(\base)
        lw a7, 64(\base)
        lw s2, 68(\base)
        lw s3, 72(\base)
        lw s4, 76(\base)
        lw s5, 80(\base)
        lw s6, 84(\base)
        lw s7, 88(\base)
        lw s8, 92(\base)
        lw s9, 96(\base)
        lw s10, 100(\base)
        lw s11, 104(\base)
        lw t3, 108(\base)
        lw t4, 112(\base)
        lw t5, 116(\base)
        lw t6, 120(\base)
.endm
# ============ Macro END   ==================
 
# Context switch
#
#   void sys_switch(struct context *old, struct context *new);
# 
# Save current registers in old. Load from new.

.globl sys_switch
.align 4
sys_switch:
        ctx_save a0  # a0 => struct context *old
        ctx_load a1  # a1 => struct context *new
        ret          # pc=ra; swtch to new task (new->ra)

.globl atomic_swap
.align 4
atomic_swap:
        lw a5, 0(a0)
        li a4, 1
        sw a4, 0(a0)
        mv a0, a5
        ret

.globl trap_vector
# the trap vector base address must always be aligned on a 4-byte boundary
.align 4
trap_vector:
	# 1. Save all registers to the area pointed by mscratch
	csrrw	t6, mscratch, t6	# swap t6 and mscratch
        reg_save t6
	csrr	t5, mscratch		# original t6 was swapped into mscratch
	sw	t5, 120(t6)		# overwrite saved t6 slot with the real original t6
	csrw	mscratch, t6

	# 2. Switch to a safe, 16-byte aligned TRAP STACK
	la   sp, trap_stack_top    # <--- CRITICAL: Switch to dedicated stack

	# 3. Call the C trap handler in trap.c
	csrr	a0, mepc
	csrr	a1, mcause
	mv	a2, t6
	call	trap_handler

	# trap_handler will return the return address via a0
	csrw	mepc, a0

	# If trap_handler requested a direct return to the OS context, skip
	# restoring the app trap frame. task_os() has already loaded the OS regs.
	la	t0, trap_skip_restore
	lw	t1, 0(t0)
	beqz	t1, 1f
	sw	zero, 0(t0)
	mret

1:

	# 4. Restore context from mscratch
	csrr	t6, mscratch
	reg_load t6                # This will restore the original SP from the saved frame
	mret

# Dedicated stack for trap handling to ensure 16-byte alignment
.section .data
.align 16
trap_stack:
    .skip 4096              # 4KB trap stack
trap_stack_top:

.global vga_hw_init
vga_hw_init:
	# PCI is at 0x30000000
	# VGA is at 00:01.0, using extended control regs (4096 bytes)
	la t0, 0x30000000|(1<<15)|(0<<12)

	# Set up frame buffer
	la t1, 0x50000008
	sw t1, 0x10(t0)

	# Set up I/O
	la t2, 0x40000000
	sw t2, 0x18(t0)

	# Enable memory accesses for this device
	lw a0, 0x04(t0)
	ori a0, a0, 0x02
	sw a0, 0x04(t0)
	lw a0, 0x04(t0)

	# Set up video mode
	li t3, 0x60 # Enable LFB, enable 8-bit DAC
	sh t3, 0x508(t2)

	# Set Mode 13h by hand
	la a0, mode_13h_regs
	addi a1, t2, 0x400-0xC0
	la t3, 0xC0
1:
	lbu a3, 0(a0)
	beq a3, zero, 2f
	add a2, a1, a3
	lb a4, 1(a0)
	lbu a5, 2(a0)
	addi a0, a0, 3
	blt a3, t3, 3f
	blt a4, zero, 4f
	sb a4, 0(a2)
	sb a5, 1(a2)
	j 1b
3:
	lb zero, 0xDA(a1)
	sb a4, 0(a2)
	sb a5, 0(a2)
	j 1b
4:
	sb a5, 0(a2)
	j 1b
2:
	# Set up a palette
	li t3, 0
	sb t3, 0x408(t2)
	li t3, 0
	li t4, 256*3
	la a0, initial_palette
1:
	lb t5, 0(a0)
	sb t5, 0x409(t2)
	addi a0, a0, 1
	addi t3, t3, 1
	bltu t3, t4, 1b
	ret

.align 3
mode_13h_regs:
    .byte 0xC2, 0xFF, 0x63, 0xC4, 0x00, 0x00, 0xC0, 0x00, 0x00, 0xC0, 0x01, 0x02
    .byte 0xC0, 0x02, 0x08, 0xC0, 0x03, 0x0A, 0xC0, 0x04, 0x20, 0xC0, 0x05, 0x22
    .byte 0xC0, 0x06, 0x28, 0xC0, 0x07, 0x2A, 0xC0, 0x08, 0x15, 0xC0, 0x09, 0x17
    .byte 0xC0, 0x0A, 0x1D, 0xC0, 0x0B, 0x1F, 0xC0, 0x0C, 0x35, 0xC0, 0x0D, 0x37
    .byte 0xC0, 0x0E, 0x3D, 0xC0, 0x0F, 0x3F, 0xC0, 0x30, 0x41, 0xC0, 0x31, 0x00
    .byte 0xC0, 0x32, 0x0F, 0xC0, 0x33, 0x00, 0xC0, 0x34, 0x00, 0xCE, 0x00, 0x00
    .byte 0xCE, 0x01, 0x00, 0xCE, 0x02, 0x00, 0xCE, 0x03, 0x00, 0xCE, 0x04, 0x00
    .byte 0xCE, 0x05, 0x40, 0xCE, 0x06, 0x05, 0xCE, 0x07, 0x00, 0xCE, 0x08, 0xFF
    .byte 0xD4, 0x11, 0x0E, 0xD4, 0x00, 0x5F, 0xD4, 0x01, 0x4F, 0xD4, 0x02, 0x50
    .byte 0xD4, 0x03, 0x82, 0xD4, 0x04, 0x54, 0xD4, 0x05, 0x80, 0xD4, 0x06, 0xBF
    .byte 0xD4, 0x07, 0x1F, 0xD4, 0x08, 0x00, 0xD4, 0x09, 0x41, 0xD4, 0x0A, 0x20
    .byte 0xD4, 0x0B, 0x1F, 0xD4, 0x0C, 0x00, 0xD4, 0x0D, 0x00, 0xD4, 0x0E, 0xFF
    .byte 0xD4, 0x0F, 0xFF, 0xD4, 0x10, 0x9C, 0xD4, 0x11, 0x8E, 0xD4, 0x12, 0x8F
    .byte 0xD4, 0x13, 0x28, 0xD4, 0x14, 0x40, 0xD4, 0x15, 0x96, 0xD4, 0x16, 0xB9
    .byte 0xD4, 0x17, 0xA3, 0xC4, 0x01, 0x01, 0xC4, 0x02, 0x0F, 0xC4, 0x03, 0x00
    .byte 0xC4, 0x04, 0x0E, 0x00

initial_palette:
    .byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0x00, 0x00, 0xAA, 0x00, 0x00, 0xFF
    .byte 0x00, 0x55, 0x00, 0x00, 0x55, 0x55, 0x00, 0x55, 0xAA, 0x00, 0x55, 0xFF
    .byte 0x00, 0xAA, 0x00, 0x00, 0xAA, 0x55, 0x00, 0xAA, 0xAA, 0x00, 0xAA, 0xFF
    .byte 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x55, 0x00, 0xFF, 0xAA, 0x00, 0xFF, 0xFF
    .byte 0x55, 0x00, 0x00, 0x55, 0x00, 0x55, 0x55, 0x00, 0xAA, 0x55, 0x00, 0xFF
    .byte 0x55, 0x55, 0x00, 0x55, 0x55, 0x55, 0x55, 0x55, 0xAA, 0x55, 0x55, 0xFF
    .byte 0x55, 0xAA, 0x00, 0x55, 0xAA, 0x55, 0x55, 0xAA, 0xAA, 0x55, 0xAA, 0xFF
    .byte 0x55, 0xFF, 0x00, 0x55, 0xFF, 0x55, 0x55, 0xFF, 0xAA, 0x55, 0xFF, 0xFF
    .fill 192*3, 1, 0x3F # Fill rest with some color
