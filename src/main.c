#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <pspgu.h>
#include <pspge.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"
#include "bg_arena.h"

PSP_MODULE_INFO("SymphonyAbyss", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define HIT_Y 236
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

static unsigned int __attribute__((aligned(16))) gulist[262144];
static void *fbp0 = (void*)0;
static void *fbp1 = (void*)(BUF_W * SCR_H * 4);
static int cur_fb = 0;

typedef struct { unsigned int color; short x, y, z; } Vtx2D;
static Vtx2D __attribute__((aligned(16))) vpool[4096];
static int vidx = 0;

static int running = 1;
static unsigned int rng = 0x77113355u;
static unsigned int xr(void) { unsigned int x = rng; x ^= x<<13; x ^= x>>17; x ^= x<<5; rng = x; return x; }
static int exit_cb(int a, int b, void *c) { (void)a; (void)b; (void)c; running = 0; return 0; }
static int cb_th(SceSize a, void *p) { (void)a; (void)p; sceKernelRegisterExitCallback(sceKernelCreateCallback("Exit", exit_cb, NULL)); sceKernelSleepThreadCB(); return 0; }

static void gu_rect(int x, int y, int w, int h, unsigned int col) {
    if (vidx + 6 > 4096) return;
    Vtx2D *v = &vpool[vidx];
    v[0] = (Vtx2D){ col, (short)x, (short)y, 0 };
    v[1] = (Vtx2D){ col, (short)(x + w), (short)y, 0 };
    v[2] = (Vtx2D){ col, (short)(x + w), (short)(y + h), 0 };
    v[3] = (Vtx2D){ col, (short)x, (short)y, 0 };
    v[4] = (Vtx2D){ col, (short)(x + w), (short)(y + h), 0 };
    v[5] = (Vtx2D){ col, (short)x, (short)(y + h), 0 };
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 6, 0, v);
    vidx += 6;
}

static void put_px(unsigned int *fb, int x, int y, unsigned int c) {
    if ((unsigned)x < SCR_W && (unsigned)y < SCR_H) fb[y * BUF_W + x] = c;
}
static void draw_text(unsigned int *fb, int x, int y, const char *s, unsigned int c) {
    while (*s) {
        unsigned char ch = (unsigned char)*s++;
        if (ch < 32 || ch > 127) ch = '?';
        const unsigned char *g = font5x7[ch - 32];
        for (int cx = 0; cx < 5; cx++) {
            unsigned char b = g[cx];
            for (int cy = 0; cy < 7; cy++) if (b & (1 << cy)) put_px(fb, x + cx, y + cy, c);
        }
        x += 6;
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
    const float notes[3][8] = {
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
                float bf = notes[curStyle][st & 7];
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
static const char *styleNames[3] = { "ROCK", "SYNTH", "SINFONICO" };
static const int bossWeak[3] = { 0, 1, 2 };

static int chapter = 0, pHp = 100, pMaxHp = 100, pMp = 50, pMaxMp = 50, bHp = 120, bMaxHp = 120;
static int combo = 0, score = 0, best = 0, tick = 0, phaseTimer = 0;
static int relicDmg = 0, relicVamp = 0, relicShield = 0;
static int inTitle = 1, inOverworld = 0, inStory = 0, inFFMenu = 0, inDefense = 0, inRelic = 0, gameOver = 0, victory = 0;
static int ffCursor = 0;
static float bardX = 240.0f, bardY = 210.0f;
static float projX[6], projY[6], projVx[6], projVy[6];
static int projAlive[6];
static char judge[32] = "";
static int judgeT = 0;

/* PARTE 1: Mapa enorme Modo Aventura (64x48) */
#define MW 64
#define MH 48
static unsigned char worldMap[MH][MW]; /* 0=pasto, 1=bosque, 2=montana, 3=agua, 4=reino, 5=dungeon */
static int mapX = 10, mapY = 10, prevX1 = 9, prevY1 = 10, prevX2 = 8, prevY2 = 10;
static int bossDefeated[3] = { 0, 0, 0 };
static const int dngX[3] = { 26, 48, 54 };
static const int dngY[3] = { 14, 34, 10 };
static const int npcX[3] = { 12, 22, 42 };
static const int npcY[3] = { 12, 22, 30 };
static const char *npcMsg[3] = {
    "ANCIONO: El Golem teme al ROCK! Ve al Este.",
    "TROVADOR: El Automata cae ante el SYNTH!",
    "SABIO: Solo lo SINFONICO vence al Dragon!"
};
static char dlgText[64] = "";
static int dlgTimer = 0;

static void init_world(void) {
    for (int y = 0; y < MH; y++) {
        for (int x = 0; x < MW; x++) {
            if (x == 0 || y == 0 || x == MW - 1 || y == MH - 1) worldMap[y][x] = 2;
            else if (x > 30 && x < 34 && y != 20 && y != 21) worldMap[y][x] = 3; /* Rio con puente en y=20 */
            else if (((x * 7 + y * 13) % 11) == 0) worldMap[y][x] = 1; /* Bosque */
            else if (((x * 11 + y * 5) % 19) == 0) worldMap[y][x] = 2; /* Montana */
            else worldMap[y][x] = 0;
        }
    }
    /* Reino inicial */
    for (int y = 8; y <= 13; y++) for (int x = 8; x <= 13; x++) worldMap[y][x] = 4;
    for (int i = 0; i < 3; i++) { worldMap[dngY[i]][dngX[i]] = 5; bossDefeated[i] = 0; }
    mapX = 10; mapY = 10; prevX1 = 9; prevY1 = 10; prevX2 = 8; prevY2 = 10;
    dlgTimer = 0;
}

static void load_best(void) { FILE *f = fopen("cb_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("cb_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static int isMobBattle = 0;
static const char *mobNames[3] = { "SLIME DEL BOSQUE", "TRASGO ERRANTE", "ESPECTRO MENOR" };
static const char *laneLabels[4] = { "[]", "^", "O", "X" };

static void start_mob_battle(void) {
    isMobBattle = 1;
    chapter = xr() % 3;
    bHp = bMaxHp = 35;
    combo = 0; tick = 0; judgeT = 0; phaseTimer = 0;
    bardX = 240.0f; bardY = 210.0f;
    memset(notes, 0, sizeof(notes));
    memset(projAlive, 0, sizeof(projAlive));
    inOverworld = 0; inStory = 0; inDefense = 0;
}

static void start_chapter(int ch) {
    isMobBattle = 0;
    chapter = ch;
    bHp = bMaxHp = 120 + ch * 60;
    combo = 0; tick = 0; judgeT = 0; phaseTimer = 0;
    bardX = 240.0f; bardY = 210.0f;
    memset(notes, 0, sizeof(notes));
    memset(projAlive, 0, sizeof(projAlive));
    inStory = 1; inDefense = 0;
}

static void start_game(void) {
    pHp = pMaxHp = 100; score = 0;
    relicDmg = 0; relicVamp = 0; relicShield = 0;
    init_world();
    inTitle = 0; inOverworld = 1; inStory = 0; inDefense = 0; inRelic = 0; gameOver = 0; victory = 0;
    snprintf(dlgText, sizeof(dlgText), "Explora el reino con tu banda! Busca los Dungeons.");
    dlgTimer = 180;
    play_sfx(660.0f, 2000);
}

int main(void) {
    int th = sceKernelCreateThread("cb", cb_th, 0x11, 0xFA0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    sceGuInit();
    sceGuStart(GU_DIRECT, gulist);
    sceGuDrawBuffer(GU_PSM_8888, fbp0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, fbp1, BUF_W);
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
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;
        if (pressed & PSP_CTRL_SELECT) sfx_on = !sfx_on;

        if (inTitle || gameOver || victory) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        } else if (inOverworld) {
            if (dlgTimer > 0) dlgTimer--;
            int dx = 0, dy = 0;
            if (pressed & PSP_CTRL_LEFT) dx = -1;
            else if (pressed & PSP_CTRL_RIGHT) dx = 1;
            else if (pressed & PSP_CTRL_UP) dy = -1;
            else if (pressed & PSP_CTRL_DOWN) dy = 1;
            if (dx != 0 || dy != 0) {
                int nx = mapX + dx, ny = mapY + dy;
                if (nx >= 0 && nx < MW && ny >= 0 && ny < MH && worldMap[ny][nx] != 2 && worldMap[ny][nx] != 3) {
                    prevX2 = prevX1; prevY2 = prevY1;
                    prevX1 = mapX; prevY1 = mapY;
                    mapX = nx; mapY = ny;
                    for (int i = 0; i < 3; i++) {
                        if (!bossDefeated[i] && mapX == dngX[i] && mapY == dngY[i]) {
                            inOverworld = 0;
                            start_chapter(i);
                            break;
                        }
                    }
                    if (inOverworld && worldMap[mapY][mapX] == 1 && (xr() % 9) == 0) {
                        start_mob_battle();
                    }
                }
            }
            if (pressed & PSP_CTRL_CROSS) {
                for (int i = 0; i < 3; i++) {
                    if (abs(mapX - npcX[i]) + abs(mapY - npcY[i]) <= 1) {
                        pHp = pMaxHp;
                        snprintf(dlgText, sizeof(dlgText), "%s", npcMsg[i]);
                        dlgTimer = 180;
                        play_sfx(780.0f, 900);
                    }
                }
            }
        } else if (inStory) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) { inStory = 0; play_sfx(520.0f, 1200); }
        } else if (inRelic) {
            if (pressed & PSP_CTRL_SQUARE) { relicDmg += 4; inRelic = 0; bossDefeated[chapter] = 1; inOverworld = 1; }
            else if (pressed & PSP_CTRL_TRIANGLE) { relicVamp += 3; inRelic = 0; bossDefeated[chapter] = 1; inOverworld = 1; }
            else if (pressed & PSP_CTRL_CIRCLE) { relicShield = 1; pHp = pMaxHp; inRelic = 0; bossDefeated[chapter] = 1; inOverworld = 1; }
            if (bossDefeated[0] && bossDefeated[1] && bossDefeated[2]) { victory = 1; inOverworld = 0; save_best(); }
        } else if (inDefense) {
            float ax = ((float)pad.Lx - 128.0f) / 128.0f, ay = ((float)pad.Ly - 128.0f) / 128.0f;
            if (fabsf(ax) < 0.18f) ax = 0; if (fabsf(ay) < 0.18f) ay = 0;
            if (pad.Buttons & PSP_CTRL_LEFT) ax = -1; if (pad.Buttons & PSP_CTRL_RIGHT) ax = 1;
            if (pad.Buttons & PSP_CTRL_UP) ay = -1; if (pad.Buttons & PSP_CTRL_DOWN) ay = 1;
            bardX += ax * 3.4f; bardY += ay * 3.0f;
            if (bardX < 30) bardX = 30; if (bardX > SCR_W - 30) bardX = SCR_W - 30;
            if (bardY < 110) bardY = 110; if (bardY > SCR_H - 20) bardY = SCR_H - 20;

            phaseTimer++;
            if ((phaseTimer % 28) == 0) {
                for (int p = 0; p < 6; p++) if (!projAlive[p]) {
                    projAlive[p] = 1;
                    projX[p] = (float)(40 + (xr() % (SCR_W - 80)));
                    projY[p] = 60.0f;
                    projVx[p] = (bardX - projX[p]) * 0.015f;
                    projVy[p] = 2.6f + chapter * 0.5f;
                    break;
                }
            }
            for (int p = 0; p < 6; p++) if (projAlive[p]) {
                projX[p] += projVx[p]; projY[p] += projVy[p];
                if (projY[p] > SCR_H) { projAlive[p] = 0; continue; }
                if (fabsf(projX[p] - bardX) < 12 && fabsf(projY[p] - bardY) < 12) {
                    projAlive[p] = 0;
                    int edmg = relicShield ? 6 : 10;
                    pHp -= edmg; play_sfx(140.0f, 1200);
                    if (pHp <= 0) { pHp = 0; gameOver = 1; save_best(); }
                }
            }
            if (phaseTimer >= 360) {
                inDefense = 0; phaseTimer = 0;
                memset(projAlive, 0, sizeof(projAlive));
            }
        } else {
            if (pressed & PSP_CTRL_LTRIGGER) { curStyle = (curStyle + 2) % 3; play_sfx(440.0f, 600); }
            if (pressed & PSP_CTRL_RTRIGGER) { curStyle = (curStyle + 1) % 3; play_sfx(440.0f, 600); }
            if (judgeT > 0) judgeT--;

            phaseTimer++;
            if (phaseTimer >= 720) {
                inDefense = 1; phaseTimer = 0;
                memset(notes, 0, sizeof(notes));
            }

            tick++;
            int rate = isMobBattle ? 36 : (20 - chapter * 3);
            if ((tick % rate) == 0) {
                for (int i = 0; i < 24; i++) if (!notes[i].active) {
                    notes[i].active = 1; notes[i].lane = xr() & 3; notes[i].y = 120; break;
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
                    int base = (isPerf ? 10 : 5) + relicDmg + (combo / 4);
                    if (curStyle == bossWeak[chapter]) base *= 2;
                    bHp -= base;
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

            float spd = isMobBattle ? 1.5f : (2.4f + chapter * 0.45f);
            for (int i = 0; i < 24; i++) if (notes[i].active) {
                notes[i].y += spd;
                if (notes[i].y > SCR_H) {
                    notes[i].active = 0; combo = 0;
                    int edmg = relicShield ? 5 : 9;
                    pHp -= edmg;
                    snprintf(judge, sizeof(judge), "MISS! -%d HP", edmg);
                    judgeT = 28;
                    play_sfx(140.0f, 1200);
                    if (pHp <= 0) { pHp = 0; gameOver = 1; save_best(); }
                }
            }
        }

        unsigned int *fb = (unsigned int*)(0x44000000 + (cur_fb ? (BUF_W * SCR_H * 4) : 0));
        for (int y = 0; y < SCR_H; y++) memcpy(&fb[y * BUF_W], &bg_arena[y * SCR_W], SCR_W * 4);

        vidx = 0;
        sceGuStart(GU_DIRECT, gulist);

        if (inTitle || gameOver || victory || inStory || inRelic) {
            gu_rect(70, 58, 340, 156, RGB(14, 12, 28));
        } else if (inOverworld) {
            /* Render 20x12 visible tiles around (mapX, mapY) */
            int ox = mapX - 10, oy = mapY - 6;
            for (int ty = 0; ty < 12; ty++) {
                for (int tx = 0; tx < 20; tx++) {
                    int wx = ox + tx, wy = oy + ty;
                    unsigned int tc = RGB(45, 115, 55);
                    if (wx < 0 || wy < 0 || wx >= MW || wy >= MH) tc = RGB(20, 20, 30);
                    else {
                        unsigned char t = worldMap[wy][wx];
                        if (t == 1) tc = RGB(25, 75, 35);
                        else if (t == 2) tc = RGB(95, 90, 105);
                        else if (t == 3) tc = RGB(35, 95, 195);
                        else if (t == 4) tc = RGB(140, 115, 85);
                        else if (t == 5) tc = RGB(140, 30, 160);
                    }
                    gu_rect(tx * 24, ty * 22, 24, 22, tc);
                }
            }
            /* NPCs */
            for (int i = 0; i < 3; i++) {
                int sx = (npcX[i] - ox) * 24 + 5, sy = (npcY[i] - oy) * 22 + 4;
                if (sx >= 0 && sx < SCR_W && sy >= 0 && sy < SCR_H) gu_rect(sx, sy, 14, 14, RGB(255, 220, 60));
            }
            /* Band (3 musicians) */
            gu_rect((prevX2 - ox) * 24 + 6, (prevY2 - oy) * 22 + 5, 12, 12, RGB(180, 90, 255));
            gu_rect((prevX1 - ox) * 24 + 6, (prevY1 - oy) * 22 + 5, 12, 12, RGB(80, 230, 140));
            gu_rect(10 * 24 + 5, 6 * 22 + 4, 14, 14, RGB(60, 210, 255));
            if (dlgTimer > 0) gu_rect(16, 226, 448, 36, RGB(14, 12, 28));
        } else if (inFFMenu) {
            gu_rect(14, 12, 160, 12, RGB(30, 20, 30));
            gu_rect(14, 12, (pHp * 160) / pMaxHp, 12, RGB(60, 210, 255));
            gu_rect(306, 12, 160, 12, RGB(30, 20, 30));
            gu_rect(306, 12, (bHp * 160) / bMaxHp, 12, RGB(255, 50, 80));
            gu_rect(16, 162, 220, 96, RGB(14, 22, 68));
            gu_rect(244, 162, 220, 96, RGB(14, 22, 68));
        } else if (inDefense) {
            gu_rect(14, 12, 160, 12, RGB(30, 20, 30));
            gu_rect(14, 12, (pHp * 160) / pMaxHp, 12, RGB(60, 210, 255));
            gu_rect(306, 12, 160, 12, RGB(30, 20, 30));
            gu_rect(306, 12, (bHp * 160) / bMaxHp, 12, RGB(255, 50, 80));
            gu_rect((int)bardX - 8, (int)bardY - 10, 16, 20, RGB(60, 210, 255));
            for (int p = 0; p < 6; p++) if (projAlive[p]) {
                gu_rect((int)projX[p] - 5, (int)projY[p] - 5, 10, 10, RGB(255, 60, 80));
            }
        } else {
            /* HP Bars */
            gu_rect(14, 12, 160, 12, RGB(30, 20, 30));
            gu_rect(14, 12, (pHp * 160) / pMaxHp, 12, RGB(60, 210, 255));
            gu_rect(306, 12, 160, 12, RGB(30, 20, 30));
            gu_rect(306, 12, (bHp * 160) / bMaxHp, 12, RGB(255, 50, 80));

            /* 4-Lane Rhythm Highway */
            gu_rect(144, 118, 192, 154, RGB(12, 10, 24));
            unsigned int lcols[4] = { RGB(255,80,180), RGB(80,255,140), RGB(255,70,70), RGB(0,220,255) };
            for (int l = 0; l < 4; l++) {
                gu_rect(148 + l * 46, HIT_Y - 4, 40, 8, lcols[l]);
            }
            for (int i = 0; i < 24; i++) if (notes[i].active) {
                gu_rect(152 + notes[i].lane * 46, (int)notes[i].y - 5, 32, 10, lcols[notes[i].lane]);
            }
        }

        sceGuFinish();
        sceGuSync(0, 0);

        char buf[64];
        if (inTitle) {
            draw_text(fb, 118, 76, "S Y M P H O N Y   O F   T H E   A B Y S S", RGB(255, 225, 80));
        } else if (inOverworld) {
            sprintf(buf, "MODO AVENTURA | HP:%d | BOSSES:%d/3 | POS:(%d,%d)", pHp, bossDefeated[0]+bossDefeated[1]+bossDefeated[2], mapX, mapY);
            draw_text(fb, 10, 8, buf, RGB(255, 235, 100));
            if (dlgTimer > 0) draw_text(fb, 24, 238, dlgText, RGB(255, 255, 255));
            draw_text(fb, 145, 96, "32-BIT GUITAR HERO + RPG ROGUELIKE", RGB(0, 235, 255));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(fb, 190, 126, buf, RGB(140, 255, 160));
            draw_text(fb, 148, 160, "Pulsa [X] o [START] para Tocar!", RGB(255, 255, 255));
            draw_text(fb, 98, 186, "L/R: Cambiar Estilo | [] Tri O X: Notas", RGB(190, 200, 230));
        } else if (inStory) {
            sprintf(buf, "CAPITULO %d: %s", chapter + 1, bossNames[chapter]);
            draw_text(fb, 135, 84, buf, RGB(255, 225, 80));
            sprintf(buf, "DEBILIDAD MUSICAL: %s", styleNames[bossWeak[chapter]]);
            draw_text(fb, 145, 114, buf, RGB(0, 235, 255));
            draw_text(fb, 120, 144, "Usa L/R para elegir su estilo debil!", RGB(255, 255, 255));
            draw_text(fb, 155, 178, "Pulsa [X] para Comenzar", RGB(140, 255, 160));
        } else if (inRelic) {
            draw_text(fb, 165, 78, "BOSS DERROTADO! ELIGE RELIQUIA:", RGB(255, 225, 80));
            draw_text(fb, 105, 110, "[[]] Amplificador (+4 Dano por Nota)", RGB(255, 120, 200));
            draw_text(fb, 105, 136, "[Tri] Pua Vampirica (+3 HP por Perfect)", RGB(120, 255, 140));
            draw_text(fb, 105, 162, "[O] Escudo Ritmico (-40% Dano + Cura)", RGB(100, 220, 255));
        } else if (gameOver || victory) {
            draw_text(fb, 175, 92, victory ? "LEYENDA DEL ROCK!" : "RITMO ROTO...", victory ? RGB(255,230,80) : RGB(255,70,90));
            sprintf(buf, "PUNTOS: %d   RECORD: %d", score, best);
            draw_text(fb, 148, 128, buf, RGB(255, 255, 255));
            draw_text(fb, 155, 164, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else if (inFFMenu) {
            sprintf(buf, "HP:%d  MP:%d", pHp, pMp); draw_text(fb, 16, 28, buf, RGB(140, 230, 255));
            draw_text(fb, 306, 28, isMobBattle ? mobNames[chapter] : bossNames[chapter], RGB(255, 140, 140));
            const char *opts[4] = { "ATACAR (Melodia)", "ESTILO MUSICAL", "CURAR (+40HP 15MP)", "HUIR" };
            for (int i = 0; i < 4; i++) {
                draw_text(fb, 28, 174 + i * 20, ffCursor == i ? ">" : " ", RGB(255, 235, 80));
                draw_text(fb, 40, 174 + i * 20, opts[i], ffCursor == i ? RGB(255, 235, 80) : RGB(255, 255, 255));
            }
            sprintf(buf, "ESTILO: %s", styleNames[curStyle]);
            draw_text(fb, 258, 176, buf, RGB(120, 255, 160));
            draw_text(fb, 258, 200, "Elige accion [X]", RGB(200, 220, 255));
        } else if (inDefense) {
            sprintf(buf, "HP:%d", pHp); draw_text(fb, 16, 28, buf, RGB(140, 230, 255));
            draw_text(fb, 306, 28, bossNames[chapter], RGB(255, 140, 140));
            draw_text(fb, 130, 56, "FASE 3: ESQUIVA EL ATAQUE DEL BOSS!", RGB(255, 90, 90));
        } else {
            sprintf(buf, "HP:%d", pHp); draw_text(fb, 16, 28, buf, RGB(140, 230, 255));
            draw_text(fb, 306, 28, isMobBattle ? mobNames[chapter] : bossNames[chapter], RGB(255, 140, 140));
            for (int l = 0; l < 4; l++) {
                draw_text(fb, 162 + l * 46, HIT_Y - 3, laneLabels[l], RGB(255, 255, 255));
            }
            sprintf(buf, "ESTILO:%s  COMBO:x%d", styleNames[curStyle], combo);
            draw_text(fb, 165, 102, buf, curStyle == bossWeak[chapter] ? RGB(100, 255, 140) : RGB(255, 235, 100));
            if (judgeT > 0) draw_text(fb, 190, 122, judge, RGB(255, 255, 255));
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
