#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdint.h>
#include "riscv.h"
// Fixed-width integer types
typedef uint8_t  u8_t;
typedef int8_t   s8_t;
typedef uint16_t u16_t;
typedef int16_t  s16_t;
typedef uint32_t u32_t;
typedef int32_t  s32_t;
#define MSTATUS_MIE (1 << 3)

// Type used for interrupt protection
typedef unsigned int sys_prot_t;

// Implement interrupt protection using your own enable/disable interrupt functions
static inline sys_prot_t sys_arch_protect(void) {
    // Disable machine mode interrupts (e.g., clear MIE)
    unsigned int mstatus = r_mstatus();
    w_mstatus(mstatus & ~MSTATUS_MIE);
    return mstatus; // Return old status for restoration
}

static inline void sys_arch_unprotect(sys_prot_t state) {
    // Restore previous interrupt status
    w_mstatus(state);
}

// Define empty macros for lwIP protection functions (customize for finer granularity if needed)
#define SYS_ARCH_DECL_PROTECT(level)  sys_prot_t level
#define SYS_ARCH_PROTECT(level)       (level = sys_arch_protect())
#define SYS_ARCH_UNPROTECT(level)     sys_arch_unprotect(level)

// Byte order
#define LWIP_PLATFORM_BYTESWAP 0

#endif /* LWIP_ARCH_CC_H */
