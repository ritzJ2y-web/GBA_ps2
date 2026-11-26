#include "gba_core.h"
#include <stdlib.h>
#include <string.h>

static uint8_t bios_buf[0x4000];
static uint8_t ewram_buf[0x40000];
static uint8_t iwram_buf[0x8000];
static uint8_t io_buf[0x400];
static uint8_t pram_buf[0x400];
static uint8_t vram_buf[0x18000];
static uint8_t oam_buf[0x400];

static inline uint32_t rgb555_to_abgr8888(uint16_t c) {
    uint32_t r = ((c & 0x001Fu) * 255u) / 31u;
    uint32_t g = (((c >> 5) & 0x001Fu) * 255u) / 31u;
    uint32_t b = (((c >> 10) & 0x001Fu) * 255u) / 31u;
    return 0x80000000u | (b << 16) | (g << 8) | r;
}

static void fill_diagnostic_bars(GBA_State *state) {
    /* Barras de teste quando não há ROM carregada: ajuda a validar vídeo/USB */
    static const uint32_t bars[4] = {
        0x80202080u, /* vermelho escuro */
        0x80208020u, /* verde escuro */
        0x80802020u, /* azul escuro */
        0x80303030u  /* cinza neutro */
    };
    for (int y = 0; y < GBA_HEIGHT; y++) {
        int band = (y * 4) / GBA_HEIGHT;
        uint32_t px = bars[band & 3];
        uint32_t *row = &state->framebuffer[y * GBA_WIDTH];
        for (int x = 0; x < GBA_WIDTH; x++) {
            row[x] = px;
        }
    }
}

void GBA_Init(GBA_State *state) {
    if (!state) return;

    memset(state, 0, sizeof(*state));
    memset(bios_buf, 0, sizeof(bios_buf));
    memset(ewram_buf, 0, sizeof(ewram_buf));
    memset(iwram_buf, 0, sizeof(iwram_buf));
    memset(io_buf, 0, sizeof(io_buf));
    memset(pram_buf, 0, sizeof(pram_buf));
    memset(vram_buf, 0, sizeof(vram_buf));
    memset(oam_buf, 0, sizeof(oam_buf));

    state->bios = bios_buf;
    state->ewram = ewram_buf;
    state->iwram = iwram_buf;
    state->io = io_buf;
    state->pram = pram_buf;
    state->vram = vram_buf;
    state->oam = oam_buf;
    state->keys = 0x03FFu;
    state->regs[15] = 0x08000000u;
    state->cpsr = 0x1Fu;

    fill_diagnostic_bars(state);
}

void GBA_LoadROM(GBA_State *state, const uint8_t *data, size_t size) {
    if (!state || !data || size == 0) return;

    if (state->rom) {
        free(state->rom);
        state->rom = NULL;
        state->rom_size = 0;
        state->rom_loaded = 0;
    }

    state->rom = (uint8_t *)malloc(size);
    if (!state->rom) return;

    memcpy(state->rom, data, size);
    state->rom_size = size;
    state->rom_loaded = 1;
    state->regs[15] = 0x08000000u;

    memset(state->framebuffer, 0, sizeof(state->framebuffer));
}

void GBA_SetKeys(GBA_State *state, uint16_t keys) {
    if (!state) return;
    state->keys = keys;
}

void GBA_RunFrame(GBA_State *state) {
    if (!state) return;

    if (!state->rom_loaded || !state->rom) {
        fill_diagnostic_bars(state);
        return;
    }

    /* Renderização simples do backdrop (cor 0 da paleta BG) enquanto expandimos CPU/PPU */
    uint16_t bg0_col = (uint16_t)state->pram[0] | ((uint16_t)state->pram[1] << 8);
    uint32_t out_col = (bg0_col != 0) ? rgb555_to_abgr8888(bg0_col) : 0x80101018u;

    for (int y = 0; y < GBA_HEIGHT; y++) {
        uint32_t *row = &state->framebuffer[y * GBA_WIDTH];
        for (int x = 0; x < GBA_WIDTH; x++) {
            row[x] = out_col;
        }
    }
}
