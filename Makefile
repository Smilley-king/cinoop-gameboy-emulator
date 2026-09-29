CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell pkg-config --cflags sdl2)
LIBS = $(shell pkg-config --libs sdl2) -lm

SRCS = main.c cpu.c mmu.c ppu.c timer.c joypad.c apu.c
OBJS = $(SRCS:.c=.o)
TARGET = cinoop

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET) Tetris.gb

clean:
	rm -f $(OBJS) $(TARGET)
