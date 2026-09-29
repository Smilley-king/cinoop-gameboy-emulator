#include "ppu.h"
#include "mmu.h"
#include <string.h>

static uint8_t lcdc = 0x91;
static uint8_t stat = 0x85;
static uint8_t scy = 0;
static uint8_t scx = 0;
static uint8_t ly = 0;
static uint8_t lyc = 0;
static uint8_t bgp = 0xFC;
static uint8_t obp0 = 0xFF;
static uint8_t obp1 = 0xFF;
static uint8_t wy = 0;
static uint8_t wx = 0;

static int scanline_cycles = 0;
static int window_line = 0;
static bool frame_ready = false;

static uint32_t framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];
static uint8_t bg_color_ids[SCREEN_WIDTH];

static const uint32_t dmg_palette[4] = {
    0xFFE0F8D0, // Lightest
    0xFF88C070, // Light green
    0xFF346856, // Dark green
    0xFF081820  // Darkest green
};

void ppu_init(void) {
    lcdc = 0x91;
    stat = 0x85;
    scy = 0;
    scx = 0;
    ly = 0;
    lyc = 0;
    bgp = 0xFC;
    obp0 = 0xFF;
    obp1 = 0xFF;
    wy = 0;
    wx = 0;
    scanline_cycles = 0;
    window_line = 0;
    frame_ready = false;
    memset(framebuffer, 0xFF, sizeof(framebuffer));
}

bool ppu_is_frame_ready(void) {
    return frame_ready;
}

void ppu_clear_frame_ready(void) {
    frame_ready = false;
}

const uint32_t *ppu_get_framebuffer(void) {
    return framebuffer;
}

static void check_lyc(void) {
    if (ly == lyc) {
        stat |= 0x04; // Coincidence flag
        if (stat & 0x40) { // LYC=LY STAT interrupt
            mmu_request_interrupt(INTERRUPT_LCD_STAT);
        }
    } else {
        stat &= ~0x04;
    }
}

static void set_stat_mode(uint8_t mode) {
    uint8_t prev_mode = stat & 0x03;
    stat = (stat & ~0x03) | (mode & 0x03);

    if (prev_mode != mode) {
        bool req = false;
        if (mode == 0 && (stat & 0x08)) req = true; // Mode 0 HBlank
        if (mode == 1 && (stat & 0x10)) req = true; // Mode 1 VBlank
        if (mode == 2 && (stat & 0x20)) req = true; // Mode 2 OAM
        if (req) {
            mmu_request_interrupt(INTERRUPT_LCD_STAT);
        }
    }
}

static void render_scanline(uint8_t current_ly) {
    if (current_ly >= SCREEN_HEIGHT) return;

    uint32_t *line_pixels = &framebuffer[current_ly * SCREEN_WIDTH];

    // 1. Background
    if (lcdc & 0x01) { // BG display enable
        uint16_t map_base = (lcdc & 0x08) ? 0x9C00 : 0x9800;
        bool unsigned_tile = (lcdc & 0x10) != 0;
        uint8_t y_pos = (current_ly + scy) & 0xFF;
        uint16_t tile_row = (y_pos / 8) * 32;

        for (int x = 0; x < SCREEN_WIDTH; x++) {
            uint8_t x_pos = (x + scx) & 0xFF;
            uint16_t tile_col = x_pos / 8;
            uint8_t tile_idx = mmu_read(map_base + tile_row + tile_col);

            uint16_t tile_addr;
            if (unsigned_tile) {
                tile_addr = 0x8000 + ((uint16_t)tile_idx * 16);
            } else {
                tile_addr = 0x9000 + ((int16_t)(int8_t)tile_idx * 16);
            }

            uint8_t row_in_tile = (y_pos % 8) * 2;
            uint8_t b1 = mmu_read(tile_addr + row_in_tile);
            uint8_t b2 = mmu_read(tile_addr + row_in_tile + 1);

            int bit = 7 - (x_pos % 8);
            uint8_t color_id = (((b2 >> bit) & 1) << 1) | ((b1 >> bit) & 1);
            bg_color_ids[x] = color_id;

            uint8_t shade = (bgp >> (color_id * 2)) & 0x03;
            line_pixels[x] = dmg_palette[shade];
        }
    } else {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            bg_color_ids[x] = 0;
            line_pixels[x] = dmg_palette[0];
        }
    }

    // 2. Window
    if ((lcdc & 0x01) && (lcdc & 0x20) && current_ly >= wy && wx <= 166) {
        uint16_t win_map_base = (lcdc & 0x40) ? 0x9C00 : 0x9800;
        bool unsigned_tile = (lcdc & 0x10) != 0;
        int win_x = (int)wx - 7;
        uint16_t win_tile_row = (window_line / 8) * 32;
        bool window_drawn = false;

        for (int x = 0; x < SCREEN_WIDTH; x++) {
            if (x < win_x) continue;

            window_drawn = true;
            uint16_t tile_col = (x - win_x) / 8;
            uint8_t tile_idx = mmu_read(win_map_base + win_tile_row + tile_col);

            uint16_t tile_addr;
            if (unsigned_tile) {
                tile_addr = 0x8000 + ((uint16_t)tile_idx * 16);
            } else {
                tile_addr = 0x9000 + ((int16_t)(int8_t)tile_idx * 16);
            }

            uint8_t row_in_tile = (window_line % 8) * 2;
            uint8_t b1 = mmu_read(tile_addr + row_in_tile);
            uint8_t b2 = mmu_read(tile_addr + row_in_tile + 1);

            int bit = 7 - ((x - win_x) % 8);
            uint8_t color_id = (((b2 >> bit) & 1) << 1) | ((b1 >> bit) & 1);
            bg_color_ids[x] = color_id;

            uint8_t shade = (bgp >> (color_id * 2)) & 0x03;
            line_pixels[x] = dmg_palette[shade];
        }

        if (window_drawn) {
            window_line++;
        }
    }

    // 3. Sprites
    if (lcdc & 0x02) { // Sprite display enable
        int sprite_height = (lcdc & 0x04) ? 16 : 8;

        // Iterate backwards through OAM so lower-indexed sprites draw on top
        for (int i = 39; i >= 0; i--) {
            int sprite_y = (int)mmu_read(0xFE00 + i * 4) - 16;
            int sprite_x = (int)mmu_read(0xFE00 + i * 4 + 1) - 8;
            uint8_t tile_idx = mmu_read(0xFE00 + i * 4 + 2);
            uint8_t flags = mmu_read(0xFE00 + i * 4 + 3);

            if (current_ly < sprite_y || current_ly >= sprite_y + sprite_height) {
                continue;
            }

            if (sprite_height == 16) {
                tile_idx &= 0xFE;
            }

            int row = current_ly - sprite_y;
            if (flags & 0x40) { // Y-flip
                row = sprite_height - 1 - row;
            }

            uint16_t tile_addr = 0x8000 + (tile_idx * 16) + (row * 2);
            uint8_t b1 = mmu_read(tile_addr);
            uint8_t b2 = mmu_read(tile_addr + 1);
            uint8_t pal = (flags & 0x10) ? obp1 : obp0;

            for (int col = 0; col < 8; col++) {
                int px_x = sprite_x + col;
                if (px_x < 0 || px_x >= SCREEN_WIDTH) continue;

                int bit = (flags & 0x20) ? col : (7 - col); // X-flip
                uint8_t color_id = (((b2 >> bit) & 1) << 1) | ((b1 >> bit) & 1);

                if (color_id == 0) continue; // Transparent

                // Priority: if bit 7 is set and BG color is not 0, sprite is behind BG
                if ((flags & 0x80) && (bg_color_ids[px_x] != 0)) {
                    continue;
                }

                uint8_t shade = (pal >> (color_id * 2)) & 0x03;
                line_pixels[px_x] = dmg_palette[shade];
            }
        }
    }
}

void ppu_step(int cycles) {
    if (!(lcdc & 0x80)) {
        // LCD is disabled: count cycles so frames continue to elapse and the main loop doesn't freeze
        scanline_cycles += cycles;
        if (scanline_cycles >= 70224) {
            scanline_cycles -= 70224;
            frame_ready = true;
        }
        ly = 0;
        stat = (stat & ~0x03); // Mode 0
        window_line = 0;
        return;
    }

    scanline_cycles += cycles;

    if (ly < 144) {
        if (scanline_cycles < 80) {
            set_stat_mode(2); // Mode 2: OAM search
        } else if (scanline_cycles < 80 + 172) {
            set_stat_mode(3); // Mode 3: Pixel transfer
        } else {
            if ((stat & 0x03) != 0) {
                // Enter Mode 0: HBlank -> render line!
                render_scanline(ly);
            }
            set_stat_mode(0); // Mode 0: HBlank
        }
    } else {
        set_stat_mode(1); // Mode 1: VBlank
    }

    if (scanline_cycles >= 456) {
        scanline_cycles -= 456;
        ly++;

        if (ly == 144) {
            set_stat_mode(1);
            mmu_request_interrupt(INTERRUPT_VBLANK);
            frame_ready = true;
        } else if (ly > 153) {
            ly = 0;
            window_line = 0;
        }

        check_lyc();
    }
}

uint8_t ppu_read(uint16_t addr) {
    switch (addr) {
        case 0xFF40: return lcdc;
        case 0xFF41: return stat | 0x80;
        case 0xFF42: return scy;
        case 0xFF43: return scx;
        case 0xFF44: return ly;
        case 0xFF45: return lyc;
        case 0xFF47: return bgp;
        case 0xFF48: return obp0;
        case 0xFF49: return obp1;
        case 0xFF4A: return wy;
        case 0xFF4B: return wx;
        default: return 0xFF;
    }
}

void ppu_write(uint16_t addr, uint8_t val) {
    switch (addr) {
        case 0xFF40:
            {
                bool was_on = (lcdc & 0x80) != 0;
                bool is_on = (val & 0x80) != 0;
                lcdc = val;
                if (was_on && !is_on) {
                    ly = 0;
                    scanline_cycles = 0;
                    stat = (stat & ~0x03);
                } else if (!was_on && is_on) {
                    scanline_cycles = 0;
                    ly = 0;
                    check_lyc();
                }
            }
            break;
        case 0xFF41:
            stat = (stat & 0x07) | (val & 0xF8);
            break;
        case 0xFF42: scy = val; break;
        case 0xFF43: scx = val; break;
        case 0xFF44: /* LY is read-only */ break;
        case 0xFF45:
            lyc = val;
            if (lcdc & 0x80) check_lyc();
            break;
        case 0xFF47: bgp = val; break;
        case 0xFF48: obp0 = val; break;
        case 0xFF49: obp1 = val; break;
        case 0xFF4A: wy = val; break;
        case 0xFF4B: wx = val; break;
    }
}
