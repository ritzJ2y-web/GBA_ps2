#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <tamtypes.h>
#include <sifrpc.h>
#include <kernel.h>
#include <loadfile.h>
#include <iopcontrol.h>
#include <libpad.h>
#include <sbv_patches.h>
#include <gsKit.h>
#include <dmaKit.h>
#include "gba_core.h"

extern unsigned char usbd_irx[];
extern unsigned int size_usbd_irx;
extern unsigned char bdm_irx[];
extern unsigned int size_bdm_irx;
extern unsigned char bdmfs_fatfs_irx[];
extern unsigned int size_bdmfs_fatfs_irx;
extern unsigned char usbmass_bd_irx[];
extern unsigned int size_usbmass_bd_irx;

static char *padBuf = NULL;
static GSGLOBAL *gsGlobal = NULL;
static GSTEXTURE gbaTex;
static GBA_State gba;

static void init_iop_usb_pad(void) {
    SifInitRpc(0);
    while (!SifIopReset("", 0)) {}
    while (!SifIopSync()) {}
    SifInitRpc(0);

    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();

    SifLoadModule("rom0:SIO2MAN", 0, NULL);
    SifLoadModule("rom0:PADMAN", 0, NULL);

    SifExecModuleBuffer(usbd_irx, size_usbd_irx, 0, NULL, NULL);
    SifExecModuleBuffer(bdm_irx, size_bdm_irx, 0, NULL, NULL);
    SifExecModuleBuffer(bdmfs_fatfs_irx, size_bdmfs_fatfs_irx, 0, NULL, NULL);
    SifExecModuleBuffer(usbmass_bd_irx, size_usbmass_bd_irx, 0, NULL, NULL);

    padBuf = (char *)memalign(64, 256);
    if (padBuf) {
        padInit(0);
        padPortOpen(0, 0, padBuf);
    }
}

static void init_video_hdmi_safe(void) {
    dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gsGlobal = gsKit_init_global();
    if (!gsGlobal) return;

    /* Resolução estável e compatível com a maioria dos adaptadores PS2 to HDMI */
    gsGlobal->Mode = GS_MODE_NTSC;
    gsGlobal->Width = 640;
    gsGlobal->Height = 448;
    gsGlobal->Interlace = GS_INTERLACED;
    gsGlobal->Field = GS_FIELD;
    gsGlobal->PSM = GS_PSM_CT32;
    gsGlobal->PSMZ = GS_PSMZ_16S;
    gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
    gsGlobal->DoubleBuffering = GS_SETTING_ON;
    gsGlobal->ZBuffering = GS_SETTING_OFF;

    gsKit_init_screen(gsGlobal);
    gsKit_mode_switch(gsGlobal, GS_ONESHOT);

    memset(&gbaTex, 0, sizeof(GSTEXTURE));
    gbaTex.Width = GBA_WIDTH;
    gbaTex.Height = GBA_HEIGHT;
    gbaTex.PSM = GS_PSM_CT32;
    gbaTex.Filter = GS_FILTER_NEAREST;
    gbaTex.Mem = gba.framebuffer;
    gbaTex.Vram = gsKit_vram_alloc(gsGlobal, gsKit_texture_size(GBA_WIDTH, GBA_HEIGHT, GS_PSM_CT32), GSKIT_ALLOC_USERBUFFER);
    if (gbaTex.Vram == GSKIT_ALLOC_ERROR) {
        gbaTex.Vram = 0;
    }
}

static uint16_t read_ps2_pad_as_gba_keys(void) {
    uint16_t keys = 0x03FFu; /* 0 = pressionado, 1 = solto */
    struct padButtonStatus btns;

    if (!padBuf) return keys;
    int st = padGetState(0, 0);
    if (st == PAD_STATE_STABLE || st == PAD_STATE_FINDCTP1) {
        if (padRead(0, 0, &btns) != 0) {
            uint32_t p = 0xFFFFu ^ btns.btns;
            if (p & PAD_CROSS)  keys &= ~(1u << 0); /* A */
            if (p & PAD_CIRCLE) keys &= ~(1u << 1); /* B */
            if (p & PAD_SELECT) keys &= ~(1u << 2); /* Select */
            if (p & PAD_START)  keys &= ~(1u << 3); /* Start */
            if (p & PAD_RIGHT)  keys &= ~(1u << 4); /* Right */
            if (p & PAD_LEFT)   keys &= ~(1u << 5); /* Left */
            if (p & PAD_UP)     keys &= ~(1u << 6); /* Up */
            if (p & PAD_DOWN)   keys &= ~(1u << 7); /* Down */
            if (p & PAD_R1)     keys &= ~(1u << 8); /* R */
            if (p & PAD_L1)     keys &= ~(1u << 9); /* L */

            if (btns.ljoy_h < 60)  keys &= ~(1u << 5);
            if (btns.ljoy_h > 195) keys &= ~(1u << 4);
            if (btns.ljoy_v < 60)  keys &= ~(1u << 6);
            if (btns.ljoy_v > 195) keys &= ~(1u << 7);
        }
    }
    return keys;
}

static void try_load_rom_from_usb(GBA_State *state) {
    FILE *f = NULL;
    for (int i = 0; i < 20; i++) {
        f = fopen("mass:/minish.gba", "rb");
        if (f != NULL) break;
        usleep(150000);
    }
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz > 0 && sz <= 0x2000000L) {
        uint8_t *buf = (uint8_t *)memalign(64, (size_t)sz);
        if (buf) {
            size_t rd = fread(buf, 1, (size_t)sz, f);
            if (rd > 0) {
                GBA_LoadROM(state, buf, rd);
            }
            free(buf);
        }
    }
    fclose(f);
}

static void present_frame(void) {
    if (!gsGlobal) return;

    gbaTex.Mem = gba.framebuffer;
    gsKit_texture_upload(gsGlobal, &gbaTex);
    gsKit_clear(gsGlobal, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x00, 0x00));
    gsKit_prim_sprite_texture(
        gsGlobal, &gbaTex,
        0.0f, 0.0f, 0.0f, 0.0f,
        (float)gsGlobal->Width, (float)gsGlobal->Height,
        (float)GBA_WIDTH, (float)GBA_HEIGHT,
        1, GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00)
    );
    gsKit_queue_exec(gsGlobal);
    gsKit_sync_flip(gsGlobal);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    GBA_Init(&gba);
    init_iop_usb_pad();
    init_video_hdmi_safe();
    try_load_rom_from_usb(&gba);

    while (1) {
        GBA_SetKeys(&gba, read_ps2_pad_as_gba_keys());
        GBA_RunFrame(&gba);
        present_frame();
    }

    return 0;
}
