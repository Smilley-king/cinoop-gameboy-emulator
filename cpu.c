#include "cpu.h"
#include "mmu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cpu cpu;

static inline uint16_t get_af(void) { return ((uint16_t)cpu.a << 8) | (cpu.f & 0xF0); }
static inline void set_af(uint16_t val) { cpu.a = (val >> 8) & 0xFF; cpu.f = val & 0xF0; }
static inline uint16_t get_bc(void) { return ((uint16_t)cpu.b << 8) | cpu.c; }
static inline void set_bc(uint16_t val) { cpu.b = (val >> 8) & 0xFF; cpu.c = val & 0xFF; }
static inline uint16_t get_de(void) { return ((uint16_t)cpu.d << 8) | cpu.e; }
static inline void set_de(uint16_t val) { cpu.d = (val >> 8) & 0xFF; cpu.e = val & 0xFF; }
static inline uint16_t get_hl(void) { return ((uint16_t)cpu.h << 8) | cpu.l; }
static inline void set_hl(uint16_t val) { cpu.h = (val >> 8) & 0xFF; cpu.l = val & 0xFF; }

static inline void set_flag(uint8_t flag, bool cond) {
    if (cond) cpu.f |= flag;
    else cpu.f &= ~flag;
}

static inline bool get_flag(uint8_t flag) {
    return (cpu.f & flag) != 0;
}

static inline void push16(uint16_t val) {
    cpu.sp--;
    mmu_write(cpu.sp, (val >> 8) & 0xFF);
    cpu.sp--;
    mmu_write(cpu.sp, val & 0xFF);
}

static inline uint16_t pop16(void) {
    uint8_t lo = mmu_read(cpu.sp++);
    uint8_t hi = mmu_read(cpu.sp++);
    return ((uint16_t)hi << 8) | lo;
}

static inline uint8_t fetch8(void) {
    return mmu_read(cpu.pc++);
}

static inline uint16_t fetch16(void) {
    uint8_t lo = mmu_read(cpu.pc++);
    uint8_t hi = mmu_read(cpu.pc++);
    return ((uint16_t)hi << 8) | lo;
}

// 8-bit ALU operations
static void alu_add(uint8_t val) {
    uint32_t res = (uint32_t)cpu.a + val;
    set_flag(FLAG_Z, (res & 0xFF) == 0);
    set_flag(FLAG_N, false);
    set_flag(FLAG_H, ((cpu.a & 0x0F) + (val & 0x0F)) > 0x0F);
    set_flag(FLAG_C, res > 0xFF);
    cpu.a = res & 0xFF;
}

static void alu_adc(uint8_t val) {
    uint8_t c = get_flag(FLAG_C) ? 1 : 0;
    uint32_t res = (uint32_t)cpu.a + val + c;
    set_flag(FLAG_Z, (res & 0xFF) == 0);
    set_flag(FLAG_N, false);
    set_flag(FLAG_H, ((cpu.a & 0x0F) + (val & 0x0F) + c) > 0x0F);
    set_flag(FLAG_C, res > 0xFF);
    cpu.a = res & 0xFF;
}

static void alu_sub(uint8_t val) {
    set_flag(FLAG_Z, cpu.a == val);
    set_flag(FLAG_N, true);
    set_flag(FLAG_H, (cpu.a & 0x0F) < (val & 0x0F));
    set_flag(FLAG_C, cpu.a < val);
    cpu.a -= val;
}

static void alu_sbc(uint8_t val) {
    uint8_t c = get_flag(FLAG_C) ? 1 : 0;
    int32_t res = (int32_t)cpu.a - val - c;
    set_flag(FLAG_Z, (res & 0xFF) == 0);
    set_flag(FLAG_N, true);
    set_flag(FLAG_H, (cpu.a & 0x0F) < ((val & 0x0F) + c));
    set_flag(FLAG_C, res < 0);
    cpu.a = res & 0xFF;
}

static void alu_and(uint8_t val) {
    cpu.a &= val;
    set_flag(FLAG_Z, cpu.a == 0);
    set_flag(FLAG_N, false);
    set_flag(FLAG_H, true);
    set_flag(FLAG_C, false);
}

static void alu_xor(uint8_t val) {
    cpu.a ^= val;
    set_flag(FLAG_Z, cpu.a == 0);
    set_flag(FLAG_N, false);
    set_flag(FLAG_H, false);
    set_flag(FLAG_C, false);
}

static void alu_or(uint8_t val) {
    cpu.a |= val;
    set_flag(FLAG_Z, cpu.a == 0);
    set_flag(FLAG_N, false);
    set_flag(FLAG_H, false);
    set_flag(FLAG_C, false);
}

static void alu_cp(uint8_t val) {
    set_flag(FLAG_Z, cpu.a == val);
    set_flag(FLAG_N, true);
    set_flag(FLAG_H, (cpu.a & 0x0F) < (val & 0x0F));
    set_flag(FLAG_C, cpu.a < val);
}

static uint8_t alu_inc(uint8_t val) {
    uint8_t res = val + 1;
    set_flag(FLAG_Z, res == 0);
    set_flag(FLAG_N, false);
    set_flag(FLAG_H, (val & 0x0F) == 0x0F);
    return res;
}

static uint8_t alu_dec(uint8_t val) {
    uint8_t res = val - 1;
    set_flag(FLAG_Z, res == 0);
    set_flag(FLAG_N, true);
    set_flag(FLAG_H, (val & 0x0F) == 0);
    return res;
}

static void alu_daa(void) {
    uint8_t a = cpu.a;
    if (!get_flag(FLAG_N)) {
        if (get_flag(FLAG_C) || a > 0x99) {
            a += 0x60;
            set_flag(FLAG_C, true);
        }
        if (get_flag(FLAG_H) || (a & 0x0F) > 0x09) {
            a += 0x06;
        }
    } else {
        if (get_flag(FLAG_C)) {
            a -= 0x60;
        }
        if (get_flag(FLAG_H)) {
            a -= 0x06;
        }
    }
    set_flag(FLAG_Z, a == 0);
    set_flag(FLAG_H, false);
    cpu.a = a;
}

// 16-bit ALU operations
static void add_hl(uint16_t val) {
    uint16_t hl = get_hl();
    uint32_t res = (uint32_t)hl + val;
    set_flag(FLAG_N, false);
    set_flag(FLAG_H, ((hl & 0x0FFF) + (val & 0x0FFF)) > 0x0FFF);
    set_flag(FLAG_C, res > 0xFFFF);
    set_hl(res & 0xFFFF);
}

static void add_sp(int8_t val) {
    uint16_t sp = cpu.sp;
    uint32_t res = sp + val;
    set_flag(FLAG_Z, false);
    set_flag(FLAG_N, false);
    set_flag(FLAG_H, ((sp & 0x0F) + ((uint8_t)val & 0x0F)) > 0x0F);
    set_flag(FLAG_C, ((sp & 0xFF) + ((uint8_t)val & 0xFF)) > 0xFF);
    cpu.sp = res & 0xFFFF;
}

static void ld_hl_sp(int8_t val) {
    uint16_t sp = cpu.sp;
    uint32_t res = sp + val;
    set_flag(FLAG_Z, false);
    set_flag(FLAG_N, false);
    set_flag(FLAG_H, ((sp & 0x0F) + ((uint8_t)val & 0x0F)) > 0x0F);
    set_flag(FLAG_C, ((sp & 0xFF) + ((uint8_t)val & 0xFF)) > 0xFF);
    set_hl(res & 0xFFFF);
}

// Register access helpers by 3-bit code (B, C, D, E, H, L, (HL), A)
static uint8_t cb_get_reg(uint8_t r) {
    switch (r) {
        case 0: return cpu.b;
        case 1: return cpu.c;
        case 2: return cpu.d;
        case 3: return cpu.e;
        case 4: return cpu.h;
        case 5: return cpu.l;
        case 6: return mmu_read(get_hl());
        case 7: return cpu.a;
        default: return 0;
    }
}

static void cb_set_reg(uint8_t r, uint8_t val) {
    switch (r) {
        case 0: cpu.b = val; break;
        case 1: cpu.c = val; break;
        case 2: cpu.d = val; break;
        case 3: cpu.e = val; break;
        case 4: cpu.h = val; break;
        case 5: cpu.l = val; break;
        case 6: mmu_write(get_hl(), val); break;
        case 7: cpu.a = val; break;
    }
}

static int execute_cb(void) {
    uint8_t cb_op = fetch8();
    uint8_t reg_idx = cb_op & 0x07;
    uint8_t bit = (cb_op >> 3) & 0x07;
    uint8_t val = cb_get_reg(reg_idx);
    int cycles = (reg_idx == 6) ? 16 : 8;

    if (cb_op < 0x40) {
        switch (cb_op >> 3) {
            case 0: // RLC
                {
                    uint8_t c = (val >> 7) & 1;
                    val = (val << 1) | c;
                    set_flag(FLAG_Z, val == 0);
                    set_flag(FLAG_N, false);
                    set_flag(FLAG_H, false);
                    set_flag(FLAG_C, c != 0);
                }
                break;
            case 1: // RRC
                {
                    uint8_t c = val & 1;
                    val = (val >> 1) | (c << 7);
                    set_flag(FLAG_Z, val == 0);
                    set_flag(FLAG_N, false);
                    set_flag(FLAG_H, false);
                    set_flag(FLAG_C, c != 0);
                }
                break;
            case 2: // RL
                {
                    uint8_t old_c = get_flag(FLAG_C) ? 1 : 0;
                    uint8_t new_c = (val >> 7) & 1;
                    val = (val << 1) | old_c;
                    set_flag(FLAG_Z, val == 0);
                    set_flag(FLAG_N, false);
                    set_flag(FLAG_H, false);
                    set_flag(FLAG_C, new_c != 0);
                }
                break;
            case 3: // RR
                {
                    uint8_t old_c = get_flag(FLAG_C) ? 1 : 0;
                    uint8_t new_c = val & 1;
                    val = (val >> 1) | (old_c << 7);
                    set_flag(FLAG_Z, val == 0);
                    set_flag(FLAG_N, false);
                    set_flag(FLAG_H, false);
                    set_flag(FLAG_C, new_c != 0);
                }
                break;
            case 4: // SLA
                {
                    uint8_t c = (val >> 7) & 1;
                    val = val << 1;
                    set_flag(FLAG_Z, val == 0);
                    set_flag(FLAG_N, false);
                    set_flag(FLAG_H, false);
                    set_flag(FLAG_C, c != 0);
                }
                break;
            case 5: // SRA
                {
                    uint8_t c = val & 1;
                    val = (int8_t)val >> 1;
                    set_flag(FLAG_Z, val == 0);
                    set_flag(FLAG_N, false);
                    set_flag(FLAG_H, false);
                    set_flag(FLAG_C, c != 0);
                }
                break;
            case 6: // SWAP
                val = ((val & 0x0F) << 4) | ((val & 0xF0) >> 4);
                set_flag(FLAG_Z, val == 0);
                set_flag(FLAG_N, false);
                set_flag(FLAG_H, false);
                set_flag(FLAG_C, false);
                break;
            case 7: // SRL
                {
                    uint8_t c = val & 1;
                    val = val >> 1;
                    set_flag(FLAG_Z, val == 0);
                    set_flag(FLAG_N, false);
                    set_flag(FLAG_H, false);
                    set_flag(FLAG_C, c != 0);
                }
                break;
        }
        cb_set_reg(reg_idx, val);
    } else if (cb_op < 0x80) {
        // BIT b, r
        set_flag(FLAG_Z, !(val & (1 << bit)));
        set_flag(FLAG_N, false);
        set_flag(FLAG_H, true);
        if (reg_idx == 6) cycles = 12;
    } else if (cb_op < 0xC0) {
        // RES b, r
        val &= ~(1 << bit);
        cb_set_reg(reg_idx, val);
    } else {
        // SET b, r
        val |= (1 << bit);
        cb_set_reg(reg_idx, val);
    }
    return cycles;
}

void cpu_init(void) {
    memset(&cpu, 0, sizeof(cpu));
    cpu.pc = 0x0100;
    cpu.sp = 0xFFFE;
    cpu.a = 0x01;
    cpu.f = 0xB0;
    cpu.b = 0x00;
    cpu.c = 0x13;
    cpu.d = 0x00;
    cpu.e = 0xD8;
    cpu.h = 0x01;
    cpu.l = 0x4D;
    cpu.ime = false;
    cpu.ime_scheduled = false;
    cpu.halted = false;
}

int cpu_step(void) {
    uint8_t ie = mmu_read(0xFFFF);
    uint8_t if_val = mmu_read(0xFF0F);
    uint8_t pending = ie & if_val & 0x1F;

    if (cpu.halted) {
        if (pending) {
            cpu.halted = false;
        } else {
            return 4;
        }
    }

    if (cpu.ime && pending) {
        cpu.ime = false;
        push16(cpu.pc);
        if (pending & INTERRUPT_VBLANK) {
            mmu_write(0xFF0F, if_val & ~INTERRUPT_VBLANK);
            cpu.pc = 0x0040;
        } else if (pending & INTERRUPT_LCD_STAT) {
            mmu_write(0xFF0F, if_val & ~INTERRUPT_LCD_STAT);
            cpu.pc = 0x0048;
        } else if (pending & INTERRUPT_TIMER) {
            mmu_write(0xFF0F, if_val & ~INTERRUPT_TIMER);
            cpu.pc = 0x0050;
        } else if (pending & INTERRUPT_SERIAL) {
            mmu_write(0xFF0F, if_val & ~INTERRUPT_SERIAL);
            cpu.pc = 0x0058;
        } else if (pending & INTERRUPT_JOYPAD) {
            mmu_write(0xFF0F, if_val & ~INTERRUPT_JOYPAD);
            cpu.pc = 0x0060;
        }
        return 20;
    }

    if (cpu.ime_scheduled) {
        cpu.ime = true;
        cpu.ime_scheduled = false;
    }

    uint8_t op = fetch8();

    // 0x40 - 0x7F: LD r, r' and HALT
    if (op >= 0x40 && op <= 0x7F) {
        if (op == 0x76) {
            cpu.halted = true;
            return 4;
        }
        uint8_t dst = (op >> 3) & 0x07;
        uint8_t src = op & 0x07;
        uint8_t val = cb_get_reg(src);
        cb_set_reg(dst, val);
        return (dst == 6 || src == 6) ? 8 : 4;
    }

    // 0x80 - 0xBF: 8-bit ALU operations on registers / (HL)
    if (op >= 0x80 && op <= 0xBF) {
        uint8_t src = op & 0x07;
        uint8_t val = cb_get_reg(src);
        int cycles = (src == 6) ? 8 : 4;
        switch ((op >> 3) & 0x07) {
            case 0: alu_add(val); break;
            case 1: alu_adc(val); break;
            case 2: alu_sub(val); break;
            case 3: alu_sbc(val); break;
            case 4: alu_and(val); break;
            case 5: alu_xor(val); break;
            case 6: alu_or(val);  break;
            case 7: alu_cp(val);  break;
        }
        return cycles;
    }

    switch (op) {
        // 0x00 - 0x0F
        case 0x00: return 4; // NOP
        case 0x01: set_bc(fetch16()); return 12; // LD BC, d16
        case 0x02: mmu_write(get_bc(), cpu.a); return 8; // LD (BC), A
        case 0x03: set_bc(get_bc() + 1); return 8; // INC BC
        case 0x04: cpu.b = alu_inc(cpu.b); return 4; // INC B
        case 0x05: cpu.b = alu_dec(cpu.b); return 4; // DEC B
        case 0x06: cpu.b = fetch8(); return 8; // LD B, d8
        case 0x07: // RLCA
            {
                uint8_t c = (cpu.a >> 7) & 1;
                cpu.a = (cpu.a << 1) | c;
                set_flag(FLAG_Z, false);
                set_flag(FLAG_N, false);
                set_flag(FLAG_H, false);
                set_flag(FLAG_C, c != 0);
            }
            return 4;
        case 0x08: // LD (a16), SP
            {
                uint16_t a16 = fetch16();
                mmu_write(a16, cpu.sp & 0xFF);
                mmu_write(a16 + 1, (cpu.sp >> 8) & 0xFF);
            }
            return 20;
        case 0x09: add_hl(get_bc()); return 8; // ADD HL, BC
        case 0x0A: cpu.a = mmu_read(get_bc()); return 8; // LD A, (BC)
        case 0x0B: set_bc(get_bc() - 1); return 8; // DEC BC
        case 0x0C: cpu.c = alu_inc(cpu.c); return 4; // INC C
        case 0x0D: cpu.c = alu_dec(cpu.c); return 4; // DEC C
        case 0x0E: cpu.c = fetch8(); return 8; // LD C, d8
        case 0x0F: // RRCA
            {
                uint8_t c = cpu.a & 1;
                cpu.a = (cpu.a >> 1) | (c << 7);
                set_flag(FLAG_Z, false);
                set_flag(FLAG_N, false);
                set_flag(FLAG_H, false);
                set_flag(FLAG_C, c != 0);
            }
            return 4;

        // 0x10 - 0x1F
        case 0x10: fetch8(); return 4; // STOP
        case 0x11: set_de(fetch16()); return 12; // LD DE, d16
        case 0x12: mmu_write(get_de(), cpu.a); return 8; // LD (DE), A
        case 0x13: set_de(get_de() + 1); return 8; // INC DE
        case 0x14: cpu.d = alu_inc(cpu.d); return 4; // INC D
        case 0x15: cpu.d = alu_dec(cpu.d); return 4; // DEC D
        case 0x16: cpu.d = fetch8(); return 8; // LD D, d8
        case 0x17: // RLA
            {
                uint8_t old_c = get_flag(FLAG_C) ? 1 : 0;
                uint8_t new_c = (cpu.a >> 7) & 1;
                cpu.a = (cpu.a << 1) | old_c;
                set_flag(FLAG_Z, false);
                set_flag(FLAG_N, false);
                set_flag(FLAG_H, false);
                set_flag(FLAG_C, new_c != 0);
            }
            return 4;
        case 0x18: // JR r8
            {
                int8_t r8 = (int8_t)fetch8();
                cpu.pc += r8;
            }
            return 12;
        case 0x19: add_hl(get_de()); return 8; // ADD HL, DE
        case 0x1A: cpu.a = mmu_read(get_de()); return 8; // LD A, (DE)
        case 0x1B: set_de(get_de() - 1); return 8; // DEC DE
        case 0x1C: cpu.e = alu_inc(cpu.e); return 4; // INC E
        case 0x1D: cpu.e = alu_dec(cpu.e); return 4; // DEC E
        case 0x1E: cpu.e = fetch8(); return 8; // LD E, d8
        case 0x1F: // RRA
            {
                uint8_t old_c = get_flag(FLAG_C) ? 1 : 0;
                uint8_t new_c = cpu.a & 1;
                cpu.a = (cpu.a >> 1) | (old_c << 7);
                set_flag(FLAG_Z, false);
                set_flag(FLAG_N, false);
                set_flag(FLAG_H, false);
                set_flag(FLAG_C, new_c != 0);
            }
            return 4;

        // 0x20 - 0x2F
        case 0x20: // JR NZ, r8
            {
                int8_t r8 = (int8_t)fetch8();
                if (!get_flag(FLAG_Z)) {
                    cpu.pc += r8;
                    return 12;
                }
            }
            return 8;
        case 0x21: set_hl(fetch16()); return 12; // LD HL, d16
        case 0x22: // LD (HL+), A
            mmu_write(get_hl(), cpu.a);
            set_hl(get_hl() + 1);
            return 8;
        case 0x23: set_hl(get_hl() + 1); return 8; // INC HL
        case 0x24: cpu.h = alu_inc(cpu.h); return 4; // INC H
        case 0x25: cpu.h = alu_dec(cpu.h); return 4; // DEC H
        case 0x26: cpu.h = fetch8(); return 8; // LD H, d8
        case 0x27: alu_daa(); return 4; // DAA
        case 0x28: // JR Z, r8
            {
                int8_t r8 = (int8_t)fetch8();
                if (get_flag(FLAG_Z)) {
                    cpu.pc += r8;
                    return 12;
                }
            }
            return 8;
        case 0x29: add_hl(get_hl()); return 8; // ADD HL, HL
        case 0x2A: // LD A, (HL+)
            cpu.a = mmu_read(get_hl());
            set_hl(get_hl() + 1);
            return 8;
        case 0x2B: set_hl(get_hl() - 1); return 8; // DEC HL
        case 0x2C: cpu.l = alu_inc(cpu.l); return 4; // INC L
        case 0x2D: cpu.l = alu_dec(cpu.l); return 4; // DEC L
        case 0x2E: cpu.l = fetch8(); return 8; // LD L, d8
        case 0x2F: // CPL
            cpu.a = ~cpu.a;
            set_flag(FLAG_N, true);
            set_flag(FLAG_H, true);
            return 4;

        // 0x30 - 0x3F
        case 0x30: // JR NC, r8
            {
                int8_t r8 = (int8_t)fetch8();
                if (!get_flag(FLAG_C)) {
                    cpu.pc += r8;
                    return 12;
                }
            }
            return 8;
        case 0x31: cpu.sp = fetch16(); return 12; // LD SP, d16
        case 0x32: // LD (HL-), A
            mmu_write(get_hl(), cpu.a);
            set_hl(get_hl() - 1);
            return 8;
        case 0x33: cpu.sp++; return 8; // INC SP
        case 0x34: // INC (HL)
            mmu_write(get_hl(), alu_inc(mmu_read(get_hl())));
            return 12;
        case 0x35: // DEC (HL)
            mmu_write(get_hl(), alu_dec(mmu_read(get_hl())));
            return 12;
        case 0x36: mmu_write(get_hl(), fetch8()); return 12; // LD (HL), d8
        case 0x37: // SCF
            set_flag(FLAG_N, false);
            set_flag(FLAG_H, false);
            set_flag(FLAG_C, true);
            return 4;
        case 0x38: // JR C, r8
            {
                int8_t r8 = (int8_t)fetch8();
                if (get_flag(FLAG_C)) {
                    cpu.pc += r8;
                    return 12;
                }
            }
            return 8;
        case 0x39: add_hl(cpu.sp); return 8; // ADD HL, SP
        case 0x3A: // LD A, (HL-)
            cpu.a = mmu_read(get_hl());
            set_hl(get_hl() - 1);
            return 8;
        case 0x3B: cpu.sp--; return 8; // DEC SP
        case 0x3C: cpu.a = alu_inc(cpu.a); return 4; // INC A
        case 0x3D: cpu.a = alu_dec(cpu.a); return 4; // DEC A
        case 0x3E: cpu.a = fetch8(); return 8; // LD A, d8
        case 0x3F: // CCF
            set_flag(FLAG_N, false);
            set_flag(FLAG_H, false);
            set_flag(FLAG_C, !get_flag(FLAG_C));
            return 4;

        // 0xC0 - 0xCF
        case 0xC0: // RET NZ
            if (!get_flag(FLAG_Z)) {
                cpu.pc = pop16();
                return 20;
            }
            return 8;
        case 0xC1: set_bc(pop16()); return 12; // POP BC
        case 0xC2: // JP NZ, a16
            {
                uint16_t a16 = fetch16();
                if (!get_flag(FLAG_Z)) {
                    cpu.pc = a16;
                    return 16;
                }
            }
            return 12;
        case 0xC3: cpu.pc = fetch16(); return 16; // JP a16
        case 0xC4: // CALL NZ, a16
            {
                uint16_t a16 = fetch16();
                if (!get_flag(FLAG_Z)) {
                    push16(cpu.pc);
                    cpu.pc = a16;
                    return 24;
                }
            }
            return 12;
        case 0xC5: push16(get_bc()); return 16; // PUSH BC
        case 0xC6: alu_add(fetch8()); return 8; // ADD A, d8
        case 0xC7: push16(cpu.pc); cpu.pc = 0x0000; return 16; // RST 00H
        case 0xC8: // RET Z
            if (get_flag(FLAG_Z)) {
                cpu.pc = pop16();
                return 20;
            }
            return 8;
        case 0xC9: cpu.pc = pop16(); return 16; // RET
        case 0xCA: // JP Z, a16
            {
                uint16_t a16 = fetch16();
                if (get_flag(FLAG_Z)) {
                    cpu.pc = a16;
                    return 16;
                }
            }
            return 12;
        case 0xCB: return execute_cb(); // PREFIX CB
        case 0xCC: // CALL Z, a16
            {
                uint16_t a16 = fetch16();
                if (get_flag(FLAG_Z)) {
                    push16(cpu.pc);
                    cpu.pc = a16;
                    return 24;
                }
            }
            return 12;
        case 0xCD: // CALL a16
            {
                uint16_t a16 = fetch16();
                push16(cpu.pc);
                cpu.pc = a16;
            }
            return 24;
        case 0xCE: alu_adc(fetch8()); return 8; // ADC A, d8
        case 0xCF: push16(cpu.pc); cpu.pc = 0x0008; return 16; // RST 08H

        // 0xD0 - 0xDF
        case 0xD0: // RET NC
            if (!get_flag(FLAG_C)) {
                cpu.pc = pop16();
                return 20;
            }
            return 8;
        case 0xD1: set_de(pop16()); return 12; // POP DE
        case 0xD2: // JP NC, a16
            {
                uint16_t a16 = fetch16();
                if (!get_flag(FLAG_C)) {
                    cpu.pc = a16;
                    return 16;
                }
            }
            return 12;
        case 0xD4: // CALL NC, a16
            {
                uint16_t a16 = fetch16();
                if (!get_flag(FLAG_C)) {
                    push16(cpu.pc);
                    cpu.pc = a16;
                    return 24;
                }
            }
            return 12;
        case 0xD5: push16(get_de()); return 16; // PUSH DE
        case 0xD6: alu_sub(fetch8()); return 8; // SUB d8
        case 0xD7: push16(cpu.pc); cpu.pc = 0x0010; return 16; // RST 10H
        case 0xD8: // RET C
            if (get_flag(FLAG_C)) {
                cpu.pc = pop16();
                return 20;
            }
            return 8;
        case 0xD9: // RETI
            cpu.pc = pop16();
            cpu.ime = true;
            return 16;
        case 0xDA: // JP C, a16
            {
                uint16_t a16 = fetch16();
                if (get_flag(FLAG_C)) {
                    cpu.pc = a16;
                    return 16;
                }
            }
            return 12;
        case 0xDC: // CALL C, a16
            {
                uint16_t a16 = fetch16();
                if (get_flag(FLAG_C)) {
                    push16(cpu.pc);
                    cpu.pc = a16;
                    return 24;
                }
            }
            return 12;
        case 0xDE: alu_sbc(fetch8()); return 8; // SBC A, d8
        case 0xDF: push16(cpu.pc); cpu.pc = 0x0018; return 16; // RST 18H

        // 0xE0 - 0xEF
        case 0xE0: mmu_write(0xFF00 + fetch8(), cpu.a); return 12; // LDH (a8), A
        case 0xE1: set_hl(pop16()); return 12; // POP HL
        case 0xE2: mmu_write(0xFF00 + cpu.c, cpu.a); return 8; // LD (C), A
        case 0xE5: push16(get_hl()); return 16; // PUSH HL
        case 0xE6: alu_and(fetch8()); return 8; // AND d8
        case 0xE7: push16(cpu.pc); cpu.pc = 0x0020; return 16; // RST 20H
        case 0xE8: add_sp((int8_t)fetch8()); return 16; // ADD SP, r8
        case 0xE9: cpu.pc = get_hl(); return 4; // JP HL
        case 0xEA: mmu_write(fetch16(), cpu.a); return 16; // LD (a16), A
        case 0xEE: alu_xor(fetch8()); return 8; // XOR d8
        case 0xEF: push16(cpu.pc); cpu.pc = 0x0028; return 16; // RST 28H

        // 0xF0 - 0xFF
        case 0xF0: cpu.a = mmu_read(0xFF00 + fetch8()); return 12; // LDH A, (a8)
        case 0xF1: set_af(pop16()); return 12; // POP AF
        case 0xF2: cpu.a = mmu_read(0xFF00 + cpu.c); return 8; // LD A, (C)
        case 0xF3: cpu.ime = false; cpu.ime_scheduled = false; return 4; // DI
        case 0xF5: push16(get_af()); return 16; // PUSH AF
        case 0xF6: alu_or(fetch8()); return 8; // OR d8
        case 0xF7: push16(cpu.pc); cpu.pc = 0x0030; return 16; // RST 30H
        case 0xF8: ld_hl_sp((int8_t)fetch8()); return 12; // LD HL, SP+r8
        case 0xF9: cpu.sp = get_hl(); return 8; // LD SP, HL
        case 0xFA: cpu.a = mmu_read(fetch16()); return 16; // LD A, (a16)
        case 0xFB: cpu.ime_scheduled = true; return 4; // EI
        case 0xFE: alu_cp(fetch8()); return 8; // CP d8
        case 0xFF: push16(cpu.pc); cpu.pc = 0x0038; return 16; // RST 38H

        default:
            printf("Warning: Undefined opcode 0x%02X at PC: 0x%04X\n", op, (uint16_t)(cpu.pc - 1));
            return 4;
    }
}
