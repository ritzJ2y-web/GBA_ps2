#ifndef GBA_CORE_H
#define GBA_CORE_H

#include <stdint.h>
#include <stddef.h>

#define GBA_WIDTH  240
#define GBA_HEIGHT 160
#define GBA_PIXELS (GBA_WIDTH * GBA_HEIGHT)

/* Registradores I/O principais do GBA */
#define REG_DISPCNT  0x04000000u
#define REG_DISPSTAT 0x04000004u
#define REG_VCOUNT   0x04000006u
#define REG_BG0CNT   0x04000008u
#define REG_BG1CNT   0x0400000Au
#define REG_BG2CNT   0x0400000Cu
#define REG_BG3CNT   0x0400000Eu
#define REG_BG0HOFS  0x04000010u
#define REG_BG0VOFS  0x04000012u
#define REG_BG1HOFS  0x04000014u
#define REG_BG1VOFS  0x04000016u
#define REG_BG2HOFS  0x04000018u
#define REG_BG2VOFS  0x0400001Au
#define REG_BG3HOFS  0x0400001Cu
#define REG_BG3VOFS  0x0400001Eu
#define REG_KEYINPUT 0x04000130u
#define REG_IE       0x04000200u
#define REG_IF       0x04000202u
#define REG_IME      0x04000208u

typedef struct {
    uint32_t r[16];     /* r0..r12, r13(SP), r14(LR), r15(PC) */
    uint32_t cpsr;
    uint32_t spsr;
    uint32_t banked_sp[6];
    uint32_t banked_lr[6];
    uint32_t banked_spsr[6];

    uint8_t *bios;      /* 16 KB */
    uint8_t *ewram;     /* 256 KB */
    uint8_t *iwram;     /* 32 KB */
    uint8_t *io;        /* 1 KB */
    uint8_t *palette;   /* 1 KB */
    uint8_t *vram;      /* 96 KB */
    uint8_t *oam;       /* 1 KB */

    uint8_t *rom;
    size_t   rom_size;

    uint32_t framebuffer[GBA_PIXELS]; /* ABGR8888 pronto para gsKit */
    int      rom_loaded;
    uint32_t cycles;
} GBA_State;

void GBA_Init(GBA_State *gba);
void GBA_Free(GBA_State *gba);
int  GBA_LoadROM(GBA_State *gba, const uint8_t *data, size_t size);
void GBA_SetKeys(GBA_State *gba, uint16_t keyinput);
void GBA_RunFrame(GBA_State *gba);

uint8_t  GBA_Read8(GBA_State *gba, uint32_t addr);
uint16_t GBA_Read16(GBA_State *gba, uint32_t addr);
uint32_t GBA_Read32(GBA_State *gba, uint32_t addr);
void     GBA_Write8(GBA_State *gba, uint32_t addr, uint8_t val);
void     GBA_Write16(GBA_State *gba, uint32_t addr, uint16_t val);
void     GBA_Write32(GBA_State *gba, uint32_t addr, uint32_t val);

#endif
