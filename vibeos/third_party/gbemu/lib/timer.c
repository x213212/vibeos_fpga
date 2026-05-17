#include "../include/common.h"

static timer_context ctx = {0};
u16 timer_div = 0; // 暴露給外部，用於內聯

timer_context *timer_get_context() {
    return &ctx;
}

void gb_timer_init() {
    ctx.div = 0xAC00;
    timer_div = 0xAC00;
}

// 供外部內聯呼叫的複雜邏輯
void timer_tick_complex() {
    ctx.div = timer_div; // 同步狀態
    bool timer_update = false;
    u16 prev_div = ctx.div - 1;

    switch(ctx.tac & (0b11)) {
        case 0b00: timer_update = (prev_div & (1 << 9)) && (!(ctx.div & (1 << 9))); break;
        case 0b01: timer_update = (prev_div & (1 << 3)) && (!(ctx.div & (1 << 3))); break;
        case 0b10: timer_update = (prev_div & (1 << 5)) && (!(ctx.div & (1 << 5))); break;
        case 0b11: timer_update = (prev_div & (1 << 7)) && (!(ctx.div & (1 << 7))); break;
    }

    if (timer_update && (ctx.tac & (1 << 2))) {
        ctx.tima++;
        if (ctx.tima == 0xFF) {
            ctx.tima = ctx.tma;
            cpu_request_interrupt(IT_TIMER);
        }
    }
}

void timer_tick() {
    timer_div++;
    timer_tick_complex();
}

void timer_write(u16 address, u8 value) {
    switch(address) {
        case 0xFF04:
            ctx.div = 0;
            timer_div = 0;
            break;
        case 0xFF05: ctx.tima = value; break;
        case 0xFF06: ctx.tma = value; break;
        case 0xFF07: ctx.tac = value; break;
    }
}

u8 timer_read(u16 address) {
    ctx.div = timer_div; // 讀取前同步
    switch(address) {
        case 0xFF04: return ctx.div >> 8;
        case 0xFF05: return ctx.tima;
        case 0xFF06: return ctx.tma;
        case 0xFF07: return ctx.tac;
        default: return 0xFF;
    }
}
