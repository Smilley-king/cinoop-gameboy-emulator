#ifndef APU_H
#define APU_H

#include <stdint.h>
#include <stdbool.h>

void apu_init(void);
void apu_step(int cycles);
uint8_t apu_read(uint16_t addr);
void apu_write(uint16_t addr, uint8_t val);
void apu_close(void);

#endif // APU_H
