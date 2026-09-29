#ifndef JOYPAD_H
#define JOYPAD_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    JOYPAD_RIGHT,
    JOYPAD_LEFT,
    JOYPAD_UP,
    JOYPAD_DOWN,
    JOYPAD_A,
    JOYPAD_B,
    JOYPAD_SELECT,
    JOYPAD_START,
    JOYPAD_BUTTON_COUNT
} JoypadButton;

void joypad_init(void);
void joypad_press(JoypadButton button);
void joypad_release(JoypadButton button);
uint8_t joypad_read(void);
void joypad_write(uint8_t val);

#endif // JOYPAD_H
