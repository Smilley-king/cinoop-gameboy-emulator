#include "timer.h"
#include "mmu.h"

static uint16_t internal_counter = 0;
static uint8_t tima = 0;
static uint8_t tma = 0;
static uint8_t tac = 0;

void timer_init(void) {
    internal_counter = 0xABCC; // Initial value on DMG
    tima = 0x00;
    tma = 0x00;
    tac = 0x00;
}

void timer_step(int cycles) {
    uint16_t prev_counter = internal_counter;
    internal_counter += cycles;

    // Timer enabled? (TAC bit 2)
    if (tac & 0x04) {
        int shift = 0;
        switch (tac & 0x03) {
            case 0: shift = 10; break; // 4096 Hz (every 1024 cycles)
            case 1: shift = 4;  break; // 262144 Hz (every 16 cycles)
            case 2: shift = 6;  break; // 65536 Hz (every 64 cycles)
            case 3: shift = 8;  break; // 16384 Hz (every 256 cycles)
        }
        uint32_t step = 1 << shift;
        uint32_t ticks = (((uint32_t)prev_counter + cycles) / step) - ((uint32_t)prev_counter / step);
        while (ticks > 0) {
            tima++;
            if (tima == 0) { // Overflowed from 0xFF to 0x00
                tima = tma;
                mmu_request_interrupt(INTERRUPT_TIMER);
            }
            ticks--;
        }
    }
}

uint8_t timer_read_div(void) {
    return (internal_counter >> 8) & 0xFF;
}

void timer_reset_div(void) {
    internal_counter = 0;
}

uint8_t timer_read_tima(void) {
    return tima;
}

void timer_write_tima(uint8_t val) {
    tima = val;
}

uint8_t timer_read_tma(void) {
    return tma;
}

void timer_write_tma(uint8_t val) {
    tma = val;
}

uint8_t timer_read_tac(void) {
    return tac | 0xF8; // Upper 5 bits unused, return 1s
}

void timer_write_tac(uint8_t val) {
    tac = val & 0x07;
}
