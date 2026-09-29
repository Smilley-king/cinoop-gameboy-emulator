#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include <stdbool.h>

void timer_init(void);
void timer_step(int cycles);

uint8_t timer_read_div(void);
void timer_reset_div(void);

uint8_t timer_read_tima(void);
void timer_write_tima(uint8_t val);

uint8_t timer_read_tma(void);
void timer_write_tma(uint8_t val);

uint8_t timer_read_tac(void);
void timer_write_tac(uint8_t val);

#endif // TIMER_H
