#include "../include/common.h"

extern cpu_context ctx;

void fetch_data()
{
    // 1. 提取為局部指標，減少全域結構體的重複解引用
    instruction *inst = ctx.cur_inst;
    
    // 將預設狀態重置
    ctx.mem_dest = 0;
    ctx.dest_is_mem = false;

    // 早期返回 (Early return)
    if (!inst) {
        return;
    }

    // 2. 將 pc 讀入局部變數，利用 CPU 暫存器加速運算
    u16 pc = ctx.regs.pc;

    switch (inst->mode)
    {
    case AM_IMP:
        return;

    case AM_R:
        ctx.fetched_data = cpu_read_reg(inst->reg_1);
        return;

    case AM_R_R:
        ctx.fetched_data = cpu_read_reg(inst->reg_2);
        return;

    case AM_R_D8:
        ctx.fetched_data = bus_read(pc);
        emu_cycles(1);
        ctx.regs.pc = pc + 1; // 寫回
        return;

    case AM_R_D16:
    case AM_D16:
    {
        // 利用 pc++ 讓語法更簡潔，且能循序讀取
        u16 lo = bus_read(pc++);
        emu_cycles(1);

        u16 hi = bus_read(pc++);
        emu_cycles(1);

        ctx.fetched_data = lo | (hi << 8);
        ctx.regs.pc = pc; // 更新寫回
        return;
    }

    case AM_MR_R:
        ctx.fetched_data = cpu_read_reg(inst->reg_2);
        ctx.mem_dest = cpu_read_reg(inst->reg_1);
        ctx.dest_is_mem = true;

        if (inst->reg_1 == RT_C) {
            ctx.mem_dest |= 0xFF00;
        }
        return;

    case AM_R_MR:
    {
        u16 addr = cpu_read_reg(inst->reg_2);
        if (inst->reg_2 == RT_C) {
            addr |= 0xFF00;
        }

        ctx.fetched_data = bus_read(addr);
        emu_cycles(1);
        return;
    }

    case AM_R_HLI:
        ctx.fetched_data = bus_read(cpu_read_reg(inst->reg_2));
        emu_cycles(1);
        cpu_set_reg(RT_HL, cpu_read_reg(RT_HL) + 1);
        return;

    case AM_R_HLD:
        ctx.fetched_data = bus_read(cpu_read_reg(inst->reg_2));
        emu_cycles(1);
        cpu_set_reg(RT_HL, cpu_read_reg(RT_HL) - 1);
        return;

    case AM_HLI_R:
        ctx.fetched_data = cpu_read_reg(inst->reg_2);
        ctx.mem_dest = cpu_read_reg(inst->reg_1);
        ctx.dest_is_mem = true;
        cpu_set_reg(RT_HL, cpu_read_reg(RT_HL) + 1);
        return;

    case AM_HLD_R:
        ctx.fetched_data = cpu_read_reg(inst->reg_2);
        ctx.mem_dest = cpu_read_reg(inst->reg_1);
        ctx.dest_is_mem = true;
        cpu_set_reg(RT_HL, cpu_read_reg(RT_HL) - 1);
        return;

    case AM_R_A8:
        ctx.fetched_data = bus_read(pc);
        emu_cycles(1);
        ctx.regs.pc = pc + 1;
        return;

    case AM_A8_R:
        ctx.mem_dest = bus_read(pc) | 0xFF00;
        ctx.dest_is_mem = true;
        emu_cycles(1);
        ctx.regs.pc = pc + 1;
        return;

    case AM_HL_SPR:
    case AM_D8:
        ctx.fetched_data = bus_read(pc);
        emu_cycles(1);
        ctx.regs.pc = pc + 1;
        return;

    case AM_A16_R:
    case AM_D16_R:
    {
        u16 lo = bus_read(pc++);
        emu_cycles(1);

        u16 hi = bus_read(pc++);
        emu_cycles(1);

        ctx.mem_dest = lo | (hi << 8);
        ctx.dest_is_mem = true;
        ctx.regs.pc = pc; // 更新寫回
        
        ctx.fetched_data = cpu_read_reg(inst->reg_2);
        return;
    }

    case AM_MR_D8:
        ctx.fetched_data = bus_read(pc);
        emu_cycles(1);
        ctx.regs.pc = pc + 1;
        
        ctx.mem_dest = cpu_read_reg(inst->reg_1);
        ctx.dest_is_mem = true;
        return;

    case AM_MR:
        ctx.mem_dest = cpu_read_reg(inst->reg_1);
        ctx.dest_is_mem = true;
        ctx.fetched_data = bus_read(ctx.mem_dest); // 複用已經算好的 ctx.mem_dest，省去一次 cpu_read_reg
        emu_cycles(1);
        return;

    case AM_R_A16:
    {
        u16 lo = bus_read(pc++);
        emu_cycles(1);

        u16 hi = bus_read(pc++);
        emu_cycles(1);

        u16 addr = lo | (hi << 8);
        ctx.regs.pc = pc; // 更新寫回

        ctx.fetched_data = bus_read(addr);
        emu_cycles(1);
        return;
    }

    default:
        printf("Unknown Addressing Mode! %d (%02X)\n", inst->mode, ctx.cur_opcode);
        exit(-7);
        return;
    }
}