#include "joypad.h"
#include "mmu.h"

static uint8_t joypad_select = 0x30; // Bits 4 & 5 default to 1 (deselected)
static bool button_states[JOYPAD_BUTTON_COUNT];

void joypad_init(void) {
    joypad_select = 0x30;
    for (int i = 0; i < JOYPAD_BUTTON_COUNT; i++) {
        button_states[i] = false;
    }
}

void joypad_press(JoypadButton button) {
    if (button < JOYPAD_BUTTON_COUNT) {
        button_states[button] = true;
        mmu_request_interrupt(INTERRUPT_JOYPAD);
    }
}

void joypad_release(JoypadButton button) {
    if (button < JOYPAD_BUTTON_COUNT) {
        button_states[button] = false;
    }
}

uint8_t joypad_read(void) {
    uint8_t res = 0xCF; // Bits 7-6 are 1; bits 3-0 default to 1 (not pressed)
    
    // Direction keys selected (bit 4 is 0)
    if (!(joypad_select & 0x10)) {
        if (button_states[JOYPAD_RIGHT]) res &= ~0x01;
        if (button_states[JOYPAD_LEFT])  res &= ~0x02;
        if (button_states[JOYPAD_UP])    res &= ~0x04;
        if (button_states[JOYPAD_DOWN])  res &= ~0x08;
    }
    
    // Action keys selected (bit 5 is 0)
    if (!(joypad_select & 0x20)) {
        if (button_states[JOYPAD_A])      res &= ~0x01;
        if (button_states[JOYPAD_B])      res &= ~0x02;
        if (button_states[JOYPAD_SELECT]) res &= ~0x04;
        if (button_states[JOYPAD_START])  res &= ~0x08;
    }

    res |= (joypad_select & 0x30);
    return res;
}

void joypad_write(uint8_t val) {
    joypad_select = val & 0x30;
}
