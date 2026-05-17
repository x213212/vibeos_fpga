#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdint.h>

// 固定寬度整數型態
typedef uint8_t  u8_t;
typedef int8_t   s8_t;
typedef uint16_t u16_t;
typedef int16_t  s16_t;
typedef uint32_t u32_t;
typedef int32_t  s32_t;
#define MSTATUS_MIE (1 << 3)

// 用於中斷保護的型態
typedef unsigned int sys_prot_t;

// 這裡用你的中斷使能/禁止函式實作中斷保護
static inline sys_prot_t sys_arch_protect(void) {
    // 禁用機器模式中斷 (例如清除 MIE)
    unsigned int mstatus = r_mstatus();
    w_mstatus(mstatus & ~MSTATUS_MIE);
    return mstatus; // 返回舊狀態，用於恢復
}

static inline void sys_arch_unprotect(sys_prot_t state) {
    // 恢復之前中斷狀態
    w_mstatus(state);
}

// 這裡定義空宏對應 lwIP 的保護函式（可改成更細節的）
#define SYS_ARCH_DECL_PROTECT(level)  sys_prot_t level
#define SYS_ARCH_PROTECT(level)       (level = sys_arch_protect())
#define SYS_ARCH_UNPROTECT(level)     sys_arch_unprotect(level)

// 字節序
#define LWIP_PLATFORM_BYTESWAP 0

#endif /* LWIP_ARCH_CC_H */
