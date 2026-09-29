#ifndef CPU_H
#define CPU_H

#include <stdint.h>
#include <stdbool.h>

struct cpu {
    uint8_t a, f;
    uint8_t b, c;
    uint8_t d, e;
    uint8_t h, l;
    uint16_t sp;
    uint16_t pc;

    bool ime;           // Interrupt Master Enable
    bool ime_scheduled; // EI delay (enables IME after the next instruction)
    bool halted;        // HALT state
};

extern struct cpu cpu;

#define FLAG_Z 0x80
#define FLAG_N 0x40
#define FLAG_H 0x20
#define FLAG_C 0x10

void cpu_init(void);
int cpu_step(void);

#endif // CPU_H