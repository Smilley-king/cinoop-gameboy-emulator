#ifndef MMU_H
#define MMU_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define INTERRUPT_VBLANK   (1 << 0)
#define INTERRUPT_LCD_STAT (1 << 1)
#define INTERRUPT_TIMER    (1 << 2)
#define INTERRUPT_SERIAL   (1 << 3)
#define INTERRUPT_JOYPAD   (1 << 4)

void mmu_init(void);
bool mmu_load_rom(const char *filename);
uint8_t mmu_read(uint16_t addr);
void mmu_write(uint16_t addr, uint8_t val);
void mmu_request_interrupt(uint8_t flag);

extern uint8_t *rom_data;
extern size_t rom_size;

#endif // MMU_H
