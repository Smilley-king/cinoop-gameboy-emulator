#include "apu.h"
#include <SDL2/SDL.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

static bool audio_active = false;
static SDL_AudioDeviceID audio_device = 0;

// Channel 1: Square with sweep & envelope
typedef struct {
    bool enabled;
    bool dac_enabled;
    uint8_t duty;
    uint8_t duty_step;
    int timer;

    // Length
    int length_counter;
    bool length_enabled;

    // Envelope
    uint8_t volume;
    uint8_t initial_volume;
    uint8_t env_dir;
    uint8_t env_pace;
    int env_timer;

    // Frequency & Sweep
    uint16_t freq;
    uint16_t shadow_freq;
    int sweep_timer;
    uint8_t sweep_pace;
    uint8_t sweep_dir;
    uint8_t sweep_shift;
    bool sweep_enabled;
} Channel1;

// Channel 2: Square with envelope
typedef struct {
    bool enabled;
    bool dac_enabled;
    uint8_t duty;
    uint8_t duty_step;
    int timer;

    // Length
    int length_counter;
    bool length_enabled;

    // Envelope
    uint8_t volume;
    uint8_t initial_volume;
    uint8_t env_dir;
    uint8_t env_pace;
    int env_timer;

    // Frequency
    uint16_t freq;
} Channel2;

// Channel 3: Wave output
typedef struct {
    bool enabled;
    bool dac_enabled;
    int timer;
    uint8_t sample_index;
    uint8_t volume_shift;

    // Length
    int length_counter;
    bool length_enabled;

    // Frequency & Wave RAM
    uint16_t freq;
    uint8_t wave_ram[16];
} Channel3;

// Channel 4: Noise
typedef struct {
    bool enabled;
    bool dac_enabled;
    int timer;
    uint16_t lfsr;

    // Length
    int length_counter;
    bool length_enabled;

    // Envelope
    uint8_t volume;
    uint8_t initial_volume;
    uint8_t env_dir;
    uint8_t env_pace;
    int env_timer;

    // Polynomial
    uint8_t shift_clock;
    bool width_mode;
    uint8_t divisor_code;
} Channel4;

static Channel1 ch1;
static Channel2 ch2;
static Channel3 ch3;
static Channel4 ch4;

static uint8_t nr10 = 0;
static uint8_t nr50 = 0x77;
static uint8_t nr51 = 0xF3;
static uint8_t nr52 = 0x80;

static const uint8_t duty_table[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1}, // 12.5%
    {1, 0, 0, 0, 0, 0, 0, 1}, // 25%
    {1, 0, 0, 0, 0, 1, 1, 1}, // 50%
    {0, 1, 1, 1, 1, 1, 1, 0}  // 75%
};

static const int divisors[8] = { 8, 16, 32, 48, 64, 80, 96, 112 };

// Frame sequencer (512 Hz, clocked every 8192 CPU cycles)
static int frame_seq_cycles = 0;
static int frame_seq_step = 0;

// Sample generation
static int sample_accum = 0;
#define SAMPLE_BUFFER_SIZE 2048
static int16_t sample_buffer[SAMPLE_BUFFER_SIZE * 2];
static int sample_count = 0;

static void ch1_sweep_step(void) {
    if (!ch1.sweep_enabled) return;

    if (--ch1.sweep_timer <= 0) {
        ch1.sweep_timer = ch1.sweep_pace ? ch1.sweep_pace : 8;

        if (ch1.sweep_enabled && ch1.sweep_pace > 0) {
            uint16_t delta = ch1.shadow_freq >> ch1.sweep_shift;
            uint16_t new_freq;

            if (ch1.sweep_dir == 1) { // Decrease
                new_freq = (ch1.shadow_freq >= delta) ? (ch1.shadow_freq - delta) : 0;
            } else { // Increase
                new_freq = ch1.shadow_freq + delta;
            }

            if (new_freq > 2047) {
                ch1.enabled = false;
            } else if (ch1.sweep_shift > 0) {
                ch1.shadow_freq = new_freq;
                ch1.freq = new_freq;

                // Overflow check on recalculation
                uint16_t check_delta = new_freq >> ch1.sweep_shift;
                uint16_t check_freq = (ch1.sweep_dir == 1) ? (new_freq - check_delta) : (new_freq + check_delta);
                if (check_freq > 2047) {
                    ch1.enabled = false;
                }
            }
        }
    }
}

static void clock_length_counters(void) {
    if (ch1.length_enabled && ch1.length_counter > 0) {
        if (--ch1.length_counter == 0) ch1.enabled = false;
    }
    if (ch2.length_enabled && ch2.length_counter > 0) {
        if (--ch2.length_counter == 0) ch2.enabled = false;
    }
    if (ch3.length_enabled && ch3.length_counter > 0) {
        if (--ch3.length_counter == 0) ch3.enabled = false;
    }
    if (ch4.length_enabled && ch4.length_counter > 0) {
        if (--ch4.length_counter == 0) ch4.enabled = false;
    }
}

static void clock_volume_envelopes(void) {
    // Channel 1
    if (ch1.env_pace > 0) {
        if (--ch1.env_timer <= 0) {
            ch1.env_timer = ch1.env_pace;
            if (ch1.env_dir == 1 && ch1.volume < 15) ch1.volume++;
            else if (ch1.env_dir == 0 && ch1.volume > 0) ch1.volume--;
        }
    }

    // Channel 2
    if (ch2.env_pace > 0) {
        if (--ch2.env_timer <= 0) {
            ch2.env_timer = ch2.env_pace;
            if (ch2.env_dir == 1 && ch2.volume < 15) ch2.volume++;
            else if (ch2.env_dir == 0 && ch2.volume > 0) ch2.volume--;
        }
    }

    // Channel 4
    if (ch4.env_pace > 0) {
        if (--ch4.env_timer <= 0) {
            ch4.env_timer = ch4.env_pace;
            if (ch4.env_dir == 1 && ch4.volume < 15) ch4.volume++;
            else if (ch4.env_dir == 0 && ch4.volume > 0) ch4.volume--;
        }
    }
}

static void generate_sample(void) {
    int left = 0;
    int right = 0;

    if (nr52 & 0x80) {
        // Channel 1
        int s1 = 0;
        if (ch1.enabled && ch1.dac_enabled && ch1.volume > 0) {
            s1 = duty_table[ch1.duty][ch1.duty_step] ? (int)ch1.volume : -(int)ch1.volume;
        }

        // Channel 2
        int s2 = 0;
        if (ch2.enabled && ch2.dac_enabled && ch2.volume > 0) {
            s2 = duty_table[ch2.duty][ch2.duty_step] ? (int)ch2.volume : -(int)ch2.volume;
        }

        // Channel 3
        int s3 = 0;
        if (ch3.enabled && ch3.dac_enabled && ch3.volume_shift > 0) {
            uint8_t byte = ch3.wave_ram[ch3.sample_index / 2];
            uint8_t val = (ch3.sample_index & 1) ? (byte & 0x0F) : (byte >> 4);
            val >>= (ch3.volume_shift - 1);
            s3 = (int)val - 8;
        }

        // Channel 4
        int s4 = 0;
        if (ch4.enabled && ch4.dac_enabled && ch4.volume > 0) {
            s4 = (!(ch4.lfsr & 1)) ? (int)ch4.volume : -(int)ch4.volume;
        }

        // Scaling factor for output volume
        const int scale = 120;
        s1 *= scale;
        s2 *= scale;
        s3 *= scale;
        s4 *= scale;

        // Panning: NR51
        if (nr51 & 0x01) right += s1;
        if (nr51 & 0x10) left  += s1;
        if (nr51 & 0x02) right += s2;
        if (nr51 & 0x20) left  += s2;
        if (nr51 & 0x04) right += s3;
        if (nr51 & 0x40) left  += s3;
        if (nr51 & 0x08) right += s4;
        if (nr51 & 0x80) left  += s4;

        // Master Volume: NR50
        int master_left = (nr50 >> 4) & 0x07;
        int master_right = nr50 & 0x07;
        left = (left * (master_left + 1)) / 8;
        right = (right * (master_right + 1)) / 8;
    }

    // Clamp to 16-bit
    if (left > 32767) left = 32767;
    if (left < -32768) left = -32768;
    if (right > 32767) right = 32767;
    if (right < -32768) right = -32768;

    sample_buffer[sample_count * 2]     = (int16_t)left;
    sample_buffer[sample_count * 2 + 1] = (int16_t)right;
    sample_count++;

    if (sample_count >= SAMPLE_BUFFER_SIZE) {
        if (audio_active && audio_device != 0) {
            uint32_t queued = SDL_GetQueuedAudioSize(audio_device);
            // Keep queue between 1-3 frames of audio to prevent lag and buffer overflow
            if (queued < 8192) {
                SDL_QueueAudio(audio_device, sample_buffer, sample_count * sizeof(int16_t) * 2);
            }
        }
        sample_count = 0;
    }
}

void apu_init(void) {
    memset(&ch1, 0, sizeof(ch1));
    memset(&ch2, 0, sizeof(ch2));
    memset(&ch3, 0, sizeof(ch3));
    memset(&ch4, 0, sizeof(ch4));
    ch4.lfsr = 0x7FFF;

    nr10 = 0;
    nr50 = 0x77;
    nr51 = 0xF3;
    nr52 = 0x80;

    frame_seq_cycles = 0;
    frame_seq_step = 0;
    sample_accum = 0;
    sample_count = 0;

    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = NULL; // Use SDL_QueueAudio

    audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_device != 0) {
        SDL_PauseAudioDevice(audio_device, 0);
        audio_active = true;
    } else {
        audio_active = false;
    }
}

void apu_step(int cycles) {
    // 1. Advance Channel Timers
    // Channel 1 timer
    ch1.timer -= cycles;
    while (ch1.timer <= 0) {
        ch1.timer += (2048 - ch1.freq) * 4;
        ch1.duty_step = (ch1.duty_step + 1) & 7;
    }

    // Channel 2 timer
    ch2.timer -= cycles;
    while (ch2.timer <= 0) {
        ch2.timer += (2048 - ch2.freq) * 4;
        ch2.duty_step = (ch2.duty_step + 1) & 7;
    }

    // Channel 3 timer
    ch3.timer -= cycles;
    while (ch3.timer <= 0) {
        ch3.timer += (2048 - ch3.freq) * 2;
        ch3.sample_index = (ch3.sample_index + 1) & 31;
    }

    // Channel 4 timer
    ch4.timer -= cycles;
    while (ch4.timer <= 0) {
        int div = divisors[ch4.divisor_code];
        ch4.timer += (div << ch4.shift_clock);
        uint16_t b0 = ch4.lfsr & 1;
        uint16_t b1 = (ch4.lfsr >> 1) & 1;
        ch4.lfsr = (ch4.lfsr >> 1) | ((b0 ^ b1) << 14);
        if (ch4.width_mode) {
            ch4.lfsr = (ch4.lfsr & ~0x40) | ((b0 ^ b1) << 6);
        }
    }

    // 2. Advance Frame Sequencer (512 Hz = every 8192 cycles)
    frame_seq_cycles += cycles;
    while (frame_seq_cycles >= 8192) {
        frame_seq_cycles -= 8192;
        switch (frame_seq_step) {
            case 0:
                clock_length_counters();
                break;
            case 1:
                break;
            case 2:
                clock_length_counters();
                ch1_sweep_step();
                break;
            case 3:
                break;
            case 4:
                clock_length_counters();
                break;
            case 5:
                break;
            case 6:
                clock_length_counters();
                ch1_sweep_step();
                break;
            case 7:
                clock_volume_envelopes();
                break;
        }
        frame_seq_step = (frame_seq_step + 1) & 7;
    }

    // 3. Audio Sample Generation (44100 Hz from 4194304 Hz CPU clock)
    sample_accum += cycles * 44100;
    while (sample_accum >= 4194304) {
        sample_accum -= 4194304;
        generate_sample();
    }
}

uint8_t apu_read(uint16_t addr) {
    if (addr >= 0xFF30 && addr <= 0xFF3F) {
        return ch3.wave_ram[addr - 0xFF30];
    }

    switch (addr) {
        case 0xFF10: return nr10 | 0x80;
        case 0xFF11: return (ch1.duty << 6) | 0x3F;
        case 0xFF12: return (ch1.initial_volume << 4) | (ch1.env_dir << 3) | ch1.env_pace;
        case 0xFF14: return (ch1.length_enabled ? 0x40 : 0) | 0xBF;
        case 0xFF16: return (ch2.duty << 6) | 0x3F;
        case 0xFF17: return (ch2.initial_volume << 4) | (ch2.env_dir << 3) | ch2.env_pace;
        case 0xFF19: return (ch2.length_enabled ? 0x40 : 0) | 0xBF;
        case 0xFF1A: return (ch3.dac_enabled ? 0x80 : 0) | 0x7F;
        case 0xFF1C: return (ch3.volume_shift << 5) | 0x9F;
        case 0xFF1E: return (ch3.length_enabled ? 0x40 : 0) | 0xBF;
        case 0xFF21: return (ch4.initial_volume << 4) | (ch4.env_dir << 3) | ch4.env_pace;
        case 0xFF22: return (ch4.shift_clock << 4) | (ch4.width_mode ? 0x08 : 0) | ch4.divisor_code;
        case 0xFF23: return (ch4.length_enabled ? 0x40 : 0) | 0xBF;
        case 0xFF24: return nr50;
        case 0xFF25: return nr51;
        case 0xFF26: {
            uint8_t res = (nr52 & 0x80) | 0x70;
            if (ch1.enabled && ch1.dac_enabled) res |= 0x01;
            if (ch2.enabled && ch2.dac_enabled) res |= 0x02;
            if (ch3.enabled && ch3.dac_enabled) res |= 0x04;
            if (ch4.enabled && ch4.dac_enabled) res |= 0x08;
            return res;
        }
        default: return 0xFF;
    }
}

void apu_write(uint16_t addr, uint8_t val) {
    if (addr >= 0xFF30 && addr <= 0xFF3F) {
        ch3.wave_ram[addr - 0xFF30] = val;
        return;
    }

    if (addr == 0xFF26) {
        nr52 = val & 0x80;
        if (!(nr52 & 0x80)) {
            // Power off all APU components
            ch1.enabled = false;
            ch2.enabled = false;
            ch3.enabled = false;
            ch4.enabled = false;
        }
        return;
    }

    if (!(nr52 & 0x80)) return; // APU powered off

    switch (addr) {
        // Channel 1
        case 0xFF10:
            nr10 = val;
            ch1.sweep_pace = (val >> 4) & 0x07;
            ch1.sweep_dir = (val >> 3) & 0x01;
            ch1.sweep_shift = val & 0x07;
            break;
        case 0xFF11:
            ch1.duty = (val >> 6) & 0x03;
            ch1.length_counter = 64 - (val & 0x3F);
            break;
        case 0xFF12:
            ch1.initial_volume = (val >> 4) & 0x0F;
            ch1.volume = ch1.initial_volume;
            ch1.env_dir = (val >> 3) & 0x01;
            ch1.env_pace = val & 0x07;
            ch1.dac_enabled = (val & 0xF8) != 0;
            if (!ch1.dac_enabled) ch1.enabled = false;
            break;
        case 0xFF13:
            ch1.freq = (ch1.freq & 0x0700) | val;
            break;
        case 0xFF14:
            ch1.freq = (ch1.freq & 0x00FF) | ((uint16_t)(val & 0x07) << 8);
            ch1.length_enabled = (val & 0x40) != 0;
            if (val & 0x80) { // Trigger
                ch1.enabled = ch1.dac_enabled;
                if (ch1.length_counter == 0) ch1.length_counter = 64;
                ch1.volume = ch1.initial_volume;
                ch1.env_timer = ch1.env_pace ? ch1.env_pace : 8;
                ch1.shadow_freq = ch1.freq;
                ch1.sweep_timer = ch1.sweep_pace ? ch1.sweep_pace : 8;
                ch1.sweep_enabled = (ch1.sweep_pace > 0) || (ch1.sweep_shift > 0);
                if (ch1.sweep_shift > 0) {
                    uint16_t delta = ch1.shadow_freq >> ch1.sweep_shift;
                    uint16_t check_freq = (ch1.sweep_dir == 1) ? (ch1.shadow_freq - delta) : (ch1.shadow_freq + delta);
                    if (check_freq > 2047) ch1.enabled = false;
                }
            }
            break;

        // Channel 2
        case 0xFF16:
            ch2.duty = (val >> 6) & 0x03;
            ch2.length_counter = 64 - (val & 0x3F);
            break;
        case 0xFF17:
            ch2.initial_volume = (val >> 4) & 0x0F;
            ch2.volume = ch2.initial_volume;
            ch2.env_dir = (val >> 3) & 0x01;
            ch2.env_pace = val & 0x07;
            ch2.dac_enabled = (val & 0xF8) != 0;
            if (!ch2.dac_enabled) ch2.enabled = false;
            break;
        case 0xFF18:
            ch2.freq = (ch2.freq & 0x0700) | val;
            break;
        case 0xFF19:
            ch2.freq = (ch2.freq & 0x00FF) | ((uint16_t)(val & 0x07) << 8);
            ch2.length_enabled = (val & 0x40) != 0;
            if (val & 0x80) { // Trigger
                ch2.enabled = ch2.dac_enabled;
                if (ch2.length_counter == 0) ch2.length_counter = 64;
                ch2.volume = ch2.initial_volume;
                ch2.env_timer = ch2.env_pace ? ch2.env_pace : 8;
            }
            break;

        // Channel 3
        case 0xFF1A:
            ch3.dac_enabled = (val & 0x80) != 0;
            if (!ch3.dac_enabled) ch3.enabled = false;
            break;
        case 0xFF1B:
            ch3.length_counter = 256 - val;
            break;
        case 0xFF1C:
            switch ((val >> 5) & 0x03) {
                case 0: ch3.volume_shift = 0; break; // Mute
                case 1: ch3.volume_shift = 1; break; // 100%
                case 2: ch3.volume_shift = 2; break; // 50%
                case 3: ch3.volume_shift = 3; break; // 25%
            }
            break;
        case 0xFF1D:
            ch3.freq = (ch3.freq & 0x0700) | val;
            break;
        case 0xFF1E:
            ch3.freq = (ch3.freq & 0x00FF) | ((uint16_t)(val & 0x07) << 8);
            ch3.length_enabled = (val & 0x40) != 0;
            if (val & 0x80) { // Trigger
                ch3.enabled = ch3.dac_enabled;
                if (ch3.length_counter == 0) ch3.length_counter = 256;
                ch3.sample_index = 0;
            }
            break;

        // Channel 4
        case 0xFF20:
            ch4.length_counter = 64 - (val & 0x3F);
            break;
        case 0xFF21:
            ch4.initial_volume = (val >> 4) & 0x0F;
            ch4.volume = ch4.initial_volume;
            ch4.env_dir = (val >> 3) & 0x01;
            ch4.env_pace = val & 0x07;
            ch4.dac_enabled = (val & 0xF8) != 0;
            if (!ch4.dac_enabled) ch4.enabled = false;
            break;
        case 0xFF22:
            ch4.shift_clock = (val >> 4) & 0x0F;
            ch4.width_mode = (val & 0x08) != 0;
            ch4.divisor_code = val & 0x07;
            break;
        case 0xFF23:
            ch4.length_enabled = (val & 0x40) != 0;
            if (val & 0x80) { // Trigger
                ch4.enabled = ch4.dac_enabled;
                ch4.lfsr = 0x7FFF;
                if (ch4.length_counter == 0) ch4.length_counter = 64;
                ch4.volume = ch4.initial_volume;
                ch4.env_timer = ch4.env_pace ? ch4.env_pace : 8;
            }
            break;

        // Control
        case 0xFF24: nr50 = val; break;
        case 0xFF25: nr51 = val; break;
    }
}

void apu_close(void) {
    if (audio_active && audio_device != 0) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
        audio_active = false;
    }
}
