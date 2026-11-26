#ifndef GBA_CORE_H
#define GBA_CORE_H

#include <stdint.h>
#include <stddef.h>

#define GBA_WIDTH  240
#define GBA_HEIGHT 160

typedef struct {
    uint32_t regs[16];
    uint32_t cpsr;
    uint8_t *bios;
    uint8_t *ewram;
    uint8_t *iwram;
    uint8_t *io;
    uint8_t *pram;
    uint8_t *vram;
    uint8_t *oam;
    uint8_t *rom;
    size_t rom_size;
    int rom_loaded;
    uint16_t keys;
    uint32_t framebuffer[GBA_WIDTH * GBA_HEIGHT];
} GBA_State;

void GBA_Init(GBA_State *state);
void GBA_LoadROM(GBA_State *state, const uint8_t *data, size_t size);
void GBA_SetKeys(GBA_State *state, uint16_t keys);
void GBA_RunFrame(GBA_State *state);

#endif
