#include "../include/common.h"

// 0x0000 - 0x3FFF : ROM Bank 0
// 0x4000 - 0x7FFF : ROM Bank 1 - Switchable
// 0x8000 - 0x97FF : CHR RAM
// 0x9800 - 0x9BFF : BG Map 1
// 0x9C00 - 0x9FFF : BG Map 2
// 0xA000 - 0xBFFF : Cartridge RAM
// 0xC000 - 0xCFFF : RAM Bank 0
// 0xD000 - 0xDFFF : RAM Bank 1-7 - switchable - Color only
// 0xE000 - 0xFDFF : Reserved - Echo RAM
// 0xFE00 - 0xFE9F : Object Attribute Memory
// 0xFEA0 - 0xFEFF : Reserved - Unusable
// 0xFF00 - 0xFF7F : I/O Registers
// 0xFF80 - 0xFFFE : Zero Page

u8 bus_read(u16 address) {
    if (address < 0x4000) {
        return cart_get_context()->rom_data[address];
    } else if (address < 0x8000) {
        return cart_get_context()->rom_bank_x[address - 0x4000];
    } else if (address < 0xA000) {
        return ppu_get_context()->vram[address - 0x8000];
    } else if (address < 0xC000) {
        return cart_read(address);
    } else if (address < 0xD000) {
        return wram_read(address);
    } else if (address < 0xE000) {
        return wram_read(address);
    } else if (address < 0xFE00) {
        return 0; // Echo RAM skip (通常回傳 0 或對應 WRAM)
    } else if (address < 0xFEA0) {
        if (dma_transferring()) return 0xFF;
        u8* oam = (u8*)ppu_get_context()->oam_ram;
        return oam[address - 0xFE00];
    } else if (address < 0xFF00) {
        return 0xFF; // Unusable reserved area (0xFEA0 - 0xFEFF)
    } else if (address < 0xFF80) {
        return io_read(address);
    } else if (address == 0xFFFF) {
        return cpu_get_ie_register();
    }
    return hram_read(address);
}

void bus_write(u16 address, u8 value) {
    if (address < 0x8000) {
        cart_write(address, value);
    } else if (address < 0xA000) {
        ppu_get_context()->vram[address - 0x8000] = value;
    } else if (address < 0xC000) {
        cart_write(address, value);
    } else if (address < 0xE000) {
        wram_write(address, value);
    } else if (address < 0xFE00) {
        // Echo RAM (0xE000 - 0xFDFF)
        // 模擬器實作通常忽略寫入，或者同步寫入 WRAM
    } else if (address < 0xFEA0) {
        // OAM (0xFE00 - 0xFE9F)
        if (dma_transferring()) return; // 修正：void 函式直接 return
        
        u8* oam = (u8*)ppu_get_context()->oam_ram;
        oam[address - 0xFE00] = value; // 修正：賦值寫入，而非 return
        
    } else if (address < 0xFF00) {
        // Unusable area (0xFEA0 - 0xFEFF) - 忽略寫入
    } else if (address < 0xFF80) {
        io_write(address, value);
    } else if (address == 0xFFFF) {
        cpu_set_ie_register(value);
    } else {
        hram_write(address, value);
    }
}

u16 bus_read16(u16 address) {
    u16 lo = bus_read(address);
    u16 hi = bus_read(address + 1);

    return lo | (hi << 8);
}

void bus_write16(u16 address, u16 value) {
    bus_write(address + 1, (value >> 8) & 0xFF);
    bus_write(address, value & 0xFF);
}