#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspge.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"
#include "bg_arena.h"
#include "world_sprites.h"

PSP_MODULE_INFO("SymphonyAbyss", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define HIT_Y 238
#define RGB(r,g,b) ((unsigned int)(0xFF000000u | ((unsigned int)(b)<<16) | ((unsigned int)(g)<<8) | (unsigned int)(r)))

static unsigned int __attribute__((aligned(16))) gulist[262144];
static unsigned int *vram_bufs[2];
static int cur_fb = 0;

static int running = 1;
static unsigned int rng = 0x77113355u;
static unsigned int xr(void) { unsigned int x = rng; x ^= x<<13; x ^= x>>17; x ^= x<<5; rng = x; return x; }
static int exit_cb(int a, int b, void *c) { (void)a; (void)b; (void)c; running = 0; return 0; }
static int cb_th(SceSize a, void *p) { (void)a; (void)p; sceKernelRegisterExitCallback(sceKernelCreateCallback("Exit", exit_cb, NULL)); sceKernelSleepThreadCB(); return 0; }

/* 2D Primitives & 32-bit Sprite Blitters */
static void put_px(unsigned int *fb, int x, int y, unsigned int c) {
    if ((unsigned)x < SCR_W && (unsigned)y < SCR_H) fb[y * BUF_W + x] = c;
}

static void fill_rect(unsigned int *fb, int x, int y, int w, int h, unsigned int col) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x;
    if (y + h > SCR_H) h = SCR_H - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        unsigned int *row = &fb[(y + j) * BUF_W + x];
        for (int i = 0; i < w; i++) row[i] = col;
    }
}

static void draw_border(unsigned int *fb, int x, int y, int w, int h, unsigned int col) {
    fill_rect(fb, x, y, w, 1, col);
    fill_rect(fb, x, y + h - 1, w, 1, col);
    fill_rect(fb, x, y, 1, h, col);
    fill_rect(fb, x + w - 1, y, 1, h, col);
}

/* Classic Final Fantasy Blue Window with double border */
static void draw_ff_window(unsigned int *fb, int x, int y, int w, int h) {
    fill_rect(fb, x, y, w, h, RGB(14, 22, 72));
    draw_border(fb, x, y, w, h, RGB(230, 235, 255));
    draw_border(fb, x + 2, y + 2, w - 4, h - 4, RGB(160, 180, 230));
}

static void draw_text(unsigned int *fb, int x, int y, const char *s, unsigned int c) {
    while (*s) {
        unsigned char ch = (unsigned char)*s++;
        if (ch < 32 || ch > 127) ch = '?';
        const unsigned char *g = font5x7[ch - 32];
        for (int cx = 0; cx < 5; cx++) {
            unsigned char b = g[cx];
            for (int cy = 0; cy < 7; cy++) {
                if (b & (1 << cy)) {
                    put_px(fb, x + cx + 1, y + cy + 1, RGB(0, 0, 0)); /* drop shadow */
                    put_px(fb, x + cx, y + cy, c);
                }
            }
        }
        x += 6;
    }
}

static void draw_text_2x(unsigned int *fb, int x, int y, const char *s, unsigned int c) {
    while (*s) {
        unsigned char ch = (unsigned char)*s++;
        if (ch < 32 || ch > 127) ch = '?';
        const unsigned char *g = font5x7[ch - 32];
        for (int cx = 0; cx < 5; cx++) {
            unsigned char b = g[cx];
            for (int cy = 0; cy < 7; cy++) {
                if (b & (1 << cy)) {
                    fill_rect(fb, x + cx * 2 + 2, y + cy * 2 + 2, 2, 2, RGB(0, 0, 0));
                    fill_rect(fb, x + cx * 2, y + cy * 2, 2, 2, c);
                }
            }
        }
        x += 12;
    }
}

static void draw_tile24(unsigned int *fb, const unsigned int *tile, int dx, int dy) {
    for (int j = 0; j < 24; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &tile[j * 24];
        for (int i = 0; i < 24; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) dst[px] = src[i];
        }
    }
}

static void draw_spr16(unsigned int *fb, const unsigned int *spr, int dx, int dy) {
    for (int j = 0; j < 16; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &spr[j * 16];
        for (int i = 0; i < 16; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[i];
                if (c) dst[px] = c;
            }
        }
    }
}

static void draw_spr20(unsigned int *fb, const unsigned int *spr, int dx, int dy) {
    for (int j = 0; j < 20; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &spr[j * 20];
        for (int i = 0; i < 20; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[i];
                if (c) dst[px] = c;
            }
        }
    }
}

static void draw_spr_lr24(unsigned int *fb, int dx, int dy) {
    for (int j = 0; j < 14; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &spr_btn_lr24[j * 24];
        for (int i = 0; i < 24; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[i];
                if (c) dst[px] = c;
            }
        }
    }
}

static void draw_spr24_dir(unsigned int *fb, const unsigned int *spr, int dx, int dy, int flipX) {
    for (int j = 0; j < 24; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &spr[j * 24];
        for (int i = 0; i < 24; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[flipX ? (23 - i) : i];
                if (c) dst[px] = c;
            }
        }
    }
}

static void draw_spr24(unsigned int *fb, const unsigned int *spr, int dx, int dy) {
    draw_spr24_dir(fb, spr, dx, dy, 0);
}

static void draw_spr48(unsigned int *fb, const unsigned int *spr, int dx, int dy) {
    for (int j = 0; j < 48; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &spr[j * 48];
        for (int i = 0; i < 48; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[i];
                if (c) dst[px] = c;
            }
        }
    }
}

static void draw_ellipse_ring(unsigned int *fb, int cx, int cy, int rx, int ry, unsigned int col) {
    for (int deg = 0; deg < 360; deg += 4) {
        float rad = (float)deg * 0.01745329f;
        int px = cx + (int)(cosf(rad) * (float)rx);
        int py = cy + (int)(sinf(rad) * (float)ry);
        put_px(fb, px, py, col);
        put_px(fb, px, py + 1, col);
    }
}

static void draw_boss64_dir(unsigned int *fb, const unsigned int *spr, int dx, int dy, int flash, int flipX) {
    for (int j = 0; j < 64; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &spr[j * 64];
        for (int i = 0; i < 64; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[flipX ? (63 - i) : i];
                if (c) dst[px] = flash ? RGB(255, 255, 255) : c;
            }
        }
    }
}

static void draw_boss64(unsigned int *fb, const unsigned int *spr, int dx, int dy, int flash) {
    draw_boss64_dir(fb, spr, dx, dy, flash, 0);
}

/* Audio: 3 styles (0=Rock, 1=Synth, 2=Symphonic) */
static int ach = -1, ath = -1;
static volatile int arun = 0, sfx_t = 0, sfx_on = 1, curStyle = 0;
static float sfx_f = 440.0f, sfx_p = 0.0f, bgm_t = 0.0f;
static short abuf[2][1024 * 2];
static void play_sfx(float f, int dur) { if (sfx_on) { sfx_f = f; sfx_t = dur; sfx_p = 0.0f; } }

static int audio_th(SceSize a, void *p) {
    (void)a; (void)p;
    int cur = 0;
    const float notes_seq[3][8] = {
        { 82.41f, 82.41f, 98.00f, 110.00f, 82.41f, 123.47f, 110.00f, 98.00f }, /* Rock */
        { 130.81f, 196.00f, 261.63f, 196.00f, 146.83f, 220.00f, 293.66f, 220.00f }, /* Synth */
        { 110.00f, 130.81f, 164.81f, 220.00f, 196.00f, 164.81f, 146.83f, 130.81f }  /* Symphonic */
    };
    while (arun) {
        short *o = abuf[cur];
        for (int i = 0; i < 1024; i++) {
            float m = 0.0f;
            if (sfx_on) {
                int st = (int)(bgm_t * 4.5f);
                float bf = notes_seq[curStyle][st & 7];
                float sub = bgm_t * 4.5f - floorf(bgm_t * 4.5f);
                float bt = bgm_t * bf - floorf(bgm_t * bf);
                m += (curStyle == 2 ? sinf(bt * 6.283f) : (2.0f * bt - 1.0f)) * 0.11f * expf(-sub * 2.0f);
                if (sfx_t > 0) {
                    sfx_p += sfx_f / 44100.0f;
                    m += sinf(sfx_p * 6.283f) * 0.24f * ((float)sfx_t / 3000.0f);
                    sfx_t--;
                }
            }
            bgm_t += 1.0f / 44100.0f;
            int v = (int)(m * 32767.0f);
            if (v > 32767) v = 32767; else if (v < -32767) v = -32767;
            o[i * 2] = (short)v; o[i * 2 + 1] = (short)v;
        }
        sceAudioOutputPannedBlocking(ach, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, o);
        cur ^= 1;
    }
    sceKernelExitDeleteThread(0);
    return 0;
}

typedef struct { int lane; float y; int active; } Note;
static Note notes[24];
static const char *bossNames[3] = { "GOLEM DE HIERRO", "AUTOMATA ARCANO", "DRAGON SOMBRIO" };
static const char *mobNames[3]  = { "SLIME DEL BOSQUE", "TRASGO ERRANTE", "ESPECTRO MENOR" };
static const char *styleNames[3] = { "ROCK", "SYNTH", "SINFONICO" };
static const int bossWeak[3] = { 0, 1, 2 };

static int chapter = 0, isMobBattle = 0, bossHitFlash = 0;
static int pHp = 100, pMaxHp = 100, pMp = 50, pMaxMp = 50, bHp = 120, bMaxHp = 120;
static int combo = 0, score = 0, best = 0, tick = 0, phaseTimer = 0;
static int relicDmg = 0, relicVamp = 0, relicShield = 0;
static int inTitle = 1, inOverworld = 0, inStory = 0, inFFMenu = 0, inDefense = 0, inAttackResolve = 0, inRelic = 0, gameOver = 0, victory = 0;
static int ffCursor = 0, pendingDmg = 0;
static float bardX = 240.0f, bardY = 195.0f;
static float projX[6], projY[6], projVx[6], projVy[6];
static int projAlive[6];
static char judge[32] = "";
static int judgeT = 0;
static int laneFlash[4] = { 0, 0, 0, 0 };
static float fretScroll = 0.0f;

/* PARTE 1: Mapa enorme Modo Aventura (64x48) */
#define MW 64
#define MH 48
static unsigned char worldMap[MH][MW]; /* 0=pasto, 1=bosque, 2=montana, 3=agua, 4=reino, 5=dungeon, 6=puente */
static int mapX = 10, mapY = 10, prevX1 = 9, prevY1 = 10, prevX2 = 8, prevY2 = 10;
static int bardFlip = 0, band1Flip = 0, band2Flip = 0;
static int bossDefeated[3] = { 0, 0, 0 };
static const int dngX[3] = { 24, 44, 52 };
static const int dngY[3] = { 12, 30, 10 };
static const int npcX[3] = { 12, 20, 38 };
static const int npcY[3] = { 11, 18, 26 };
static const char *npcMsg[3] = {
    "ANCIANO: El Golem al Este teme al ROCK! (+HP/MP)",
    "TROVADOR: El Automata tras el puente teme al SYNTH!",
    "SABIO: Solo lo SINFONICO vence al Dragon Sombrio!"
};
static char dlgText[64] = "";
static int dlgTimer = 0;

static void init_world(void) {
    for (int y = 0; y < MH; y++) {
        for (int x = 0; x < MW; x++) {
            if (x == 0 || y == 0 || x == MW - 1 || y == MH - 1) worldMap[y][x] = 2;
            else if (x >= 30 && x <= 31) {
                worldMap[y][x] = (y == 18 || y == 19) ? 6 : 3; /* Rio con puente de madera */
            } else {
                /* Natural clustered forest groves & mountains (no straight rows!) */
                int h = (x * 37 + y * 53 + (x * y) * 7) % 97;
                if (h < 14 && (abs(y - 18) > 2)) worldMap[y][x] = 1; /* Bosque */
                else if (h > 90 && (abs(y - 18) > 2)) worldMap[y][x] = 2; /* Montana */
                else worldMap[y][x] = 0;
            }
        }
    }
    /* Caminos de tierra (7) conectando Reino -> Puente -> Dungeons */
    for (int x = 10; x <= 44; x++) {
        if (worldMap[18][x] != 6) worldMap[18][x] = 7;
        if (worldMap[19][x] != 6) worldMap[19][x] = 7;
    }
    for (int y = 12; y <= 18; y++) worldMap[y][24] = 7;
    for (int y = 18; y <= 30; y++) worldMap[y][44] = 7;
    for (int x = 44; x <= 52; x++) worldMap[12][x] = 7;
    for (int y = 10; y <= 18; y++) worldMap[y][52] = 7;

    /* Reino amurallado inicial (8=muralla, 4=patio, 7=puerta Este) */
    for (int y = 7; y <= 14; y++) {
        for (int x = 7; x <= 14; x++) {
            if (x == 7 || x == 14 || y == 7 || y == 14) worldMap[y][x] = 8;
            else worldMap[y][x] = 4;
        }
    }
    worldMap[11][14] = 7; /* Puerta del castillo hacia el Este */
    for (int x = 14; x <= 24; x++) worldMap[11][x] = 7;

    for (int i = 0; i < 3; i++) {
        worldMap[dngY[i]][dngX[i]] = 5;
        worldMap[npcY[i]][npcX[i]] = 0;
        bossDefeated[i] = 0;
    }
    mapX = 10; mapY = 11; prevX1 = 9; prevY1 = 11; prevX2 = 8; prevY2 = 11;
    bardFlip = 0; band1Flip = 0; band2Flip = 0;
    dlgTimer = 0;
}

static void load_best(void) { FILE *f = fopen("cb_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("cb_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static void start_mob_battle(void) {
    isMobBattle = 1;
    chapter = xr() % 3;
    bHp = bMaxHp = 40;
    combo = 0; tick = 0; judgeT = 0; phaseTimer = 0; bossHitFlash = 0;
    bardX = 240.0f; bardY = 195.0f;
    memset(notes, 0, sizeof(notes));
    memset(projAlive, 0, sizeof(projAlive));
    inOverworld = 0; inStory = 0; inFFMenu = 1; ffCursor = 0; inDefense = 0; inAttackResolve = 0; pendingDmg = 0;
}

static void start_chapter(int ch) {
    isMobBattle = 0;
    chapter = ch;
    bHp = bMaxHp = 130 + ch * 65;
    combo = 0; tick = 0; judgeT = 0; phaseTimer = 0; bossHitFlash = 0;
    bardX = 240.0f; bardY = 195.0f;
    memset(notes, 0, sizeof(notes));
    memset(projAlive, 0, sizeof(projAlive));
    inOverworld = 0; inStory = 1; inFFMenu = 0; ffCursor = 0; inDefense = 0; inAttackResolve = 0; pendingDmg = 0;
}

static void start_game(void) {
    pHp = pMaxHp = 100; pMp = pMaxMp = 50; score = 0;
    relicDmg = 0; relicVamp = 0; relicShield = 0;
    init_world();
    inTitle = 0; inOverworld = 1; inStory = 0; inFFMenu = 0; inDefense = 0; inAttackResolve = 0; inRelic = 0; gameOver = 0; victory = 0; pendingDmg = 0;
    snprintf(dlgText, sizeof(dlgText), "Explora el Reino con tu banda! Habla con los NPCs.");
    dlgTimer = 180;
    play_sfx(660.0f, 2000);
}

int main(void) {
    int th = sceKernelCreateThread("cb", cb_th, 0x11, 0xFA0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    unsigned int vram_base = ((unsigned int)sceGeEdramGetAddr()) | 0x40000000u;
    vram_bufs[0] = (unsigned int *)vram_base;
    vram_bufs[1] = (unsigned int *)(vram_base + BUF_W * SCR_H * 4);
    memset((void *)vram_base, 0, BUF_W * SCR_H * 4 * 2);

    sceGuInit();
    sceGuStart(GU_DIRECT, gulist);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)(BUF_W * SCR_H * 4), BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_FLAT);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    load_best();
    ach = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1024, PSP_AUDIO_FORMAT_STEREO);
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("cb_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

    SceCtrlData pad; unsigned int prev = 0;
    const unsigned int laneBtns[4] = { PSP_CTRL_SQUARE, PSP_CTRL_TRIANGLE, PSP_CTRL_CIRCLE, PSP_CTRL_CROSS };
    int moveCd = 0;

    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;
        if (pressed & PSP_CTRL_SELECT) sfx_on = !sfx_on;
        if (bossHitFlash > 0) bossHitFlash--;

        /* ================= LOGIC ================= */
        if (inTitle || gameOver || victory) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        } else if (inOverworld) {
            if (dlgTimer > 0) dlgTimer--;
            if (moveCd > 0) moveCd--;

            int dx = 0, dy = 0;
            float ax = ((float)pad.Lx - 128.0f) / 128.0f, ay = ((float)pad.Ly - 128.0f) / 128.0f;
            if ((pad.Buttons & PSP_CTRL_LEFT)  || ax < -0.45f) dx = -1;
            else if ((pad.Buttons & PSP_CTRL_RIGHT) || ax >  0.45f) dx =  1;
            else if ((pad.Buttons & PSP_CTRL_UP)    || ay < -0.45f) dy = -1;
            else if ((pad.Buttons & PSP_CTRL_DOWN)  || ay >  0.45f) dy =  1;

            if ((dx != 0 || dy != 0) && moveCd <= 0) {
                moveCd = 9;
                int nx = mapX + dx, ny = mapY + dy;
                if (nx >= 0 && nx < MW && ny >= 0 && ny < MH && worldMap[ny][nx] != 2 && worldMap[ny][nx] != 3 && worldMap[ny][nx] != 8) {
                    band2Flip = (prevX2 > prevX1) ? 1 : (prevX2 < prevX1 ? 0 : band1Flip);
                    band1Flip = (prevX1 > mapX)   ? 1 : (prevX1 < mapX   ? 0 : bardFlip);
                    if (dx < 0) bardFlip = 1;
                    else if (dx > 0) bardFlip = 0;
                    prevX2 = prevX1; prevY2 = prevY1;
                    prevX1 = mapX;   prevY1 = mapY;
                    mapX = nx;       mapY = ny;
                    for (int i = 0; i < 3; i++) {
                        if (!bossDefeated[i] && mapX == dngX[i] && mapY == dngY[i]) {
                            start_chapter(i);
                            break;
                        }
                    }
                    if (inOverworld && worldMap[mapY][mapX] == 1 && (xr() % 7) == 0) {
                        start_mob_battle();
                    }
                }
            }
            if (pressed & PSP_CTRL_CROSS) {
                for (int i = 0; i < 3; i++) {
                    if (abs(mapX - npcX[i]) + abs(mapY - npcY[i]) <= 1) {
                        pHp = pMaxHp; pMp = pMaxMp;
                        snprintf(dlgText, sizeof(dlgText), "%s", npcMsg[i]);
                        dlgTimer = 200;
                        play_sfx(780.0f, 900);
                    }
                }
            }
        } else if (inStory) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) {
                inStory = 0; inFFMenu = 1; ffCursor = 0;
                play_sfx(520.0f, 1200);
            }
        } else if (inRelic) {
            int chosen = 0;
            if (pressed & PSP_CTRL_SQUARE)   { relicDmg += 4; chosen = 1; }
            else if (pressed & PSP_CTRL_TRIANGLE) { relicVamp += 3; chosen = 1; }
            else if (pressed & PSP_CTRL_CIRCLE)   { relicShield = 1; pHp = pMaxHp; chosen = 1; }
            if (chosen) {
                play_sfx(880.0f, 1500);
                inRelic = 0;
                bossDefeated[chapter] = 1;
                if (bossDefeated[0] && bossDefeated[1] && bossDefeated[2]) {
                    victory = 1; save_best();
                } else {
                    inOverworld = 1;
                    snprintf(dlgText, sizeof(dlgText), "Reliquia obtenida! Busca el siguiente Dungeon.");
                    dlgTimer = 180;
                }
            }
        } else if (inFFMenu) {
            if (pressed & PSP_CTRL_UP)   { ffCursor = (ffCursor + 3) % 4; play_sfx(440.0f, 400); }
            if (pressed & PSP_CTRL_DOWN) { ffCursor = (ffCursor + 1) % 4; play_sfx(440.0f, 400); }
            if (pressed & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) {
                curStyle = (curStyle + 1) % 3; play_sfx(520.0f, 600);
            }
            if (pressed & PSP_CTRL_CROSS) {
                if (ffCursor == 0) {
                    inFFMenu = 0; inAttackResolve = 0; phaseTimer = 0; tick = 0; pendingDmg = 0; combo = 0;
                    memset(notes, 0, sizeof(notes));
                    play_sfx(660.0f, 800);
                } else if (ffCursor == 1) {
                    curStyle = (curStyle + 1) % 3;
                    play_sfx(520.0f, 600);
                } else if (ffCursor == 2) {
                    if (pMp >= 15) {
                        pMp -= 15; pHp = (pHp + 45 > pMaxHp) ? pMaxHp : pHp + 45;
                        inFFMenu = 0; inDefense = 1; phaseTimer = 0;
                        memset(projAlive, 0, sizeof(projAlive));
                        play_sfx(880.0f, 1200);
                    } else {
                        play_sfx(180.0f, 600);
                    }
                } else if (ffCursor == 3) {
                    if (isMobBattle) {
                        inFFMenu = 0; inOverworld = 1;
                        snprintf(dlgText, sizeof(dlgText), "Escapaste del combate!"); dlgTimer = 100;
                        play_sfx(300.0f, 800);
                    } else {
                        play_sfx(180.0f, 600);
                    }
                }
            }
        } else if (inDefense) {
            float ax = ((float)pad.Lx - 128.0f) / 128.0f, ay = ((float)pad.Ly - 128.0f) / 128.0f;
            if (fabsf(ax) < 0.18f) ax = 0; if (fabsf(ay) < 0.18f) ay = 0;
            if (pad.Buttons & PSP_CTRL_LEFT)  ax = -1; if (pad.Buttons & PSP_CTRL_RIGHT) ax = 1;
            if (pad.Buttons & PSP_CTRL_UP)    ay = -1; if (pad.Buttons & PSP_CTRL_DOWN)  ay = 1;
            bardX += ax * 3.2f; bardY += ay * 2.8f;
            if (bardX < 140) bardX = 140; if (bardX > 340) bardX = 340;
            if (bardY < 130) bardY = 130; if (bardY > 245) bardY = 245;

            if (judgeT > 0) judgeT--;
            phaseTimer++;
            int pRate = isMobBattle ? 52 : (42 - chapter * 7);
            if ((phaseTimer % pRate) == 0) {
                for (int p = 0; p < 6; p++) if (!projAlive[p]) {
                    projAlive[p] = 1;
                    projX[p] = (float)(145 + (xr() % 190));
                    projY[p] = 132.0f;
                    projVx[p] = (bardX - projX[p]) * 0.011f;
                    projVy[p] = isMobBattle ? 1.35f : (1.65f + chapter * 0.30f);
                    break;
                }
            }
            for (int p = 0; p < 6; p++) if (projAlive[p]) {
                projX[p] += projVx[p]; projY[p] += projVy[p];
                if (projY[p] > 256) { projAlive[p] = 0; continue; }
                if (fabsf(projX[p] - bardX) < 11 && fabsf(projY[p] - bardY) < 11) {
                    projAlive[p] = 0;
                    int baseEdmg = isMobBattle ? 18 : (24 + chapter * 6);
                    int edmg = relicShield ? ((baseEdmg * 6) / 10) : baseEdmg;
                    pHp -= edmg; play_sfx(140.0f, 1200);
                    snprintf(judge, sizeof(judge), "-%d HP!", edmg);
                    judgeT = 26;
                    if (pHp <= 0) { pHp = 0; gameOver = 1; save_best(); }
                }
            }
            if (phaseTimer >= 240) {
                inDefense = 0; inFFMenu = 1; phaseTimer = 0;
                pMp = (pMp + 5 > pMaxMp) ? pMaxMp : pMp + 5;
                memset(projAlive, 0, sizeof(projAlive));
            }
        } else if (inAttackResolve) {
            /* Resolve accumulated Guitar Hero combo damage with sonic beam + impact + floating number! */
            if (judgeT > 0) judgeT--;
            phaseTimer++;
            if (phaseTimer == 18) {
                if (pendingDmg > 0) {
                    bHp -= pendingDmg;
                    if (bHp < 0) bHp = 0;
                    bossHitFlash = 16;
                    play_sfx(980.0f, 1800);
                } else {
                    play_sfx(220.0f, 900);
                }
            }
            if (phaseTimer >= 92) {
                inAttackResolve = 0;
                phaseTimer = 0;
                pendingDmg = 0;
                if (bHp <= 0) {
                    bHp = 0;
                    if (isMobBattle) {
                        score += 150;
                        inOverworld = 1;
                        snprintf(dlgText, sizeof(dlgText), "Enemigo vencido! +150 PTS");
                        dlgTimer = 120;
                    } else {
                        inRelic = 1;
                    }
                } else {
                    inDefense = 1;
                    bardX = 240.0f; bardY = 195.0f;
                    memset(projAlive, 0, sizeof(projAlive));
                }
            }
        } else {
            /* Guitar Hero Rhythm Attack Phase (Progressive & Readable) */
            if (pressed & PSP_CTRL_LTRIGGER) { curStyle = (curStyle + 2) % 3; play_sfx(440.0f, 600); }
            if (pressed & PSP_CTRL_RTRIGGER) { curStyle = (curStyle + 1) % 3; play_sfx(440.0f, 600); }
            if (judgeT > 0) judgeT--;
            for (int l = 0; l < 4; l++) if (laneFlash[l] > 0) laneFlash[l]--;

            float spd = isMobBattle ? 0.95f : (1.20f + chapter * 0.28f);
            fretScroll += spd;
            if (fretScroll >= 28.0f) fretScroll -= 28.0f;

            if (judgeT > 0) judgeT--;
            phaseTimer++;
            if (phaseTimer >= 420) {
                inAttackResolve = 1;
                phaseTimer = 0;
                memset(notes, 0, sizeof(notes));
            }

            tick++;
            int rate = isMobBattle ? 56 : (44 - chapter * 9);
            if ((tick == 12 || (tick % rate) == 0) && phaseTimer < 330) {
                for (int i = 0; i < 24; i++) if (!notes[i].active) {
                    notes[i].active = 1; notes[i].lane = xr() & 3; notes[i].y = 104.0f; break;
                }
            }

            for (int l = 0; l < 4; l++) if (pressed & laneBtns[l]) {
                laneFlash[l] = 8;
                int hit = -1; float bestD = 32.0f;
                for (int i = 0; i < 24; i++) if (notes[i].active && notes[i].lane == l) {
                    float d = fabsf(notes[i].y - HIT_Y);
                    if (d < bestD) { bestD = d; hit = i; }
                }
                if (hit >= 0) {
                    notes[hit].active = 0; combo++;
                    laneFlash[l] = 14;
                    int isPerf = (bestD < 14.0f);
                    int base = (isPerf ? 11 : 7) + relicDmg + (combo / 4);
                    if (curStyle == bossWeak[chapter]) base *= 2;
                    pendingDmg += base;
                    score += base * 10;
                    if (isPerf && relicVamp > 0) { pHp += relicVamp; if (pHp > pMaxHp) pHp = pMaxHp; }
                    snprintf(judge, sizeof(judge), isPerf ? "PERFECT! +%d" : "GOOD +%d", base);
                    judgeT = 30;
                    play_sfx(523.25f + l * 75.0f, 800);
                }
            }

            for (int i = 0; i < 24; i++) if (notes[i].active) {
                notes[i].y += spd;
                if (notes[i].y > SCR_H - 10) {
                    notes[i].active = 0; combo = 0;
                    int baseMiss = isMobBattle ? 3 : (4 + chapter);
                    int edmg = relicShield ? (baseMiss / 2 + 1) : baseMiss;
                    pHp -= edmg;
                    snprintf(judge, sizeof(judge), "MISS! -%d HP", edmg);
                    judgeT = 28;
                    play_sfx(140.0f, 1200);
                    if (pHp <= 0) { pHp = 0; gameOver = 1; save_best(); }
                }
            }
        }

        /* ================= RENDER ================= */
        unsigned int *fb = vram_bufs[cur_fb];
        char buf[64];

        if (inOverworld) {
            /* Render 20x12 textured 24x24 tiles covering the full 480x272 screen */
            int ox = mapX - 10, oy = mapY - 5;
            for (int ty = 0; ty < 12; ty++) {
                int sy = ty * 24 - 8;
                for (int tx = 0; tx < 20; tx++) {
                    int sx = tx * 24;
                    int wx = ox + tx, wy = oy + ty;
                    if (wx < 0 || wy < 0 || wx >= MW || wy >= MH) {
                        draw_tile24(fb, tile_grass, sx, sy);
                        draw_spr24(fb, spr_mountain, sx, sy);
                    } else {
                        unsigned char t = worldMap[wy][wx];
                        if (t == 3) {
                            draw_tile24(fb, tile_water, sx, sy);
                        } else if (t == 4) {
                            draw_tile24(fb, tile_castle, sx, sy);
                        } else if (t == 6) {
                            draw_tile24(fb, tile_bridge, sx, sy);
                        } else if (t == 7) {
                            draw_tile24(fb, tile_path, sx, sy);
                        } else if (t == 8) {
                            draw_tile24(fb, tile_wall, sx, sy);
                        } else {
                            draw_tile24(fb, tile_grass, sx, sy);
                            if (t == 1) draw_spr24(fb, spr_tree, sx, sy);
                            else if (t == 2) draw_spr24(fb, spr_mountain, sx, sy);
                            else if (t == 5) {
                                int dIdx = -1;
                                for (int k = 0; k < 3; k++) if (dngX[k] == wx && dngY[k] == wy) dIdx = k;
                                if (dIdx >= 0 && bossDefeated[dIdx]) draw_spr24(fb, spr_chest, sx, sy);
                                else draw_spr24(fb, spr_dungeon, sx, sy);
                            }
                        }
                    }
                }
            }
            /* NPCs dynamically turn to look towards your Bard! */
            for (int i = 0; i < 3; i++) {
                int sx = (npcX[i] - ox) * 24, sy = (npcY[i] - oy) * 24 - 8;
                if (sx >= -24 && sx < SCR_W && sy >= -24 && sy < SCR_H) {
                    int npcFlip = (mapX < npcX[i]) ? 1 : 0;
                    draw_spr24_dir(fb, (i == 1) ? spr_troubadour : spr_elder, sx, sy, npcFlip);
                    draw_text(fb, sx + 9, sy - 7, "!", RGB(255, 235, 60));
                }
            }
            /* Your 3-Piece Band following in formation and facing their walking direction */
            draw_spr24_dir(fb, spr_band2, (prevX2 - ox) * 24, (prevY2 - oy) * 24 - 8, band2Flip);
            draw_spr24_dir(fb, spr_band1, (prevX1 - ox) * 24, (prevY1 - oy) * 24 - 8, band1Flip);
            draw_spr24_dir(fb, spr_bard,  10 * 24, 5 * 24 - 8, bardFlip);

            /* Top Adventure HUD Window with visual HP & MP bars */
            draw_ff_window(fb, 4, 3, 472, 22);
            draw_text(fb, 12, 10, "REINO AETHEL", RGB(255, 225, 90));
            draw_text(fb, 94, 10, "HP", RGB(255, 130, 130));
            fill_rect(fb, 110, 10, 56, 7, RGB(40, 20, 25));
            fill_rect(fb, 110, 10, (pHp * 56) / pMaxHp, 7, RGB(70, 235, 100));
            draw_border(fb, 110, 10, 56, 7, RGB(220, 235, 255));
            draw_text(fb, 174, 10, "MP", RGB(130, 220, 255));
            fill_rect(fb, 190, 10, 44, 7, RGB(20, 25, 50));
            fill_rect(fb, 190, 10, (pMp * 44) / pMaxMp, 7, RGB(50, 200, 255));
            draw_border(fb, 190, 10, 44, 7, RGB(220, 235, 255));
            sprintf(buf, "BOSSES:%d/3 | %s | %d,%d",
                    bossDefeated[0]+bossDefeated[1]+bossDefeated[2], styleNames[curStyle], mapX, mapY);
            draw_text(fb, 244, 10, buf, RGB(255, 255, 255));

            if (dlgTimer > 0) {
                draw_ff_window(fb, 10, 228, 460, 38);
                fill_rect(fb, 16, 235, 24, 24, RGB(10, 14, 42));
                draw_spr24(fb, spr_troubadour, 16, 235);
                draw_border(fb, 16, 235, 24, 24, RGB(255, 215, 80));
                draw_text(fb, 48, 236, "CRONISTA DEL REINO:", RGB(255, 225, 90));
                draw_text(fb, 48, 250, dlgText, RGB(255, 255, 255));
            }
        } else {
            /* Copy 32-bit Arena Backdrop */
            for (int y = 0; y < SCR_H; y++) memcpy(&fb[y * BUF_W], &bg_arena[y * SCR_W], SCR_W * 4);

            if (inTitle) {
                draw_ff_window(fb, 64, 52, 352, 168);
                draw_text(fb, 115, 72, "S Y M P H O N Y   O F   T H E   A B Y S S", RGB(255, 225, 80));
                draw_text(fb, 138, 92, "32-BIT GUITAR HERO + RPG ROGUELIKE", RGB(100, 235, 255));
                draw_spr24(fb, spr_bard,  192, 110);
                draw_spr24(fb, spr_band1, 228, 110);
                draw_spr24(fb, spr_band2, 264, 110);
                sprintf(buf, "RECORD MAXIMO: %d PTS", best);
                draw_text(fb, 174, 144, buf, RGB(140, 255, 160));
                draw_text(fb, 148, 170, "PULSA     PARA EMPEZAR AVENTURA", RGB(255, 255, 255));
                draw_spr16(fb, spr_btn_cr16, 187, 166);
                draw_text(fb, 92, 194, "Explora el Reino, Toca Melodias y Vence al Mal", RGB(190, 205, 235));
            } else if (inStory) {
                draw_ff_window(fb, 64, 56, 352, 160);
                const unsigned int *bspr = (chapter == 0) ? boss_golem : (chapter == 1 ? boss_automaton : boss_dragon);
                draw_boss64_dir(fb, bspr, 82, 86, 0, 1);
                sprintf(buf, "DUNGEON %d: %s", chapter + 1, bossNames[chapter]);
                draw_text(fb, 158, 78, buf, RGB(255, 225, 80));
                sprintf(buf, "DEBILIDAD: %s (Dano x2)", styleNames[bossWeak[chapter]]);
                draw_text(fb, 158, 104, buf, RGB(100, 255, 160));
                draw_text(fb, 158, 130, "Combate por turnos clasico FF.", RGB(220, 230, 255));
                draw_text(fb, 158, 146, "Toca las notas para atacar!", RGB(220, 230, 255));
                draw_text(fb, 172, 182, "PULSA     PARA LUCHAR", RGB(255, 235, 90));
                draw_spr16(fb, spr_btn_cr16, 211, 178);
            } else if (inRelic) {
                /* 32-Bit Roguelike Relic Selection Cards with Real 20x20 PSP Button Sprites! */
                draw_spr48(fb, spr_band2_48, 18, 76);
                draw_spr48(fb, spr_bard_48,  98, 80);
                draw_spr48(fb, spr_band1_48, 58, 88);

                draw_ff_window(fb, 68, 52, 344, 198);
                fill_rect(fb, 74, 58, 332, 18, RGB(68, 48, 16));
                draw_spr24(fb, spr_chest, 78, 54);
                draw_text(fb, 108, 63, "4. BOTIN ROGUELIKE (ELIGE 1 RELIQUIA):", RGB(255, 225, 80));

                /* Card 1: Amplificador Runico (Square Button Sprite) */
                fill_rect(fb, 78, 84, 324, 46, RGB(42, 18, 48));
                draw_border(fb, 78, 84, 324, 46, RGB(255, 130, 210));
                fill_rect(fb, 84, 91, 32, 32, RGB(14, 12, 26));
                draw_border(fb, 84, 91, 32, 32, RGB(255, 130, 210));
                fill_rect(fb, 92, 99, 16, 16, RGB(235, 60, 170));
                fill_rect(fb, 96, 103, 8, 8, RGB(255, 240, 90));
                draw_text(fb, 126, 93, "AMPLIFICADOR RUNICO", RGB(255, 255, 255));
                draw_text(fb, 126, 109, "+4 DANO POR CADA NOTA ACERTADA", RGB(255, 155, 220));
                draw_spr20(fb, spr_btn_sq20, 368, 97);

                /* Card 2: Pua Vampirica Carmesi (Triangle Button Sprite) */
                fill_rect(fb, 78, 138, 324, 46, RGB(16, 44, 30));
                draw_border(fb, 78, 138, 324, 46, RGB(95, 245, 145));
                fill_rect(fb, 84, 145, 32, 32, RGB(14, 12, 26));
                draw_border(fb, 84, 145, 32, 32, RGB(95, 245, 145));
                fill_rect(fb, 93, 152, 14, 18, RGB(220, 40, 70));
                fill_rect(fb, 97, 156, 6, 6, RGB(255, 230, 80));
                draw_text(fb, 126, 147, "PUA VAMPIRICA CARMESI", RGB(255, 255, 255));
                draw_text(fb, 126, 163, "+3 HP CURADOS POR NOTA PERFECT", RGB(120, 255, 165));
                draw_spr20(fb, spr_btn_tr20, 368, 151);

                /* Card 3: Escudo Ritmico Astral (Circle Button Sprite) */
                fill_rect(fb, 78, 192, 324, 46, RGB(16, 32, 58));
                draw_border(fb, 78, 192, 324, 46, RGB(100, 215, 255));
                fill_rect(fb, 84, 199, 32, 32, RGB(14, 12, 26));
                draw_border(fb, 84, 199, 32, 32, RGB(100, 215, 255));
                fill_rect(fb, 92, 207, 16, 16, RGB(45, 155, 255));
                fill_rect(fb, 98, 209, 4, 12, RGB(255, 255, 255));
                fill_rect(fb, 94, 213, 12, 4, RGB(255, 255, 255));
                draw_text(fb, 126, 201, "ESCUDO RITMICO ASTRAL", RGB(255, 255, 255));
                draw_text(fb, 126, 217, "-40% DANO RECIBIDO + CURA 100%", RGB(130, 225, 255));
                draw_spr20(fb, spr_btn_ci20, 368, 205);
            } else if (gameOver || victory) {
                draw_ff_window(fb, 80, 66, 320, 140);
                draw_text(fb, 168, 90, victory ? "LEYENDA DEL ROCK!" : "CAISTE EN EL ABISMO", victory ? RGB(255,230,80) : RGB(255,80,90));
                draw_text(fb, 118, 112, victory ? "Has liberado los 3 Reinos del Mal!" : "Muerte permanente: la historia reinicia.", RGB(210, 225, 255));
                sprintf(buf, "PUNTOS: %d   RECORD: %d", score, best);
                draw_text(fb, 148, 140, buf, RGB(140, 255, 160));
                draw_text(fb, 148, 172, "Pulsa     para Nueva Partida", RGB(255, 255, 255));
                draw_spr16(fb, spr_btn_cr16, 187, 168);
            } else {
                const unsigned int * const btns20_arr[4] = { spr_btn_sq20, spr_btn_tr20, spr_btn_ci20, spr_btn_cr20 };
                /* Battle Scene: Top Status Windows + 32-bit High-Res Band (48x48) & Boss (64x64) */
                draw_ff_window(fb, 8, 6, 206, 42);
                fill_rect(fb, 14, 12, 26, 28, RGB(14, 18, 48));
                draw_spr24(fb, spr_bard, 15, 14);
                draw_border(fb, 14, 12, 26, 28, RGB(240, 200, 80));
                draw_text(fb, 46, 11, "BANDA DEL BARDO", RGB(255, 225, 85));
                draw_text(fb, 46, 23, "HP", RGB(120, 255, 140));
                fill_rect(fb, 62, 23, 80, 7, RGB(20, 35, 25));
                fill_rect(fb, 62, 23, (pHp * 80) / pMaxHp, 7, RGB(50, 225, 100));
                draw_border(fb, 62, 23, 80, 7, RGB(230, 240, 255));
                sprintf(buf, "%d/%d", pHp, pMaxHp);
                draw_text(fb, 146, 23, buf, RGB(255, 255, 255));

                draw_text(fb, 46, 34, "MP", RGB(100, 225, 255));
                fill_rect(fb, 62, 34, 80, 6, RGB(16, 28, 48));
                fill_rect(fb, 62, 34, (pMp * 80) / pMaxMp, 6, RGB(45, 195, 255));
                draw_border(fb, 62, 34, 80, 6, RGB(200, 235, 255));
                sprintf(buf, "%d/%d", pMp, pMaxMp);
                draw_text(fb, 146, 34, buf, RGB(140, 235, 255));

                /* Top-Right Boss/Enemy Status Window */
                draw_ff_window(fb, 266, 6, 206, 42);
                draw_text(fb, 276, 11, isMobBattle ? mobNames[chapter] : bossNames[chapter], RGB(255, 255, 255));
                draw_text(fb, 276, 23, "HP", RGB(255, 130, 140));
                fill_rect(fb, 294, 23, 104, 7, RGB(45, 14, 20));
                fill_rect(fb, 294, 23, (bHp * 104) / bMaxHp, 7, RGB(240, 50, 70));
                draw_border(fb, 294, 23, 104, 7, RGB(255, 230, 235));
                sprintf(buf, "%d/%d", bHp, bMaxHp);
                draw_text(fb, 402, 23, buf, RGB(255, 255, 255));
                sprintf(buf, "DEBILIDAD: %s (x2!)", styleNames[bossWeak[chapter]]);
                draw_text(fb, 276, 35, buf, RGB(255, 215, 80));

                /* Individual Runic Summoning Circles + Staggered 3-Piece 48x48 Band on Left */
                draw_ellipse_ring(fb, 42,  144, 22, 6, RGB(175, 105, 255));
                draw_ellipse_ring(fb, 122, 148, 22, 6, RGB(65, 225, 255));
                draw_ellipse_ring(fb, 82,  158, 22, 6, RGB(80, 235, 145));

                draw_spr48(fb, spr_band2_48, 18, 100);
                draw_spr48(fb, spr_bard_48,  98, 104);
                draw_spr48(fb, spr_band1_48, 58, 114);

                /* Boss Summoning Circle & Flood-Filled 64x64 Boss on Right */
                draw_ellipse_ring(fb, 390, 148, 42, 11, RGB(255, 65, 85));
                draw_ellipse_ring(fb, 390, 148, 32, 8,  RGB(255, 185, 90));
                const unsigned int *espr;
                if (isMobBattle) espr = (chapter == 0) ? mob_slime : (chapter == 1 ? mob_goblin : mob_wraith);
                else             espr = (chapter == 0) ? boss_golem : (chapter == 1 ? boss_automaton : boss_dragon);
                int shakeX = (inAttackResolve && phaseTimer >= 18 && phaseTimer < 38) ? ((phaseTimer & 2) ? 6 : -6) : 0;
                draw_boss64(fb, espr, 358 + shakeX, 86, bossHitFlash > 0);

                if (inFFMenu) {
                    /* Classic Final Fantasy Turn-Based Command Menu + Partitura Preview with REAL 32-Bit PSP Button Sprites! */
                    draw_ff_window(fb, 8, 170, 224, 96);
                    fill_rect(fb, 12, 174, 216, 12, RGB(38, 52, 128));
                    draw_text(fb, 16, 176, "COMANDOS DE BATALLA (TURNO)", RGB(255, 220, 80));

                    fill_rect(fb, 14, 189 + ffCursor * 18, 212, 15, RGB(65, 85, 185));
                    draw_border(fb, 14, 189 + ffCursor * 18, 212, 15, RGB(255, 215, 85));

                    const char *opts[4] = {
                        "ATACAR (Tocar Melodia)",
                        "CAMBIAR ESTILO MUSICAL",
                        "HIMNO CURATIVO (+45 HP)",
                        "HUIR DEL COMBATE"
                    };
                    for (int i = 0; i < 4; i++) {
                        int ry = 193 + i * 18;
                        draw_text(fb, 20, ry, opts[i], ffCursor == i ? RGB(255, 240, 90) : RGB(255, 255, 255));
                    }
                    draw_spr16(fb, spr_btn_cr16, 204, 188);
                    draw_spr_lr24(fb, 198, 207);
                    draw_text(fb, 196, 229, "15MP", RGB(130, 235, 215));
                    draw_spr16(fb, spr_btn_ci16, 204, 242);

                    /* Right Tactical & Partitura Window with Real 20x20 PSP Button Sprites */
                    draw_ff_window(fb, 238, 170, 234, 96);
                    fill_rect(fb, 242, 174, 226, 12, RGB(38, 52, 128));
                    draw_text(fb, 248, 176, "ARMONIA Y PARTITURA ENEMIGA", RGB(100, 235, 255));

                    fill_rect(fb, 246, 190, 218, 16, curStyle == bossWeak[chapter] ? RGB(18, 52, 36) : RGB(44, 34, 18));
                    draw_border(fb, 246, 190, 218, 16, curStyle == bossWeak[chapter] ? RGB(80, 235, 135) : RGB(240, 190, 75));
                    sprintf(buf, curStyle == bossWeak[chapter] ? "ESTILO: %s -> CRITICO x2!" : "ESTILO: %s", styleNames[curStyle]);
                    draw_text(fb, 252, 194, buf, curStyle == bossWeak[chapter] ? RGB(120, 255, 150) : RGB(255, 225, 95));
                    if (curStyle != bossWeak[chapter]) draw_spr_lr24(fb, 434, 191);

                    draw_text(fb, 246, 211, isMobBattle ? "MELODIA: SENCILLA (LENTA):" : "MELODIA BOSS (RAPIDA):", RGB(180, 225, 255));
                    for (int n = 0; n < 6; n++) {
                        int nl = (chapter + n * 3) & 3;
                        int nx = 248 + n * 36;
                        fill_rect(fb, nx, 221, 26, 22, RGB(18, 22, 54));
                        draw_border(fb, nx, 221, 26, 22, RGB(130, 150, 220));
                        draw_spr20(fb, btns20_arr[nl], nx + 3, 222);
                    }
                    sprintf(buf, "RELIQUIAS: +%dDMG  +%dVAMP  %s", relicDmg, relicVamp, relicShield ? "ESCUDO" : "-");
                    draw_text(fb, 246, 248, buf, RGB(255, 215, 85));
                } else if (inDefense) {
                    /* Enemy Turn: 32-Bit Runic Dodge Arena with Survival Bar & Fiery Shadow Orbs */
                    draw_ff_window(fb, 132, 110, 216, 156);
                    fill_rect(fb, 136, 114, 208, 13, RGB(88, 22, 42));
                    draw_text(fb, 140, 117, "3. ESQUIVA ATAQUE!", RGB(255, 235, 80));
                    fill_rect(fb, 266, 117, 72, 7, RGB(25, 10, 15));
                    fill_rect(fb, 266, 117, ((240 - phaseTimer) * 72) / 240, 7, RGB(255, 185, 50));
                    draw_border(fb, 266, 117, 72, 7, RGB(255, 255, 255));

                    /* Inner Runic Grid Arena (flashes crimson when hit!) */
                    fill_rect(fb, 138, 130, 204, 130, (judgeT > 18) ? RGB(72, 14, 24) : RGB(14, 11, 28));
                    for (int gx = 158; gx < 340; gx += 22) fill_rect(fb, gx, 131, 1, 128, RGB(34, 28, 64));
                    for (int gy = 148; gy < 258; gy += 18) fill_rect(fb, 139, gy, 202, 1, RGB(34, 28, 64));
                    draw_border(fb, 138, 130, 204, 130, (judgeT > 18) ? RGB(255, 80, 90) : RGB(140, 110, 225));

                    /* Glowing Cyan Aegis Shield Ring around Bard */
                    draw_ellipse_ring(fb, (int)bardX, (int)bardY, 15, 14, (judgeT > 16) ? RGB(255, 80, 90) : RGB(70, 235, 255));
                    draw_spr24(fb, spr_bard, (int)bardX - 12, (int)bardY - 12);
                    if (judgeT > 0) draw_text(fb, (int)bardX - 18, (int)bardY - 24 - (26 - judgeT) / 2, judge, RGB(255, 90, 90));

                    /* 32-Bit Layered Fiery Shadow-Flame Orbs with Comet Trails */
                    for (int p = 0; p < 6; p++) if (projAlive[p]) {
                        int ox = (int)projX[p], oy = (int)projY[p];
                        fill_rect(fb, ox - (int)(projVx[p]*3) - 3, oy - (int)(projVy[p]*3) - 3, 6, 6, RGB(160, 35, 190));
                        fill_rect(fb, ox - (int)(projVx[p]*1.5f) - 4, oy - (int)(projVy[p]*1.5f) - 4, 8, 8, RGB(230, 50, 95));
                        fill_rect(fb, ox - 6, oy - 6, 12, 12, RGB(255, 55, 75));
                        fill_rect(fb, ox - 4, oy - 4, 8, 8, RGB(255, 140, 50));
                        fill_rect(fb, ox - 2, oy - 2, 4, 4, RGB(255, 245, 170));
                    }
                } else if (inAttackResolve) {
                    /* Musical Combo Discharge: Sonic Beam -> Impact Flash -> Big Floating Damage Number! */
                    if (phaseTimer < 18) {
                        int bx = 142 + (phaseTimer * 240) / 18;
                        int by = 124 - (int)(sinf((float)phaseTimer * 0.17f) * 14.0f);
                        fill_rect(fb, bx - 16, by - 4, 24, 8, RGB(50, 180, 255));
                        fill_rect(fb, bx - 8,  by - 7, 16, 14, RGB(140, 240, 255));
                        fill_rect(fb, bx - 4,  by - 4, 8,  8, RGB(255, 255, 200));
                    } else {
                        int dt = phaseTimer - 18;
                        if (dt < 22 && pendingDmg > 0) {
                            /* Expanding Sonic Shockwave Rings & Sparks around Boss */
                            draw_ellipse_ring(fb, 390, 118, 16 + dt * 2, 16 + dt * 2, RGB(255, 225, 80));
                            draw_ellipse_ring(fb, 390, 118, 8 + dt * 2,  8 + dt * 2,  RGB(80, 235, 255));
                        }
                        /* Big 2x Floating Damage Number popping up above Enemy! */
                        int bounce = (dt < 14) ? (int)(sinf((float)dt * 0.22f) * 14.0f) : 14;
                        int fy = 66 - bounce - (dt > 24 ? (dt - 24) / 3 : 0);
                        if (pendingDmg > 0) {
                            sprintf(buf, "-%d!", pendingDmg);
                            draw_text_2x(fb, 366, fy, buf, (curStyle == bossWeak[chapter]) ? RGB(255, 235, 70) : RGB(255, 110, 110));
                        } else {
                            draw_text_2x(fb, 354, fy, "FALLO!", RGB(180, 190, 210));
                        }
                    }
                    draw_ff_window(fb, 68, 196, 344, 48);
                    sprintf(buf, pendingDmg > 0 ? "DESCARGA DEL COMBO! -%d HP AL ENEMIGO!" : "NO ACERTASTE NOTAS EN ESTE TURNO...", pendingDmg);
                    draw_text(fb, 92, 216, buf, pendingDmg > 0 ? RGB(255, 230, 90) : RGB(210, 220, 240));
                } else {
                    /* Guitar Hero 4-Lane Rhythm Highway (Enhanced Fretboard, Scrolling Beat Bars, & Hit Flash!) */
                    draw_ff_window(fb, 130, 74, 220, 192);
                    fill_rect(fb, 134, 78, 212, 14, RGB(28, 42, 108));
                    sprintf(buf, "%s | COMBO x%d", styleNames[curStyle], combo);
                    draw_text(fb, 138, 81, buf, curStyle == bossWeak[chapter] ? RGB(100, 255, 140) : RGB(255, 235, 100));
                    draw_spr_lr24(fb, 318, 78);

                    /* Turn Timer Bar at top of fretboard */
                    fill_rect(fb, 136, 94, 208, 4, RGB(18, 22, 50));
                    fill_rect(fb, 136, 94, ((420 - phaseTimer) * 208) / 420, 4, RGB(80, 220, 255));

                    /* Fretboard Dark Stage & Scrolling Beat Lines */
                    fill_rect(fb, 136, 100, 208, 160, RGB(11, 13, 30));
                    for (int fy = 100 + (int)fretScroll; fy < 258; fy += 28) {
                        fill_rect(fb, 137, fy, 206, 1, RGB(34, 40, 76));
                    }

                    unsigned int lcols[4] = { RGB(255,80,180), RGB(80,255,140), RGB(255,70,70), RGB(0,220,255) };
                    unsigned int ltint[4] = { RGB(34,16,32), RGB(14,34,24), RGB(34,16,18), RGB(14,26,40) };

                    /* Hit Zone Glowing Bar across all 4 lanes */
                    fill_rect(fb, 137, HIT_Y - 2, 206, 5, RGB(36, 44, 88));

                    for (int l = 0; l < 4; l++) {
                        int lx = 144 + l * 50;
                        fill_rect(fb, lx + 4, 100, 32, 158, ltint[l]);
                        fill_rect(fb, lx + 19, 100, 2, 158, RGB(65, 78, 135));
                        /* Receptor Box (lights up when pressed/hit!) */
                        fill_rect(fb, lx + 3, HIT_Y - 12, 34, 26, laneFlash[l] > 0 ? lcols[l] : RGB(16, 20, 48));
                        draw_border(fb, lx + 3, HIT_Y - 12, 34, 26, laneFlash[l] > 0 ? RGB(255, 255, 255) : lcols[l]);
                        draw_spr20(fb, btns20_arr[l], lx + 10, HIT_Y - 9);
                    }
                    for (int i = 0; i < 24; i++) if (notes[i].active) {
                        int l = notes[i].lane;
                        int lx = 144 + l * 50;
                        int ny = (int)notes[i].y;
                        fill_rect(fb, lx + 5, ny - 12, 30, 24, lcols[l]);
                        fill_rect(fb, lx + 7, ny - 10, 26, 20, RGB(20, 24, 58));
                        draw_border(fb, lx + 5, ny - 12, 30, 24, RGB(255, 255, 255));
                        draw_spr20(fb, btns20_arr[l], lx + 10, ny - 10);
                    }
                    sprintf(buf, "DANO ACUMULADO: %d", pendingDmg);
                    draw_text(fb, 174, 54, buf, RGB(120, 245, 255));
                    if (judgeT > 0) draw_text(fb, 192, 64, judge, RGB(255, 255, 120));
                }
            }
        }

        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
        cur_fb ^= 1;
    }

    save_best();
    arun = 0;
    if (ath >= 0) sceKernelWaitThreadEnd(ath, NULL);
    if (ach >= 0) sceAudioChRelease(ach);
    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
