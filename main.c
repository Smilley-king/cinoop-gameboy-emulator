#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include "cpu.h"
#include "mmu.h"
#include "ppu.h"
#include "timer.h"
#include "joypad.h"
#include "apu.h"

#define SCALE 3

static void handle_key(SDL_Keycode sym, bool pressed) {
    void (*action)(JoypadButton) = pressed ? joypad_press : joypad_release;

    switch (sym) {
        case SDLK_RIGHT:
        case SDLK_d:
            action(JOYPAD_RIGHT);
            break;
        case SDLK_LEFT:
        case SDLK_a:
            action(JOYPAD_LEFT);
            break;
        case SDLK_UP:
        case SDLK_w:
            action(JOYPAD_UP);
            break;
        case SDLK_DOWN:
        case SDLK_s:
            action(JOYPAD_DOWN);
            break;
        case SDLK_z:
        case SDLK_j:
        case SDLK_c:
            action(JOYPAD_A);
            break;
        case SDLK_x:
        case SDLK_k:
            action(JOYPAD_B);
            break;
        case SDLK_RETURN:
            action(JOYPAD_START);
            break;
        case SDLK_SPACE:
        case SDLK_RSHIFT:
        case SDLK_LSHIFT:
        case SDLK_BACKSPACE:
            action(JOYPAD_SELECT);
            break;
        default:
            break;
    }
}

static void reset_system(void) {
    cpu_init();
    timer_init();
    ppu_init();
    joypad_init();
    apu_init();
}

int main(int argc, char *argv[]) {
    const char *rom_path = "Tetris.gb";
    if (argc >= 2) {
        rom_path = argv[1];
    }

    printf("=========================================\n");
    printf("   Cinoop Game Boy Emulator\n");
    printf("=========================================\n");
    printf("Controls:\n");
    printf("  D-Pad:      Arrow Keys or W/A/S/D\n");
    printf("  A Button:   Z, J, or C (Rotate Clockwise)\n");
    printf("  B Button:   X or K     (Rotate Counter-CW)\n");
    printf("  Start:      Enter\n");
    printf("  Select:     Space / Shift / Backspace\n");
    printf("  Fullscreen: F11\n");
    printf("  Reset:      R\n");
    printf("  Pause:      P\n");
    printf("  Exit:       Escape\n");
    printf("=========================================\n");

    mmu_init();
    if (!mmu_load_rom(rom_path)) {
        printf("Failed to load ROM: '%s'\n", rom_path);
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        printf("SDL Init Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    SDL_Window *window = SDL_CreateWindow(
        "Cinoop - Game Boy Emulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH * SCALE,
        SCREEN_HEIGHT * SCALE,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        printf("Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED
    );
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }

    SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, SCREEN_HEIGHT);

    SDL_Texture *texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        SCREEN_WIDTH,
        SCREEN_HEIGHT
    );

    reset_system();

    printf("Emulation started for: %s\n", rom_path);

    int running = 1;
    bool paused = false;
    bool is_fullscreen = false;
    SDL_Event e;

    uint64_t perf_freq = SDL_GetPerformanceFrequency();
    uint64_t frame_interval = (perf_freq * 70224) / 4194304; // 16.7427 ms per frame
    uint64_t next_frame_time = SDL_GetPerformanceCounter();

    while (running) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = 0;
            } else if (e.type == SDL_KEYDOWN) {
                if (e.key.repeat) break;

                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    running = 0;
                } else if (e.key.keysym.sym == SDLK_F11) {
                    is_fullscreen = !is_fullscreen;
                    SDL_SetWindowFullscreen(window, is_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                } else if (e.key.keysym.sym == SDLK_r) {
                    printf("Resetting emulation...\n");
                    reset_system();
                } else if (e.key.keysym.sym == SDLK_p) {
                    paused = !paused;
                    printf(paused ? "Paused\n" : "Resumed\n");
                } else {
                    handle_key(e.key.keysym.sym, true);
                }
            } else if (e.type == SDL_KEYUP) {
                handle_key(e.key.keysym.sym, false);
            }
        }

        if (!paused) {
            // Run until PPU finishes rendering a frame
            while (!ppu_is_frame_ready()) {
                int cycles = cpu_step();
                timer_step(cycles);
                ppu_step(cycles);
                apu_step(cycles);
            }
            ppu_clear_frame_ready();

            // Update display
            SDL_UpdateTexture(texture, NULL, ppu_get_framebuffer(), SCREEN_WIDTH * sizeof(uint32_t));
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);
        } else {
            SDL_Delay(16);
        }

        // High precision frame timing (~59.73 FPS)
        next_frame_time += frame_interval;
        uint64_t now = SDL_GetPerformanceCounter();
        if (now < next_frame_time) {
            uint32_t delay_ms = (uint32_t)(((next_frame_time - now) * 1000) / perf_freq);
            if (delay_ms > 1) {
                SDL_Delay(delay_ms - 1);
            }
            while (SDL_GetPerformanceCounter() < next_frame_time) {
                // Precision spin-wait for the remaining sub-millisecond
            }
        } else {
            // Avoid fast-forward spiral if lagging behind
            if (now - next_frame_time > frame_interval * 2) {
                next_frame_time = now;
            }
        }
    }

    apu_close();
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    if (rom_data) {
        free(rom_data);
    }

    printf("Emulation closed gracefully.\n");
    return 0;
}