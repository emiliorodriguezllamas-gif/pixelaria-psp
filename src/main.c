#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
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
#define HIT_Y 232
#define RGB(r,g,b) ((unsigned int)(0xFF000000u | ((unsigned int)(b)<<16) | ((unsigned int)(g)<<8) | (unsigned int)(r)))

/* Pure double-buffered uncached VRAM: zero flicker, zero Gu/CPU desync */
static unsigned int * const vram_bufs[2] = {
    (unsigned int *)0x44000000,
    (unsigned int *)(0x44000000 + BUF_W * SCR_H * 4)
};
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

static void draw_spr24(unsigned int *fb, const unsigned int *spr, int dx, int dy) {
    for (int j = 0; j < 24; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &spr[j * 24];
        for (int i = 0; i < 24; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[i];
                if (c) dst[px] = c;
            }
        }
    }
}

static void draw_spr24_scaled(unsigned int *fb, const unsigned int *spr, int dx, int dy, int scale) {
    for (int j = 0; j < 24 * scale; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &spr[(j / scale) * 24];
        for (int i = 0; i < 24 * scale; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[i / scale];
                if (c) dst[px] = c;
            }
        }
    }
}

static void draw_boss64(unsigned int *fb, const unsigned int *spr, int dx, int dy, int flash) {
    for (int j = 0; j < 64; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &spr[j * 64];
        for (int i = 0; i < 64; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[i];
                if (c) dst[px] = flash ? RGB(255, 255, 255) : c;
            }
        }
    }
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
static const char *laneLabels[4] = { "[]", "^", "O", "X" };
static const int bossWeak[3] = { 0, 1, 2 };

static int chapter = 0, isMobBattle = 0, bossHitFlash = 0;
static int pHp = 100, pMaxHp = 100, pMp = 50, pMaxMp = 50, bHp = 120, bMaxHp = 120;
static int combo = 0, score = 0, best = 0, tick = 0, phaseTimer = 0;
static int relicDmg = 0, relicVamp = 0, relicShield = 0;
static int inTitle = 1, inOverworld = 0, inStory = 0, inFFMenu = 0, inDefense = 0, inRelic = 0, gameOver = 0, victory = 0;
static int ffCursor = 0;
static float bardX = 240.0f, bardY = 195.0f;
static float projX[6], projY[6], projVx[6], projVy[6];
static int projAlive[6];
static char judge[32] = "";
static int judgeT = 0;

/* PARTE 1: Mapa enorme Modo Aventura (64x48) */
#define MW 64
#define MH 48
static unsigned char worldMap[MH][MW]; /* 0=pasto, 1=bosque, 2=montana, 3=agua, 4=reino, 5=dungeon, 6=puente */
static int mapX = 10, mapY = 10, prevX1 = 9, prevY1 = 10, prevX2 = 8, prevY2 = 10;
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
            else if (x >= 30 && x <= 32) {
                worldMap[y][x] = (y == 18 || y == 19) ? 6 : 3; /* Rio con puente de madera */
            }
            else if (((x * 7 + y * 13) % 9) == 0) worldMap[y][x] = 1; /* Bosque */
            else if (((x * 11 + y * 5) % 19) == 0) worldMap[y][x] = 2; /* Montana */
            else worldMap[y][x] = 0;
        }
    }
    /* Reino inicial */
    for (int y = 8; y <= 13; y++) for (int x = 8; x <= 13; x++) worldMap[y][x] = 4;
    for (int i = 0; i < 3; i++) {
        worldMap[dngY[i]][dngX[i]] = 5;
        worldMap[npcY[i]][npcX[i]] = 0;
        bossDefeated[i] = 0;
    }
    mapX = 10; mapY = 10; prevX1 = 9; prevY1 = 10; prevX2 = 8; prevY2 = 10;
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
    inOverworld = 0; inStory = 0; inFFMenu = 1; ffCursor = 0; inDefense = 0;
}

static void start_chapter(int ch) {
    isMobBattle = 0;
    chapter = ch;
    bHp = bMaxHp = 130 + ch * 65;
    combo = 0; tick = 0; judgeT = 0; phaseTimer = 0; bossHitFlash = 0;
    bardX = 240.0f; bardY = 195.0f;
    memset(notes, 0, sizeof(notes));
    memset(projAlive, 0, sizeof(projAlive));
    inOverworld = 0; inStory = 1; inFFMenu = 0; ffCursor = 0; inDefense = 0;
}

static void start_game(void) {
    pHp = pMaxHp = 100; pMp = pMaxMp = 50; score = 0;
    relicDmg = 0; relicVamp = 0; relicShield = 0;
    init_world();
    inTitle = 0; inOverworld = 1; inStory = 0; inFFMenu = 0; inDefense = 0; inRelic = 0; gameOver = 0; victory = 0;
    snprintf(dlgText, sizeof(dlgText), "Explora el mundo con tu banda! [X] Hablar con NPCs.");
    dlgTimer = 180;
    play_sfx(660.0f, 2000);
}

int main(void) {
    int th = sceKernelCreateThread("cb", cb_th, 0x11, 0xFA0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    sceDisplaySetMode(0, SCR_W, SCR_H);
    sceDisplaySetFrameBuf(vram_bufs[0], BUF_W, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_IMMEDIATE);

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
                if (nx >= 0 && nx < MW && ny >= 0 && ny < MH && worldMap[ny][nx] != 2 && worldMap[ny][nx] != 3) {
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
                    inFFMenu = 0; phaseTimer = 0; memset(notes, 0, sizeof(notes));
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

            phaseTimer++;
            int pRate = isMobBattle ? 36 : (26 - chapter * 4);
            if ((phaseTimer % pRate) == 0) {
                for (int p = 0; p < 6; p++) if (!projAlive[p]) {
                    projAlive[p] = 1;
                    projX[p] = (float)(145 + (xr() % 190));
                    projY[p] = 122.0f;
                    projVx[p] = (bardX - projX[p]) * 0.015f;
                    projVy[p] = isMobBattle ? 2.0f : (2.6f + chapter * 0.45f);
                    break;
                }
            }
            for (int p = 0; p < 6; p++) if (projAlive[p]) {
                projX[p] += projVx[p]; projY[p] += projVy[p];
                if (projY[p] > 260) { projAlive[p] = 0; continue; }
                if (fabsf(projX[p] - bardX) < 12 && fabsf(projY[p] - bardY) < 12) {
                    projAlive[p] = 0;
                    int edmg = relicShield ? 5 : 9;
                    pHp -= edmg; play_sfx(140.0f, 1200);
                    if (pHp <= 0) { pHp = 0; gameOver = 1; save_best(); }
                }
            }
            if (phaseTimer >= 240) {
                inDefense = 0; inFFMenu = 1; phaseTimer = 0;
                pMp = (pMp + 5 > pMaxMp) ? pMaxMp : pMp + 5;
                memset(projAlive, 0, sizeof(projAlive));
            }
        } else {
            /* Guitar Hero Rhythm Attack Phase */
            if (pressed & PSP_CTRL_LTRIGGER) { curStyle = (curStyle + 2) % 3; play_sfx(440.0f, 600); }
            if (pressed & PSP_CTRL_RTRIGGER) { curStyle = (curStyle + 1) % 3; play_sfx(440.0f, 600); }
            if (judgeT > 0) judgeT--;

            phaseTimer++;
            if (phaseTimer >= 360) {
                inDefense = 1; phaseTimer = 0;
                bardX = 240.0f; bardY = 195.0f;
                memset(notes, 0, sizeof(notes));
                memset(projAlive, 0, sizeof(projAlive));
            }

            tick++;
            int rate = isMobBattle ? 36 : (21 - chapter * 3);
            if ((tick % rate) == 0 && phaseTimer < 300) {
                for (int i = 0; i < 24; i++) if (!notes[i].active) {
                    notes[i].active = 1; notes[i].lane = xr() & 3; notes[i].y = 122; break;
                }
            }

            for (int l = 0; l < 4; l++) if (pressed & laneBtns[l]) {
                int hit = -1; float bestD = 26.0f;
                for (int i = 0; i < 24; i++) if (notes[i].active && notes[i].lane == l) {
                    float d = fabsf(notes[i].y - HIT_Y);
                    if (d < bestD) { bestD = d; hit = i; }
                }
                if (hit >= 0) {
                    notes[hit].active = 0; combo++;
                    int isPerf = (bestD < 10.0f);
                    int base = (isPerf ? 10 : 6) + relicDmg + (combo / 4);
                    if (curStyle == bossWeak[chapter]) base *= 2;
                    bHp -= base;
                    bossHitFlash = 6;
                    score += base * 10;
                    if (isPerf && relicVamp > 0) { pHp += relicVamp; if (pHp > pMaxHp) pHp = pMaxHp; }
                    snprintf(judge, sizeof(judge), isPerf ? "PERFECT! -%d" : "GOOD -%d", base);
                    judgeT = 28;
                    play_sfx(523.25f + l * 75.0f, 800);
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
                    }
                }
            }

            float spd = isMobBattle ? 1.5f : (2.3f + chapter * 0.4f);
            for (int i = 0; i < 24; i++) if (notes[i].active) {
                notes[i].y += spd;
                if (notes[i].y > SCR_H - 12) {
                    notes[i].active = 0; combo = 0;
                    int edmg = relicShield ? 4 : 7;
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
                            draw_tile24(fb, tile_water, sx, sy);
                            fill_rect(fb, sx, sy + 4, 24, 16, RGB(145, 95, 50));
                            draw_border(fb, sx, sy + 4, 24, 16, RGB(90, 55, 25));
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
            /* NPCs */
            for (int i = 0; i < 3; i++) {
                int sx = (npcX[i] - ox) * 24, sy = (npcY[i] - oy) * 24 - 8;
                if (sx >= -24 && sx < SCR_W && sy >= -24 && sy < SCR_H) {
                    draw_spr24(fb, (i == 1) ? spr_troubadour : spr_elder, sx, sy);
                    draw_text(fb, sx + 9, sy - 7, "!", RGB(255, 235, 60));
                }
            }
            /* Your 3-Piece Band following in formation */
            draw_spr24(fb, spr_band2, (prevX2 - ox) * 24, (prevY2 - oy) * 24 - 8);
            draw_spr24(fb, spr_band1, (prevX1 - ox) * 24, (prevY1 - oy) * 24 - 8);
            draw_spr24(fb, spr_bard,  10 * 24, 5 * 24 - 8);

            /* Top Adventure HUD Window */
            draw_ff_window(fb, 4, 3, 472, 20);
            sprintf(buf, "AVENTURA | HP:%d/%d MP:%d | BOSSES:%d/3 | POS:%d,%d",
                    pHp, pMaxHp, pMp, bossDefeated[0]+bossDefeated[1]+bossDefeated[2], mapX, mapY);
            draw_text(fb, 12, 9, buf, RGB(255, 235, 110));

            if (dlgTimer > 0) {
                draw_ff_window(fb, 12, 234, 456, 32);
                draw_text(fb, 22, 246, dlgText, RGB(255, 255, 255));
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
                draw_text(fb, 136, 170, "> PULSA [X] O [START] PARA JUGAR <", RGB(255, 255, 255));
                draw_text(fb, 92, 194, "Explora el Reino, Toca Melodias y Vence al Mal", RGB(190, 205, 235));
            } else if (inStory) {
                draw_ff_window(fb, 64, 56, 352, 160);
                const unsigned int *bspr = (chapter == 0) ? boss_golem : (chapter == 1 ? boss_automaton : boss_dragon);
                draw_boss64(fb, bspr, 82, 86, 0);
                sprintf(buf, "DUNGEON %d: %s", chapter + 1, bossNames[chapter]);
                draw_text(fb, 158, 78, buf, RGB(255, 225, 80));
                sprintf(buf, "DEBILIDAD: %s (Dano x2)", styleNames[bossWeak[chapter]]);
                draw_text(fb, 158, 104, buf, RGB(100, 255, 160));
                draw_text(fb, 158, 130, "Combate por turnos clasico FF.", RGB(220, 230, 255));
                draw_text(fb, 158, 146, "Toca las notas para atacar!", RGB(220, 230, 255));
                draw_text(fb, 165, 182, "> PULSA [X] PARA LUCHAR <", RGB(255, 235, 90));
            } else if (inRelic) {
                draw_ff_window(fb, 56, 52, 368, 168);
                draw_spr24(fb, spr_chest, 228, 62);
                draw_text(fb, 140, 92, "BOSS DERROTADO! ELIGE RELIQUIA:", RGB(255, 225, 80));
                draw_text(fb, 96, 118, "[[]] Amplificador (+4 Dano por Nota)", RGB(255, 140, 210));
                draw_text(fb, 96, 144, "[Tri] Pua Vampirica (+3 HP por Perfect)", RGB(120, 255, 150));
                draw_text(fb, 96, 170, "[O] Escudo Ritmico (-40% Dano + Cura 100%)", RGB(100, 225, 255));
            } else if (gameOver || victory) {
                draw_ff_window(fb, 80, 66, 320, 140);
                draw_text(fb, 168, 90, victory ? "LEYENDA DEL ROCK!" : "CAISTE EN EL ABISMO", victory ? RGB(255,230,80) : RGB(255,80,90));
                draw_text(fb, 118, 112, victory ? "Has liberado los 3 Reinos del Mal!" : "Muerte permanente: la historia reinicia.", RGB(210, 225, 255));
                sprintf(buf, "PUNTOS: %d   RECORD: %d", score, best);
                draw_text(fb, 148, 140, buf, RGB(140, 255, 160));
                draw_text(fb, 148, 172, "Pulsa [X] para Nueva Partida", RGB(255, 255, 255));
            } else {
                /* Battle Scene: Top Status Windows + 32-bit Character & Boss Sprites */
                draw_ff_window(fb, 8, 6, 184, 36);
                fill_rect(fb, 16, 14, 168, 8, RGB(30, 20, 35));
                fill_rect(fb, 16, 14, (pHp * 168) / pMaxHp, 8, RGB(60, 215, 255));
                sprintf(buf, "BANDA HP:%d/%d MP:%d", pHp, pMaxHp, pMp);
                draw_text(fb, 16, 26, buf, RGB(220, 240, 255));

                draw_ff_window(fb, 288, 6, 184, 36);
                fill_rect(fb, 296, 14, 168, 8, RGB(30, 20, 35));
                fill_rect(fb, 296, 14, (bHp * 168) / bMaxHp, 8, RGB(255, 55, 85));
                draw_text(fb, 296, 26, isMobBattle ? mobNames[chapter] : bossNames[chapter], RGB(255, 180, 180));

                /* Draw Band on Left & Boss/Monster on Right */
                draw_spr24_scaled(fb, spr_band2, 14, 70, 2);
                draw_spr24_scaled(fb, spr_band1, 36, 92, 2);
                draw_spr24_scaled(fb, spr_bard,  64, 82, 2);

                const unsigned int *espr;
                if (isMobBattle) espr = (chapter == 0) ? mob_slime : (chapter == 1 ? mob_goblin : mob_wraith);
                else             espr = (chapter == 0) ? boss_golem : (chapter == 1 ? boss_automaton : boss_dragon);
                draw_boss64(fb, espr, 376, 72, bossHitFlash > 0);

                if (inFFMenu) {
                    /* Classic Final Fantasy Turn-Based Command Menu */
                    draw_ff_window(fb, 12, 162, 224, 102);
                    draw_ff_window(fb, 244, 162, 224, 102);
                    const char *opts[4] = {
                        "ATACAR (Melodia)",
                        "CAMBIAR ESTILO",
                        "CURAR (+45HP / 15MP)",
                        "HUIR"
                    };
                    for (int i = 0; i < 4; i++) {
                        draw_text(fb, 24, 174 + i * 21, ffCursor == i ? ">" : " ", RGB(255, 235, 80));
                        draw_text(fb, 36, 174 + i * 21, opts[i], ffCursor == i ? RGB(255, 235, 80) : RGB(255, 255, 255));
                    }
                    sprintf(buf, "ESTILO: %s", styleNames[curStyle]);
                    draw_text(fb, 256, 176, buf, curStyle == bossWeak[chapter] ? RGB(100, 255, 140) : RGB(255, 225, 90));
                    sprintf(buf, "DEBIL A: %s", styleNames[bossWeak[chapter]]);
                    draw_text(fb, 256, 198, buf, RGB(180, 220, 255));
                    sprintf(buf, "ENEMIGO HP: %d/%d", bHp, bMaxHp);
                    draw_text(fb, 256, 220, buf, RGB(255, 170, 170));
                    draw_text(fb, 256, 242, "TURNO DE TU BANDA [X]", RGB(255, 255, 255));
                } else if (inDefense) {
                    /* Enemy Turn: Dodge Box */
                    draw_ff_window(fb, 128, 114, 224, 150);
                    draw_text(fb, 140, 98, "TURNO ENEMIGO: ESQUIVA!", RGB(255, 90, 90));
                    draw_spr24(fb, spr_bard, (int)bardX - 12, (int)bardY - 12);
                    for (int p = 0; p < 6; p++) if (projAlive[p]) {
                        fill_rect(fb, (int)projX[p] - 5, (int)projY[p] - 5, 10, 10, RGB(255, 60, 80));
                        draw_border(fb, (int)projX[p] - 5, (int)projY[p] - 5, 10, 10, RGB(255, 230, 90));
                    }
                } else {
                    /* Guitar Hero 4-Lane Rhythm Highway */
                    draw_ff_window(fb, 140, 114, 200, 152);
                    unsigned int lcols[4] = { RGB(255,80,180), RGB(80,255,140), RGB(255,70,70), RGB(0,220,255) };
                    for (int l = 0; l < 4; l++) {
                        int lx = 148 + l * 47;
                        fill_rect(fb, lx + 18, 118, 2, 142, RGB(45, 50, 90));
                        fill_rect(fb, lx, HIT_Y - 7, 38, 14, RGB(25, 30, 65));
                        draw_border(fb, lx, HIT_Y - 7, 38, 14, lcols[l]);
                        draw_text(fb, lx + 13, HIT_Y - 3, laneLabels[l], RGB(255, 255, 255));
                    }
                    for (int i = 0; i < 24; i++) if (notes[i].active) {
                        int lx = 151 + notes[i].lane * 47;
                        int ny = (int)notes[i].y;
                        fill_rect(fb, lx, ny - 6, 32, 12, lcols[notes[i].lane]);
                        draw_border(fb, lx, ny - 6, 32, 12, RGB(255, 255, 255));
                        draw_text(fb, lx + 10, ny - 3, laneLabels[notes[i].lane], RGB(255, 255, 255));
                    }
                    sprintf(buf, "ESTILO:%s  COMBO:x%d", styleNames[curStyle], combo);
                    draw_text(fb, 158, 98, buf, curStyle == bossWeak[chapter] ? RGB(100, 255, 140) : RGB(255, 235, 100));
                    if (judgeT > 0) draw_text(fb, 185, 82, judge, RGB(255, 255, 120));
                }
            }
        }

        sceDisplayWaitVblankStart();
        sceDisplaySetFrameBuf(fb, BUF_W, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_IMMEDIATE);
        cur_fb ^= 1;
    }

    save_best();
    arun = 0;
    if (ath >= 0) sceKernelWaitThreadEnd(ath, NULL);
    if (ach >= 0) sceAudioChRelease(ach);
    sceKernelExitGame();
    return 0;
}
