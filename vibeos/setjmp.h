#ifndef _SETJMP_H
#define _SETJMP_H

// 基於 RISC-V 32-bit (rv32) 的暫存器結構
typedef long jmp_buf[32]; 

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#endif
