#ifndef PPU_H
#define PPU_H

#include <stdint.h>
#include <stdbool.h>

#define SCREEN_WIDTH  160
#define SCREEN_HEIGHT 144

void ppu_init(void);
void ppu_step(int cycles);
uint8_t ppu_read(uint16_t addr);
void ppu_write(uint16_t addr, uint8_t val);

bool ppu_is_frame_ready(void);
void ppu_clear_frame_ready(void);
const uint32_t *ppu_get_framebuffer(void);

#endif // PPU_H
