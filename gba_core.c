#include "gba_core.h"
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

/* Flags CPSR */
#define FLAG_N (1u << 31)
#define FLAG_Z (1u << 30)
#define FLAG_C (1u << 29)
#define FLAG_V (1u << 28)
#define THUMB_BIT (1u << 5)
#define IRQ_DISABLE_BIT (1u << 7)

static inline uint32_t bgr555_to_abgr8888(uint16_t c) {
    uint32_t r = ((c & 0x001Fu) * 255u) / 31u;
    uint32_t g = (((c >> 5) & 0x001Fu) * 255u) / 31u;
    uint32_t b = (((c >> 10) & 0x001Fu) * 255u) / 31u;
    return 0x80000000u | (b << 16) | (g << 8) | r;
}

static inline uint16_t read_io16(GBA_State *gba, uint32_t off) {
    off &= 0x3FEu;
    return (uint16_t)(gba->io[off] | (gba->io[off + 1] << 8));
}

static inline void write_io16(GBA_State *gba, uint32_t off, uint16_t val) {
    off &= 0x3FEu;
    gba->io[off] = (uint8_t)(val & 0xFFu);
    gba->io[off + 1] = (uint8_t)((val >> 8) & 0xFFu);
}

static void run_dma(GBA_State *gba, int ch) {
    uint32_t base = 0x0B0u + (uint32_t)ch * 12u;
    uint32_t sad = read_io16(gba, base + 0) | ((uint32_t)read_io16(gba, base + 2) << 16);
    uint32_t dad = read_io16(gba, base + 4) | ((uint32_t)read_io16(gba, base + 6) << 16);
    uint32_t cnt_l = read_io16(gba, base + 8);
    uint32_t cnt_h = read_io16(gba, base + 10);

    if (!(cnt_h & 0x8000u)) return; /* DMA não habilitado */
    uint32_t timing = (cnt_h >> 12) & 0x3u;
    if (timing != 0u) return; /* Foco em transferência imediata */

    uint32_t count = cnt_l ? cnt_l : (ch == 3 ? 0x10000u : 0x4000u);
    int word32 = (cnt_h & 0x0400u) != 0;
    int dst_mode = (cnt_h >> 5) & 0x3u;
    int src_mode = (cnt_h >> 7) & 0x3u;

    int s_step = word32 ? 4 : 2;
    int d_step = word32 ? 4 : 2;

    int s_inc = (src_mode == 0) ? s_step : (src_mode == 1) ? -s_step : 0;
    int d_inc = (dst_mode == 0 || dst_mode == 3) ? d_step : (dst_mode == 1) ? -d_step : 0;

    for (uint32_t i = 0; i < count; i++) {
        if (word32) {
            GBA_Write32(gba, dad, GBA_Read32(gba, sad));
        } else {
            GBA_Write16(gba, dad, GBA_Read16(gba, sad));
        }
        sad = (uint32_t)((int32_t)sad + s_inc);
        dad = (uint32_t)((int32_t)dad + d_inc);
    }

    if (!(cnt_h & 0x0200u)) { /* Sem repeat */
        write_io16(gba, base + 10, (uint16_t)(cnt_h & ~0x8000u));
    }
}

uint8_t GBA_Read8(GBA_State *gba, uint32_t addr) {
    switch (addr >> 24) {
        case 0x00: return gba->bios[addr & 0x3FFFu];
        case 0x02: return gba->ewram[addr & 0x3FFFFu];
        case 0x03: return gba->iwram[addr & 0x7FFFu];
        case 0x04: return gba->io[addr & 0x3FFu];
        case 0x05: return gba->palette[addr & 0x3FFu];
        case 0x06: return gba->vram[addr % 0x18000u];
        case 0x07: return gba->oam[addr & 0x3FFu];
        case 0x08: case 0x09:
        case 0x0A: case 0x0B:
        case 0x0C: case 0x0D: {
            uint32_t off = addr & 0x01FFFFFFu;
            if (gba->rom && off < gba->rom_size) return gba->rom[off];
            return 0;
        }
        default: return 0;
    }
}

uint16_t GBA_Read16(GBA_State *gba, uint32_t addr) {
    addr &= ~1u;
    return (uint16_t)(GBA_Read8(gba, addr) | (GBA_Read8(gba, addr + 1) << 8));
}

uint32_t GBA_Read32(GBA_State *gba, uint32_t addr) {
    addr &= ~3u;
    return (uint32_t)(GBA_Read16(gba, addr) | ((uint32_t)GBA_Read16(gba, addr + 2) << 16));
}

void GBA_Write8(GBA_State *gba, uint32_t addr, uint8_t val) {
    switch (addr >> 24) {
        case 0x02: gba->ewram[addr & 0x3FFFFu] = val; break;
        case 0x03: gba->iwram[addr & 0x7FFFu] = val; break;
        case 0x04: {
            uint32_t off = addr & 0x3FFu;
            if (off == 0x202u) { /* Escrever 1 em REG_IF limpa flags */
                gba->io[0x202u] &= (uint8_t)~val;
                gba->io[0x203u] &= (uint8_t)~val;
            } else {
                gba->io[off] = val;
            }
            break;
        }
        case 0x05: gba->palette[addr & 0x3FFu] = val; break;
        case 0x06: gba->vram[addr % 0x18000u] = val; break;
        case 0x07: gba->oam[addr & 0x3FFu] = val; break;
        default: break;
    }
}

void GBA_Write16(GBA_State *gba, uint32_t addr, uint16_t val) {
    addr &= ~1u;
    GBA_Write8(gba, addr, (uint8_t)(val & 0xFFu));
    GBA_Write8(gba, addr + 1, (uint8_t)((val >> 8) & 0xFFu));
    if ((addr >> 24) == 0x04u) {
        uint32_t off = addr & 0x3FFu;
        if (off == 0x0BAu) run_dma(gba, 0);
        else if (off == 0x0C6u) run_dma(gba, 1);
        else if (off == 0x0D2u) run_dma(gba, 2);
        else if (off == 0x0DEu) run_dma(gba, 3);
    }
}

void GBA_Write32(GBA_State *gba, uint32_t addr, uint32_t val) {
    addr &= ~3u;
    GBA_Write16(gba, addr, (uint16_t)(val & 0xFFFFu));
    GBA_Write16(gba, addr + 2, (uint16_t)((val >> 16) & 0xFFFFu));
}

static uint32_t ror32(uint32_t val, uint32_t shift) {
    shift &= 31u;
    if (!shift) return val;
    return (val >> shift) | (val << (32u - shift));
}

static int cond_passed(uint32_t cond, uint32_t cpsr) {
    uint32_t n = (cpsr & FLAG_N) != 0;
    uint32_t z = (cpsr & FLAG_Z) != 0;
    uint32_t c = (cpsr & FLAG_C) != 0;
    uint32_t v = (cpsr & FLAG_V) != 0;
    switch (cond) {
        case 0x0: return z == 1;
        case 0x1: return z == 0;
        case 0x2: return c == 1;
        case 0x3: return c == 0;
        case 0x4: return n == 1;
        case 0x5: return n == 0;
        case 0x6: return v == 1;
        case 0x7: return v == 0;
        case 0x8: return (c == 1) && (z == 0);
        case 0x9: return (c == 0) || (z == 1);
        case 0xA: return n == v;
        case 0xB: return n != v;
        case 0xC: return (z == 0) && (n == v);
        case 0xD: return (z == 1) || (n != v);
        case 0xE: return 1;
        default:  return 0;
    }
}

static void set_nz(GBA_State *gba, uint32_t res) {
    gba->cpsr &= ~(FLAG_N | FLAG_Z);
    if (res & 0x80000000u) gba->cpsr |= FLAG_N;
    if (res == 0u) gba->cpsr |= FLAG_Z;
}

static void exec_thumb(GBA_State *gba) {
    uint32_t pc = gba->r[15];
    uint16_t op = GBA_Read16(gba, pc);
    gba->r[15] = pc + 2u;

    /* BL / BLX prefixo (11110xxx) e sufixo (11111xxx) */
    if ((op & 0xF800u) == 0xF000u) {
        int32_t offset = (int32_t)(op & 0x07FFu);
        if (offset & 0x0400) offset |= ~0x07FF;
        gba->r[14] = gba->r[15] + (uint32_t)(offset << 12);
        return;
    }
    if ((op & 0xF800u) == 0xF800u) {
        uint32_t next_pc = gba->r[15] + 2u;
        uint32_t target = (gba->r[14] + ((uint32_t)(op & 0x07FFu) << 1)) & ~1u;
        gba->r[14] = next_pc | 1u;
        gba->r[15] = target;
        return;
    }

    /* Branch incondicional B label (11100xxx) */
    if ((op & 0xF800u) == 0xE000u) {
        int32_t off = (int32_t)(op & 0x07FFu);
        if (off & 0x0400) off |= ~0x07FF;
        gba->r[15] = (uint32_t)((int32_t)(gba->r[15] + 2u) + (off << 1));
        return;
    }

    /* Branch condicional B<cond> (1101xxxx, cond != 1110/1111) */
    if ((op & 0xF000u) == 0xD000u) {
        uint32_t cond = (op >> 8) & 0xFu;
        if (cond < 0xEu) {
            if (cond_passed(cond, gba->cpsr)) {
                int8_t off = (int8_t)(op & 0xFFu);
                gba->r[15] = (uint32_t)((int32_t)(gba->r[15] + 2u) + ((int32_t)off << 1));
            }
            return;
        }
    }

    /* MOV Rd, #imm8 (00100ddd) */
    if ((op & 0xF800u) == 0x2000u) {
        uint32_t rd = (op >> 8) & 7u;
        uint32_t imm = op & 0xFFu;
        gba->r[rd] = imm;
        set_nz(gba, imm);
        return;
    }

    /* CMP Rn, #imm8 (00101nnn) */
    if ((op & 0xF800u) == 0x2800u) {
        uint32_t rn = (op >> 8) & 7u;
        uint32_t imm = op & 0xFFu;
        uint32_t a = gba->r[rn];
        uint32_t res = a - imm;
        set_nz(gba, res);
        gba->cpsr &= ~(FLAG_C | FLAG_V);
        if (a >= imm) gba->cpsr |= FLAG_C;
        if (((a ^ imm) & (a ^ res)) & 0x80000000u) gba->cpsr |= FLAG_V;
        return;
    }

    /* ADD Rd, #imm8 (00110ddd) */
    if ((op & 0xF800u) == 0x3000u) {
        uint32_t rd = (op >> 8) & 7u;
        uint32_t imm = op & 0xFFu;
        uint32_t a = gba->r[rd];
        uint32_t res = a + imm;
        gba->r[rd] = res;
        set_nz(gba, res);
        gba->cpsr &= ~(FLAG_C | FLAG_V);
        if (res < a) gba->cpsr |= FLAG_C;
        if (~(a ^ imm) & (a ^ res) & 0x80000000u) gba->cpsr |= FLAG_V;
        return;
    }

    /* SUB Rd, #imm8 (00111ddd) */
    if ((op & 0xF800u) == 0x3800u) {
        uint32_t rd = (op >> 8) & 7u;
        uint32_t imm = op & 0xFFu;
        uint32_t a = gba->r[rd];
        uint32_t res = a - imm;
        gba->r[rd] = res;
        set_nz(gba, res);
        gba->cpsr &= ~(FLAG_C | FLAG_V);
        if (a >= imm) gba->cpsr |= FLAG_C;
        if (((a ^ imm) & (a ^ res)) & 0x80000000u) gba->cpsr |= FLAG_V;
        return;
    }

    /* LDR Rd, [PC, #imm8*4] (01001ddd) */
    if ((op & 0xF800u) == 0x4800u) {
        uint32_t rd = (op >> 8) & 7u;
        uint32_t base = (gba->r[15] + 2u) & ~3u;
        uint32_t off = (uint32_t)(op & 0xFFu) * 4u;
        gba->r[rd] = GBA_Read32(gba, base + off);
        return;
    }

    /* STR / LDR imediato (011xxxx) */
    if ((op & 0xE000u) == 0x6000u) {
        int is_load = (op & (1u << 11)) != 0;
        int is_byte = (op & (1u << 12)) != 0;
        uint32_t off = ((op >> 6) & 0x1Fu) * (is_byte ? 1u : 4u);
        uint32_t rn = (op >> 3) & 7u;
        uint32_t rd = op & 7u;
        uint32_t addr = gba->r[rn] + off;
        if (is_load) {
            gba->r[rd] = is_byte ? (uint32_t)GBA_Read8(gba, addr) : GBA_Read32(gba, addr);
        } else {
            if (is_byte) GBA_Write8(gba, addr, (uint8_t)gba->r[rd]);
            else GBA_Write32(gba, addr, gba->r[rd]);
        }
        return;
    }

    /* ADD SP, #imm7*4 (10110000) */
    if ((op & 0xFF00u) == 0xB000u) {
        uint32_t off = (uint32_t)(op & 0x7Fu) * 4u;
        if (op & 0x80u) gba->r[13] -= off;
        else gba->r[13] += off;
        return;
    }

    /* PUSH / POP (1011x10x) */
    if ((op & 0xF600u) == 0xB400u) {
        int is_pop = (op & (1u << 11)) != 0;
        int pclr = (op & (1u << 8)) != 0;
        uint32_t mask = op & 0xFFu;
        if (!is_pop) {
            int cnt = (pclr ? 1 : 0);
            for (int i = 0; i < 8; i++) if (mask & (1u << i)) cnt++;
            uint32_t sp = gba->r[13] - (uint32_t)(cnt * 4);
            uint32_t cur = sp;
            for (int i = 0; i < 8; i++) {
                if (mask & (1u << i)) {
                    GBA_Write32(gba, cur, gba->r[i]);
                    cur += 4u;
                }
            }
            if (pclr) GBA_Write32(gba, cur, gba->r[14]);
            gba->r[13] = sp;
        } else {
            uint32_t cur = gba->r[13];
            for (int i = 0; i < 8; i++) {
                if (mask & (1u << i)) {
                    gba->r[i] = GBA_Read32(gba, cur);
                    cur += 4u;
                }
            }
            if (pclr) {
                gba->r[15] = GBA_Read32(gba, cur) & ~1u;
                cur += 4u;
            }
            gba->r[13] = cur;
        }
        return;
    }

    /* BX Rm (010001110mmmm000) */
    if ((op & 0xFF87u) == 0x4700u) {
        uint32_t rm = (op >> 3) & 0xFu;
        uint32_t target = gba->r[rm];
        if (target & 1u) {
            gba->cpsr |= THUMB_BIT;
            gba->r[15] = target & ~1u;
        } else {
            gba->cpsr &= ~THUMB_BIT;
            gba->r[15] = target & ~3u;
        }
        return;
    }

    /* MOV Rd, Rs (0001110000sssddd) */
    if ((op & 0xFFC0u) == 0x1C00u) {
        uint32_t rs = (op >> 3) & 7u;
        uint32_t rd = op & 7u;
        gba->r[rd] = gba->r[rs];
        set_nz(gba, gba->r[rd]);
    }
}

static void exec_arm(GBA_State *gba) {
    uint32_t pc = gba->r[15];
    uint32_t op = GBA_Read32(gba, pc);
    gba->r[15] = pc + 4u;

    uint32_t cond = op >> 28;
    if (!cond_passed(cond, gba->cpsr)) return;

    /* Branch B / BL (101Lxxxx) */
    if ((op & 0x0E000000u) == 0x0A000000u) {
        int32_t off = (int32_t)(op & 0x00FFFFFFu);
        if (off & 0x00800000) off |= ~0x00FFFFFF;
        if (op & (1u << 24)) {
            gba->r[14] = gba->r[15];
        }
        gba->r[15] = (uint32_t)((int32_t)(gba->r[15] + 4u) + (off << 2));
        return;
    }

    /* BX Rn (xxxx000100101111111111110001nnnn) */
    if ((op & 0x0FFFFFF0u) == 0x012FFF10u) {
        uint32_t rn = op & 0xFu;
        uint32_t target = gba->r[rn];
        if (target & 1u) {
            gba->cpsr |= THUMB_BIT;
            gba->r[15] = target & ~1u;
        } else {
            gba->cpsr &= ~THUMB_BIT;
            gba->r[15] = target & ~3u;
        }
        return;
    }

    /* Data processing imediato (001xxxx) */
    if ((op & 0x0E000000u) == 0x02000000u) {
        uint32_t opcode = (op >> 21) & 0xFu;
        int s_bit = (op & (1u << 20)) != 0;
        uint32_t rn = (op >> 16) & 0xFu;
        uint32_t rd = (op >> 12) & 0xFu;
        uint32_t rot = ((op >> 8) & 0xFu) * 2u;
        uint32_t imm = ror32(op & 0xFFu, rot);
        uint32_t a = gba->r[rn];

        switch (opcode) {
            case 0x4: { /* ADD */
                uint32_t res = a + imm;
                gba->r[rd] = res;
                if (s_bit) {
                    set_nz(gba, res);
                    gba->cpsr &= ~(FLAG_C | FLAG_V);
                    if (res < a) gba->cpsr |= FLAG_C;
                    if (~(a ^ imm) & (a ^ res) & 0x80000000u) gba->cpsr |= FLAG_V;
                }
                break;
            }
            case 0xA: { /* CMP */
                uint32_t res = a - imm;
                set_nz(gba, res);
                gba->cpsr &= ~(FLAG_C | FLAG_V);
                if (a >= imm) gba->cpsr |= FLAG_C;
                if (((a ^ imm) & (a ^ res)) & 0x80000000u) gba->cpsr |= FLAG_V;
                break;
            }
            case 0xD: { /* MOV */
                gba->r[rd] = imm;
                if (s_bit) set_nz(gba, imm);
                break;
            }
            default: break;
        }
        return;
    }

    /* LDR / STR imediato (010xxxx) */
    if ((op & 0x0E000000u) == 0x04000000u) {
        int pre = (op & (1u << 24)) != 0;
        int up = (op & (1u << 23)) != 0;
        int byte_x = (op & (1u << 22)) != 0;
        int wb = (op & (1u << 21)) != 0;
        int load = (op & (1u << 20)) != 0;
        uint32_t rn = (op >> 16) & 0xFu;
        uint32_t rd = (op >> 12) & 0xFu;
        uint32_t off = op & 0xFFFu;

        uint32_t base = gba->r[rn];
        uint32_t addr = pre ? (up ? base + off : base - off) : base;

        if (load) {
            gba->r[rd] = byte_x ? (uint32_t)GBA_Read8(gba, addr) : GBA_Read32(gba, addr);
        } else {
            if (byte_x) GBA_Write8(gba, addr, (uint8_t)gba->r[rd]);
            else GBA_Write32(gba, addr, gba->r[rd]);
        }

        if (!pre) {
            gba->r[rn] = up ? base + off : base - off;
        } else if (wb) {
            gba->r[rn] = addr;
        }
    }
}

static void render_scanline(GBA_State *gba, int y) {
    uint16_t dispcnt = read_io16(gba, 0x000u);
    uint32_t *line = &gba->framebuffer[(size_t)y * GBA_WIDTH];
    uint16_t backdrop = (uint16_t)(gba->palette[0] | (gba->palette[1] << 8));
    uint32_t bg_col = bgr555_to_abgr8888(backdrop);

    uint32_t mode = dispcnt & 0x7u;

    /* Mode 3: 240x160 15-bit bitmap direto na VRAM */
    if (mode == 3u) {
        const uint16_t *v16 = (const uint16_t *)gba->vram;
        for (int x = 0; x < GBA_WIDTH; x++) {
            line[x] = bgr555_to_abgr8888(v16[y * GBA_WIDTH + x]);
        }
        return;
    }

    /* Mode 4: 240x160 8-bit paletizado (2 frames em VRAM) */
    if (mode == 4u) {
        const uint8_t *page = gba->vram + ((dispcnt & (1u << 4)) ? 0xA000u : 0x0000u);
        const uint16_t *pal = (const uint16_t *)gba->palette;
        for (int x = 0; x < GBA_WIDTH; x++) {
            uint8_t idx = page[y * GBA_WIDTH + x];
            line[x] = bgr555_to_abgr8888(idx ? pal[idx] : backdrop);
        }
        return;
    }

    /* Limpa scanline com cor de fundo (backdrop) */
    for (int x = 0; x < GBA_WIDTH; x++) {
        line[x] = bg_col;
    }

    /* Mode 0 / 1 / 2: Backgrounds em tiles 8x8 de 4bpp ou 8bpp */
    const uint16_t *pal = (const uint16_t *)gba->palette;
    for (int bg = 3; bg >= 0; bg--) {
        if (!(dispcnt & (1u << (8 + bg)))) continue;
        if (bg >= 2 && mode == 0u) {
            /* BG2/BG3 texto padrão no Mode 0 */
        } else if (bg >= 2 && mode != 0u) {
            continue;
        }

        uint16_t bgcnt = read_io16(gba, 0x008u + (uint32_t)bg * 2u);
        uint16_t hofs = read_io16(gba, 0x010u + (uint32_t)bg * 4u) & 0x1FFu;
        uint16_t vofs = read_io16(gba, 0x012u + (uint32_t)bg * 4u) & 0x1FFu;

        int is_8bpp = (bgcnt & (1u << 7)) != 0;
        uint32_t char_base = ((bgcnt >> 2) & 0x3u) * 0x4000u;
        uint32_t map_base  = ((bgcnt >> 8) & 0x1Fu) * 0x0800u;
        uint32_t sz = (bgcnt >> 14) & 0x3u;

        uint32_t map_w_tiles = (sz & 1u) ? 64u : 32u;
        uint32_t map_h_tiles = (sz & 2u) ? 64u : 32u;

        uint32_t py = ((uint32_t)y + vofs) & (map_h_tiles * 8u - 1u);
        uint32_t ty = py / 8u;
        uint32_t fine_y = py % 8u;

        for (int x = 0; x < GBA_WIDTH; x++) {
            uint32_t px = ((uint32_t)x + hofs) & (map_w_tiles * 8u - 1u);
            uint32_t tx = px / 8u;
            uint32_t fine_x = px % 8u;

            uint32_t sbb_offset = 0u;
            if (tx >= 32u) sbb_offset += 0x0800u;
            if (ty >= 32u) sbb_offset += (map_w_tiles == 64u) ? 0x1000u : 0x0800u;

            uint32_t entry_addr = map_base + sbb_offset + (((ty & 31u) * 32u + (tx & 31u)) * 2u);
            if (entry_addr + 1u >= 0x18000u) continue;

            uint16_t entry = (uint16_t)(gba->vram[entry_addr] | (gba->vram[entry_addr + 1u] << 8));
            uint32_t tile_id = entry & 0x03FFu;
            int hflip = (entry & (1u << 10)) != 0;
            int vflip = (entry & (1u << 11)) != 0;
            uint32_t pal_bank = (entry >> 12) & 0xFu;

            uint32_t sy = vflip ? (7u - fine_y) : fine_y;
            uint32_t sx = hflip ? (7u - fine_x) : fine_x;

            if (!is_8bpp) {
                uint32_t t_addr = char_base + tile_id * 32u + sy * 4u + (sx / 2u);
                if (t_addr >= 0x18000u) continue;
                uint8_t b = gba->vram[t_addr];
                uint8_t ci = (sx & 1u) ? (b >> 4) : (b & 0x0Fu);
                if (ci != 0u) {
                    line[x] = bgr555_to_abgr8888(pal[pal_bank * 16u + ci]);
                }
            } else {
                uint32_t t_addr = char_base + tile_id * 64u + sy * 8u + sx;
                if (t_addr >= 0x18000u) continue;
                uint8_t ci = gba->vram[t_addr];
                if (ci != 0u) {
                    line[x] = bgr555_to_abgr8888(pal[ci]);
                }
            }
        }
    }

    /* Sprites básicos (OBJ) se habilitados em DISPCNT bit 12 */
    if (dispcnt & (1u << 12)) {
        const uint16_t *oam16 = (const uint16_t *)gba->oam;
        const uint16_t *obj_pal = (const uint16_t *)(gba->palette + 0x200u);
        for (int i = 127; i >= 0; i--) {
            uint16_t a0 = oam16[i * 4 + 0];
            uint16_t a1 = oam16[i * 4 + 1];
            uint16_t a2 = oam16[i * 4 + 2];

            int obj_mode = (a0 >> 8) & 0x3u;
            if (obj_mode == 2u) continue; /* OBJ escondido */

            int sy_pos = a0 & 0xFFu;
            if (sy_pos >= 160) sy_pos -= 256;

            int shape = (a0 >> 14) & 0x3u;
            int size = (a1 >> 14) & 0x3u;
            int w = 8, h = 8;
            if (shape == 0) {
                w = h = (size == 0) ? 8 : (size == 1) ? 16 : (size == 2) ? 32 : 64;
            } else if (shape == 1) {
                w = (size == 0) ? 16 : (size == 1) ? 32 : (size == 2) ? 64 : 64;
                h = (size == 0) ? 8  : (size == 1) ? 8  : (size == 2) ? 32 : 32;
            } else if (shape == 2) {
                w = (size == 0) ? 8  : (size == 1) ? 8  : (size == 2) ? 16 : 32;
                h = (size == 0) ? 16 : (size == 1) ? 32 : (size == 2) ? 32 : 64;
            }

            if (y < sy_pos || y >= sy_pos + h) continue;

            int sx_pos = a1 & 0x1FFu;
            if (sx_pos >= 240) sx_pos -= 512;

            int hflip = (a1 & (1u << 12)) != 0;
            int vflip = (a1 & (1u << 13)) != 0;
            int is_8bpp = (a0 & (1u << 13)) != 0;
            uint32_t tile = a2 & 0x3FFu;
            uint32_t pbank = (a2 >> 12) & 0xFu;

            uint32_t local_y = (uint32_t)(y - sy_pos);
            if (vflip) local_y = (uint32_t)(h - 1 - (int)local_y);

            for (int lx = 0; lx < w; lx++) {
                int px = sx_pos + lx;
                if (px < 0 || px >= GBA_WIDTH) continue;
                uint32_t local_x = (uint32_t)(hflip ? (w - 1 - lx) : lx);

                uint32_t tx = local_x / 8u;
                uint32_t ty = local_y / 8u;
                uint32_t fx = local_x % 8u;
                uint32_t fy = local_y % 8u;

                uint32_t t_idx = tile + ty * (is_8bpp ? 16u : 32u) + tx * (is_8bpp ? 2u : 1u);
                uint32_t addr = 0x10000u + t_idx * 32u;
                if (!is_8bpp) {
                    addr += fy * 4u + (fx / 2u);
                    if (addr >= 0x18000u) continue;
                    uint8_t b = gba->vram[addr];
                    uint8_t ci = (fx & 1u) ? (b >> 4) : (b & 0x0Fu);
                    if (ci != 0u) line[px] = bgr555_to_abgr8888(obj_pal[pbank * 16u + ci]);
                } else {
                    addr += fy * 8u + fx;
                    if (addr >= 0x18000u) continue;
                    uint8_t ci = gba->vram[addr];
                    if (ci != 0u) line[px] = bgr555_to_abgr8888(obj_pal[ci]);
                }
            }
        }
    }
}

void GBA_Init(GBA_State *gba) {
    memset(gba, 0, sizeof(*gba));
    gba->bios    = (uint8_t *)memalign(64, 0x4000u);
    gba->ewram   = (uint8_t *)memalign(64, 0x40000u);
    gba->iwram   = (uint8_t *)memalign(64, 0x8000u);
    gba->io      = (uint8_t *)memalign(64, 0x400u);
    gba->palette = (uint8_t *)memalign(64, 0x400u);
    gba->vram    = (uint8_t *)memalign(64, 0x18000u);
    gba->oam     = (uint8_t *)memalign(64, 0x400u);

    if (gba->bios)    memset(gba->bios, 0, 0x4000u);
    if (gba->ewram)   memset(gba->ewram, 0, 0x40000u);
    if (gba->iwram)   memset(gba->iwram, 0, 0x8000u);
    if (gba->io)      memset(gba->io, 0, 0x400u);
    if (gba->palette) memset(gba->palette, 0, 0x400u);
    if (gba->vram)    memset(gba->vram, 0, 0x18000u);
    if (gba->oam)     memset(gba->oam, 0, 0x400u);

    /* Todos os botões soltos inicialmente (bit 0..9 = 1) */
    write_io16(gba, 0x130u, 0x03FFu);
    gba->r[13] = 0x03007F00u; /* SP inicial em IWRAM */
    gba->r[15] = 0x08000000u; /* Início da ROM GBA */
    gba->cpsr  = 0x0000001Fu; /* System mode, ARM */
}

void GBA_Free(GBA_State *gba) {
    free(gba->bios);
    free(gba->ewram);
    free(gba->iwram);
    free(gba->io);
    free(gba->palette);
    free(gba->vram);
    free(gba->oam);
    free(gba->rom);
    memset(gba, 0, sizeof(*gba));
}

int GBA_LoadROM(GBA_State *gba, const uint8_t *data, size_t size) {
    if (!data || size == 0 || size > 0x2000000u) return 0;
    if (gba->rom) free(gba->rom);

    gba->rom = (uint8_t *)memalign(64, size);
    if (!gba->rom) return 0;

    memcpy(gba->rom, data, size);
    gba->rom_size = size;
    gba->rom_loaded = 1;

    /* Reset do PC para o ponto de entrada da ROM */
    gba->r[13] = 0x03007F00u;
    gba->r[15] = 0x08000000u;
    gba->cpsr  = 0x0000001Fu;
    return 1;
}

void GBA_SetKeys(GBA_State *gba, uint16_t keyinput) {
    write_io16(gba, 0x130u, keyinput & 0x03FFu);
}

void GBA_RunFrame(GBA_State *gba) {
    if (!gba->rom_loaded || !gba->rom) {
        /* Padrão visual simples caso a ROM não seja encontrada no USB */
        for (int y = 0; y < GBA_HEIGHT; y++) {
            for (int x = 0; x < GBA_WIDTH; x++) {
                uint32_t v = (((uint32_t)x ^ (uint32_t)y) & 16u) ? 0xFF401010u : 0xFF101040u;
                gba->framebuffer[y * GBA_WIDTH + x] = v;
            }
        }
        return;
    }

    /* 228 scanlines visíveis + VBlank (total 228 linhas por quadro simplificado) */
    for (int line = 0; line < 228; line++) {
        write_io16(gba, 0x006u, (uint16_t)line); /* REG_VCOUNT */
        uint16_t dispstat = read_io16(gba, 0x004u);
        if (line >= 160) dispstat |= 0x0001u;
        else dispstat &= (uint16_t)~0x0001u;
        write_io16(gba, 0x004u, dispstat);

        int steps = (line < 160) ? 160 : 60;
        for (int s = 0; s < steps; s++) {
            if (gba->cpsr & THUMB_BIT) exec_thumb(gba);
            else exec_arm(gba);
            gba->cycles++;
        }

        if (line < GBA_HEIGHT) {
            render_scanline(gba, line);
        }
    }
}
