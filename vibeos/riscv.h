#ifndef __RISCV_H__
#define __RISCV_H__

#include <stdint.h>

#ifdef __riscv_xlen64
  typedef uint64_t reg_t;
#else
  typedef uint32_t reg_t;
#endif
#define PGSIZE 4096 // bytes per page

// ref: https://www.activexperts.com/serial-port-component/tutorials/uart/
#define UART 0x10000000L
#define UART_THR (volatile uint8_t *)(UART + 0x00) // THR: transmitter holding register
#define UART_RHR (volatile uint8_t *)(UART + 0x00) // RHR: receive holding register
#define UART_DLL (volatile uint8_t *)(UART + 0x00) // LSB of divisor latch (write mode)
#define UART_DLM (volatile uint8_t *)(UART + 0x01) // MSB of divisor latch (write mode)
#define UART_IER (volatile uint8_t *)(UART + 0x01) // Interrupt Enable Register
#define UART_LCR (volatile uint8_t *)(UART + 0x03) // Line Control Register
#define UART_LSR (volatile uint8_t *)(UART + 0x05) // LSR: line status register
#define UART_LSR_EMPTY_MASK 0x40                   // LSR Bit 6: transmitter empty; both THR and LSR are empty

#define UART_REGR(reg) (*(reg))
#define UART_REGW(reg, v) ((*reg) = (v))

#define UART0_IRQ 10
#define VIRTIO_IRQ 1
#define VIRTIO_IRQ2 2
#define VIRTIO_IRQ3 3
#define VIRTIO_IRQ4 4
#define VIRTIO_IRQ5 5
#define VIRTIO_IRQ6 6
#define VIRTIO_IRQ7 7
#define VIRTIO_IRQ8 8
#define VIRTIO_IRQ9 9

// Saved registers for kernel context switches
struct context
{
  reg_t ra;
  reg_t sp;

  // callee-saved
  reg_t s0;
  reg_t s1;
  reg_t s2;
  reg_t s3;
  reg_t s4;
  reg_t s5;
  reg_t s6;
  reg_t s7;
  reg_t s8;
  reg_t s9;
  reg_t s10;
  reg_t s11;
};

struct trap_frame
{
  reg_t ra;
  reg_t sp;
  reg_t gp;
  reg_t tp;
  reg_t t0;
  reg_t t1;
  reg_t t2;
  reg_t s0;
  reg_t s1;
  reg_t a0;
  reg_t a1;
  reg_t a2;
  reg_t a3;
  reg_t a4;
  reg_t a5;
  reg_t a6;
  reg_t a7;
  reg_t s2;
  reg_t s3;
  reg_t s4;
  reg_t s5;
  reg_t s6;
  reg_t s7;
  reg_t s8;
  reg_t s9;
  reg_t s10;
  reg_t s11;
  reg_t t3;
  reg_t t4;
  reg_t t5;
  reg_t t6;
  reg_t mepc;
};

#define NCPU 8 // maximum number of CPUs
#define CLINT 0x2000000
#define CLINT_MTIMECMP(hartid) (CLINT + 0x4000 + 4 * (hartid))
#define CLINT_MTIME (CLINT + 0xBFF8) // cycles since boot

static inline reg_t r_tp()
{
  reg_t x;
  asm volatile("csrr %0, tp"
               : "=r"(x));
  return x;
}

static inline reg_t r_mhartid()
{
  reg_t x;
  asm volatile("csrr %0, mhartid"
               : "=r"(x));
  return x;
}

#define MSTATUS_MPP_MASK (3L << 11) // previous mode
#define MSTATUS_MPP_M (3L << 11)
#define MSTATUS_MPP_S (1L << 11)
#define MSTATUS_MPP_U (0L << 11)
#define MSTATUS_MIE (1L << 3) // machine-mode interrupt enable

static inline reg_t r_mstatus()
{
  reg_t x;
  asm volatile("csrr %0, mstatus"
               : "=r"(x));
  return x;
}

static inline void w_mstatus(reg_t x)
{
  asm volatile("csrw mstatus, %0"
               :
               : "r"(x));
}

static inline void w_mepc(reg_t x)
{
  asm volatile("csrw mepc, %0"
               :
               : "r"(x));
}

static inline void w_satp(reg_t x)
{
  asm volatile("csrw satp, %0"
               :
               : "r"(x));
}

static inline void sfence_vma(void)
{
  asm volatile("sfence.vma zero, zero"
               :
               :
               : "memory");
}

static inline reg_t r_mepc()
{
  reg_t x;
  asm volatile("csrr %0, mepc"
               : "=r"(x));
  return x;
}

static inline reg_t r_mtval()
{
  reg_t x;
  asm volatile("csrr %0, mtval"
               : "=r"(x));
  return x;
}

static inline void w_mscratch(reg_t x)
{
  asm volatile("csrw mscratch, %0"
               :
               : "r"(x));
}

static inline void w_mtvec(reg_t x)
{
  asm volatile("csrw mtvec, %0"
               :
               : "r"(x));
}

static inline void w_pmpcfg0(reg_t x)
{
  asm volatile("csrw pmpcfg0, %0"
               :
               : "r"(x));
}

static inline void w_pmpaddr0(reg_t x)
{
  asm volatile("csrw pmpaddr0, %0"
               :
               : "r"(x));
}

static inline void w_pmpaddr1(reg_t x)
{
  asm volatile("csrw pmpaddr1, %0"
               :
               : "r"(x));
}

static inline void w_pmpaddr2(reg_t x)
{
  asm volatile("csrw pmpaddr2, %0"
               :
               : "r"(x));
}

static inline void w_pmpaddr3(reg_t x)
{
  asm volatile("csrw pmpaddr3, %0"
               :
               : "r"(x));
}

#define MIE_MEIE (1L << 11) // external
#define MIE_MTIE (1L << 7)  // timer
#define MIE_MSIE (1L << 3)  // software

static inline reg_t r_mie()
{
  reg_t x;
  asm volatile("csrr %0, mie"
               : "=r"(x));
  return x;
}

static inline void w_mie(reg_t x)
{
  asm volatile("csrw mie, %0"
               :
               : "r"(x));
}

#endif
/* MMU Support */
#define PTE_V (1L << 0)
#define PTE_R (1L << 1)
#define PTE_W (1L << 2)
#define PTE_X (1L << 3)
#define PTE_U (1L << 4)
#define PTE_A (1L << 6)
#define PTE_D (1L << 7)
#define SATP_MODE_SV32 (1L << 31)
#ifndef REG_T_DEFINED
typedef uint32_t reg_t;
#define REG_T_DEFINED
#endif
