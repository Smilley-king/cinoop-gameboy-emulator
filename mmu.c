#include "mmu.h"
#include "timer.h"
#include "ppu.h"
#include "joypad.h"
#include "apu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *rom_data = NULL;
size_t rom_size = 0;

static uint8_t vram[8192];
static uint8_t sram[32768];
static uint8_t wram[8192];
static uint8_t oam[160];
static uint8_t hram[128];
static uint8_t if_reg = 0xE1;
static uint8_t ie_reg = 0x00;

// MBC1 state
static uint8_t cartridge_type = 0;
static bool ram_enabled = false;
static uint8_t rom_bank = 1;
static uint8_t ram_bank = 0;
static uint8_t banking_mode = 0;

void mmu_init(void) {
    memset(vram, 0, sizeof(vram));
    memset(sram, 0, sizeof(sram));
    memset(wram, 0, sizeof(wram));
    memset(oam, 0, sizeof(oam));
    memset(hram, 0, sizeof(hram));
    if_reg = 0xE1;
    ie_reg = 0x00;
    ram_enabled = false;
    rom_bank = 1;
    ram_bank = 0;
    banking_mode = 0;
}

bool mmu_load_rom(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        printf("Error: Could not open ROM file '%s'\n", filename);
        return false;
    }

    fseek(f, 0, SEEK_END);
    rom_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (rom_size < 0x8000) {
        rom_size = 0x8000; // Pad to at least 32KB
    }

    if (rom_data) free(rom_data);
    rom_data = (uint8_t *)calloc(1, rom_size);
    if (!rom_data) {
        printf("Error: Memory allocation failed for ROM\n");
        fclose(f);
        return false;
    }

    size_t read_bytes = fread(rom_data, 1, rom_size, f);
    fclose(f);

    cartridge_type = rom_data[0x0147];
    printf("Loaded ROM: %zu bytes read, Cartridge Type: 0x%02X\n", read_bytes, cartridge_type);
    return true;
}

void mmu_request_interrupt(uint8_t flag) {
    if_reg |= flag;
}

static void dma_transfer(uint8_t base) {
    uint16_t src = ((uint16_t)base) << 8;
    for (int i = 0; i < 160; i++) {
        oam[i] = mmu_read(src + i);
    }
}

uint8_t mmu_read(uint16_t addr) {
    // ROM Bank 0 (0x0000 - 0x3FFF)
    if (addr < 0x4000) {
        if (!rom_data) return 0xFF;
        return rom_data[addr % rom_size];
    }

    // ROM Bank 1..N (0x4000 - 0x7FFF)
    if (addr < 0x8000) {
        if (!rom_data) return 0xFF;
        uint32_t bank = (rom_bank == 0) ? 1 : rom_bank;
        uint32_t offset = (bank * 0x4000) + (addr - 0x4000);
        return rom_data[offset % rom_size];
    }

    // VRAM (0x8000 - 0x9FFF)
    if (addr < 0xA000) {
        return vram[addr - 0x8000];
    }

    // External RAM (0xA000 - 0xBFFF)
    if (addr < 0xC000) {
        if (!ram_enabled) return 0xFF;
        uint32_t offset = (ram_bank * 0x2000) + (addr - 0xA000);
        return sram[offset % sizeof(sram)];
    }

    // Work RAM (0xC000 - 0xDFFF)
    if (addr < 0xE000) {
        return wram[addr - 0xC000];
    }

    // Echo RAM (0xE000 - 0xFDFF)
    if (addr < 0xFE00) {
        return wram[addr - 0xE000];
    }

    // OAM (0xFE00 - 0xFE9F)
    if (addr < 0xFEA0) {
        return oam[addr - 0xFE00];
    }

    // Not Usable (0xFEA0 - 0xFEFF)
    if (addr < 0xFF00) {
        return 0xFF;
    }

    // I/O Registers (0xFF00 - 0xFF7F)
    if (addr < 0xFF80) {
        if (addr == 0xFF00) return joypad_read();
        if (addr == 0xFF04) return timer_read_div();
        if (addr == 0xFF05) return timer_read_tima();
        if (addr == 0xFF06) return timer_read_tma();
        if (addr == 0xFF07) return timer_read_tac();
        if (addr == 0xFF0F) return if_reg | 0xE0;
        if (addr >= 0xFF10 && addr <= 0xFF3F) return apu_read(addr);
        if (addr >= 0xFF40 && addr <= 0xFF4B) return ppu_read(addr);
        if (addr == 0xFF4D) return 0xFF; // Speed switch (DMG)
        return 0xFF;
    }

    // High RAM (0xFF80 - 0xFFFE)
    if (addr < 0xFFFF) {
        return hram[addr - 0xFF80];
    }

    // Interrupt Enable (0xFFFF)
    return ie_reg;
}

void mmu_write(uint16_t addr, uint8_t val) {
    // ROM Write (Cartridge MBC control)
    if (addr < 0x8000) {
        // MBC1 handling
        if (cartridge_type >= 0x01 && cartridge_type <= 0x03) {
            if (addr < 0x2000) {
                // RAM Enable
                ram_enabled = ((val & 0x0F) == 0x0A);
            } else if (addr < 0x4000) {
                // ROM Bank lower 5 bits
                uint8_t lower = val & 0x1F;
                if (lower == 0) lower = 1;
                rom_bank = (rom_bank & 0x60) | lower;
            } else if (addr < 0x6000) {
                // RAM Bank or ROM Bank upper 2 bits
                if (banking_mode == 1) {
                    ram_bank = val & 0x03;
                } else {
                    rom_bank = (rom_bank & 0x1F) | ((val & 0x03) << 5);
                }
            } else {
                // Banking mode select
                banking_mode = val & 0x01;
            }
        }
        return;
    }

    // VRAM (0x8000 - 0x9FFF)
    if (addr < 0xA000) {
        vram[addr - 0x8000] = val;
        return;
    }

    // External RAM (0xA000 - 0xBFFF)
    if (addr < 0xC000) {
        if (ram_enabled) {
            uint32_t offset = (ram_bank * 0x2000) + (addr - 0xA000);
            sram[offset % sizeof(sram)] = val;
        }
        return;
    }

    // Work RAM (0xC000 - 0xDFFF)
    if (addr < 0xE000) {
        wram[addr - 0xC000] = val;
        return;
    }

    // Echo RAM (0xE000 - 0xFDFF)
    if (addr < 0xFE00) {
        wram[addr - 0xE000] = val;
        return;
    }

    // OAM (0xFE00 - 0xFE9F)
    if (addr < 0xFEA0) {
        oam[addr - 0xFE00] = val;
        return;
    }

    // Not Usable (0xFEA0 - 0xFEFF)
    if (addr < 0xFF00) {
        return;
    }

    // I/O Registers (0xFF00 - 0xFF7F)
    if (addr < 0xFF80) {
        if (addr == 0xFF00) { joypad_write(val); return; }
        if (addr == 0xFF01) { return; } // SB (Serial byte)
        if (addr == 0xFF02) { return; } // SC (Serial control)
        if (addr == 0xFF04) { timer_reset_div(); return; }
        if (addr == 0xFF05) { timer_write_tima(val); return; }
        if (addr == 0xFF06) { timer_write_tma(val); return; }
        if (addr == 0xFF07) { timer_write_tac(val); return; }
        if (addr == 0xFF0F) { if_reg = val & 0x1F; return; }
        if (addr >= 0xFF10 && addr <= 0xFF3F) { apu_write(addr, val); return; }
        if (addr == 0xFF46) { dma_transfer(val); return; }
        if (addr >= 0xFF40 && addr <= 0xFF4B) { ppu_write(addr, val); return; }
        return;
    }

    // High RAM (0xFF80 - 0xFFFE)
    if (addr < 0xFFFF) {
        hram[addr - 0xFF80] = val;
        return;
    }

    // Interrupt Enable (0xFFFF)
    ie_reg = val;
}
