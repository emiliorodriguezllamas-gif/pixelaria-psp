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

PSP_MODULE_INFO("CrimsonSurvivors", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

static unsigned int __attribute__((aligned(16))) gulist[262144];
static void *fbp0 = (void*)0;
static void *fbp1 = (void*)(BUF_W * SCR_H * 4);
static int cur_fb = 0;

typedef struct {
    unsigned int color;
    short x, y, z;
} Vtx2D;

static Vtx2D __attribute__((aligned(16))) vpool[4096];
static int vidx = 0;

static int running = 1;
static unsigned int rng = 0x661199AAu;
static unsigned int xr(void) { unsigned int x = rng; x ^= x<<13; x ^= x>>17; x ^= x<<5; rng = x; return x; }
static int exit_cb(int a, int b, void *c) { (void)a; (void)b; (void)c; running = 0; return 0; }
static int cb_th(SceSize a, void *p) { (void)a; (void)p; sceKernelRegisterExitCallback(sceKernelCreateCallback("Exit", exit_cb, NULL)); sceKernelSleepThreadCB(); return 0; }

/* Hardware 2D quad */
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

/* Audio */
static int ach = -1, ath = -1;
static volatile int arun = 0, sfx_t = 0, sfx_on = 1;
static float sfx_f = 440.0f, sfx_p = 0.0f, bgm_t = 0.0f;
static short abuf[2][1024 * 2];
static void play_sfx(float f, int dur) { if (sfx_on) { sfx_f = f; sfx_t = dur; sfx_p = 0.0f; } }

static int audio_th(SceSize a, void *p) {
    (void)a; (void)p;
    int cur = 0;
    const float bass[8] = { 55.00f, 55.00f, 65.41f, 73.42f, 82.41f, 73.42f, 65.41f, 49.00f };
    while (arun) {
        short *o = abuf[cur];
        for (int i = 0; i < 1024; i++) {
            float m = 0.0f;
            if (sfx_on) {
                int st = (int)(bgm_t * 4.0f);
                float bf = bass[st & 7];
                float sub = bgm_t * 4.0f - floorf(bgm_t * 4.0f);
                float bt = bgm_t * bf - floorf(bgm_t * bf);
                m += (2.0f * bt - 1.0f) * 0.11f * expf(-sub * 2.0f);
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

typedef struct { float x, y, spd; int hp, maxHp, isBoss, hitFlash, alive; } Enemy;
typedef struct { float x, y, vx, vy; int life, alive; } Knife;
typedef struct { float x, y; int val, alive; } Gem;
typedef struct { float x, y, vx, vy; unsigned int col; int life; } Part;

static Enemy enemies[48];
static Knife knives[24];
static Gem gems[40];
static Part parts[48];

static float camX = 0, camY = 0, orbAng = 0;
static int hp = 100, maxHp = 100, xp = 0, nextXp = 5, lv = 1, kills = 0, bestKills = 0;
static int orbCount = 1, knifeLv = 1, dmg = 2, fireCd = 0, iframes = 0, tick = 0;
static int inTitle = 1, levelUp = 0, gameOver = 0;

static void load_best(void) { FILE *f = fopen("cs_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) bestKills=v; fclose(f); } }
static void save_best(void) { if (kills > bestKills) { bestKills = kills; FILE *f = fopen("cs_save.dat", "wb"); if (f) { fwrite(&bestKills,4,1,f); fclose(f); } } }

static void spawn_parts(float x, float y, unsigned int col, int cnt) {
    int n = 0;
    for (int i = 0; i < 48 && n < cnt; i++) if (parts[i].life <= 0) {
        parts[i].x = x; parts[i].y = y;
        parts[i].vx = ((int)(xr() % 30) - 15) * 0.15f;
        parts[i].vy = ((int)(xr() % 30) - 15) * 0.15f;
        parts[i].col = col; parts[i].life = 14; n++;
    }
}

static void start_game(void) {
    camX = 0; camY = 0; hp = maxHp = 100; xp = 0; nextXp = 5; lv = 1; kills = 0;
    orbCount = 1; knifeLv = 1; dmg = 2; fireCd = 0; iframes = 0; tick = 0;
    memset(enemies, 0, sizeof(enemies));
    memset(knives, 0, sizeof(knives));
    memset(gems, 0, sizeof(gems));
    memset(parts, 0, sizeof(parts));
    inTitle = 0; levelUp = 0; gameOver = 0;
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
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("cs_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

    SceCtrlData pad; unsigned int prev = 0;
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;
        if (pressed & PSP_CTRL_SELECT) sfx_on = !sfx_on;

        if (inTitle || gameOver) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        } else if (levelUp) {
            if (pressed & PSP_CTRL_SQUARE) { if (orbCount < 5) orbCount++; levelUp = 0; play_sfx(880.0f, 1500); }
            else if (pressed & PSP_CTRL_TRIANGLE) { knifeLv++; dmg++; levelUp = 0; play_sfx(880.0f, 1500); }
            else if (pressed & PSP_CTRL_CIRCLE) { hp = maxHp; levelUp = 0; play_sfx(880.0f, 1500); }
        } else {
            float ax = ((float)pad.Lx - 128.0f) / 128.0f, ay = ((float)pad.Ly - 128.0f) / 128.0f;
            if (fabsf(ax) < 0.18f) ax = 0; if (fabsf(ay) < 0.18f) ay = 0;
            if (pad.Buttons & PSP_CTRL_LEFT) ax = -1; if (pad.Buttons & PSP_CTRL_RIGHT) ax = 1;
            if (pad.Buttons & PSP_CTRL_UP) ay = -1; if (pad.Buttons & PSP_CTRL_DOWN) ay = 1;
            camX += ax * 2.4f; camY += ay * 2.4f;
            if (iframes > 0) iframes--;

            orbAng += 0.06f;
            tick++;

            /* Spawn Enemies */
            int interval = 36 - lv * 2; if (interval < 12) interval = 12;
            if ((tick % interval) == 0) {
                for (int i = 0; i < 48; i++) if (!enemies[i].alive) {
                    enemies[i].alive = 1;
                    float a = ((float)(xr() % 360)) * 0.01745f;
                    enemies[i].x = camX + cosf(a) * 280.0f;
                    enemies[i].y = camY + sinf(a) * 190.0f;
                    enemies[i].isBoss = ((tick % 1800) == 0);
                    enemies[i].hp = enemies[i].maxHp = enemies[i].isBoss ? (30 + lv * 10) : (2 + lv);
                    enemies[i].spd = enemies[i].isBoss ? 0.8f : (0.95f + lv * 0.06f);
                    enemies[i].hitFlash = 0;
                    break;
                }
            }

            /* Auto Knives */
            if (fireCd > 0) fireCd--;
            if (fireCd <= 0) {
                int tgt = -1; float bd = 240.0f * 240.0f;
                for (int i = 0; i < 48; i++) if (enemies[i].alive) {
                    float dx = enemies[i].x - camX, dy = enemies[i].y - camY;
                    float d = dx * dx + dy * dy;
                    if (d < bd) { bd = d; tgt = i; }
                }
                if (tgt >= 0) {
                    fireCd = (knifeLv >= 3) ? 14 : 22;
                    float dx = enemies[tgt].x - camX, dy = enemies[tgt].y - camY;
                    float d = sqrtf(dx * dx + dy * dy) + 0.001f;
                    for (int k = 0; k < 24; k++) if (!knives[k].alive) {
                        knives[k].alive = 1; knives[k].x = camX; knives[k].y = camY;
                        knives[k].vx = (dx / d) * 6.5f; knives[k].vy = (dy / d) * 6.5f;
                        knives[k].life = 55; break;
                    }
                    play_sfx(740.0f, 500);
                }
            }

            /* Knives update */
            for (int k = 0; k < 24; k++) if (knives[k].alive) {
                knives[k].x += knives[k].vx; knives[k].y += knives[k].vy;
                if (--knives[k].life <= 0) { knives[k].alive = 0; continue; }
                for (int i = 0; i < 48; i++) if (enemies[i].alive) {
                    float r = enemies[i].isBoss ? 18.0f : 10.0f;
                    if (fabsf(knives[k].x - enemies[i].x) < r && fabsf(knives[k].y - enemies[i].y) < r) {
                        knives[k].alive = 0; enemies[i].hp -= dmg; enemies[i].hitFlash = 4;
                        if (enemies[i].hp <= 0) {
                            enemies[i].alive = 0; kills++;
                            spawn_parts(enemies[i].x, enemies[i].y, RGB(255, 60, 80), 8);
                            for (int g = 0; g < 40; g++) if (!gems[g].alive) {
                                gems[g].alive = 1; gems[g].x = enemies[i].x; gems[g].y = enemies[i].y;
                                gems[g].val = enemies[i].isBoss ? 5 : 1; break;
                            }
                            play_sfx(200.0f, 900);
                        }
                        break;
                    }
                }
            }

            /* Orbiting Orbs + Enemy movement */
            for (int i = 0; i < 48; i++) if (enemies[i].alive) {
                if (enemies[i].hitFlash > 0) enemies[i].hitFlash--;
                float dx = camX - enemies[i].x, dy = camY - enemies[i].y;
                float d = sqrtf(dx * dx + dy * dy) + 0.001f;
                enemies[i].x += (dx / d) * enemies[i].spd;
                enemies[i].y += (dy / d) * enemies[i].spd;

                for (int o = 0; o < orbCount; o++) {
                    float a = orbAng + o * (6.28318f / orbCount);
                    float ox = camX + cosf(a) * 44.0f, oy = camY + sinf(a) * 44.0f;
                    if (fabsf(ox - enemies[i].x) < 14 && fabsf(oy - enemies[i].y) < 14) {
                        enemies[i].hp -= 1; enemies[i].hitFlash = 3;
                        if (enemies[i].hp <= 0) {
                            enemies[i].alive = 0; kills++;
                            spawn_parts(enemies[i].x, enemies[i].y, RGB(255, 60, 80), 8);
                            for (int g = 0; g < 40; g++) if (!gems[g].alive) {
                                gems[g].alive = 1; gems[g].x = enemies[i].x; gems[g].y = enemies[i].y; gems[g].val = 1; break;
                            }
                            play_sfx(200.0f, 800);
                            break;
                        }
                    }
                }

                if (enemies[i].alive && iframes <= 0 && d < (enemies[i].isBoss ? 22.0f : 13.0f)) {
                    hp -= 15; iframes = 35; play_sfx(120.0f, 2000);
                    if (hp <= 0) { hp = 0; gameOver = 1; save_best(); }
                }
            }

            /* Gems */
            for (int g = 0; g < 40; g++) if (gems[g].alive) {
                float dx = camX - gems[g].x, dy = camY - gems[g].y;
                float d = sqrtf(dx * dx + dy * dy) + 0.001f;
                if (d < 65.0f) { gems[g].x += (dx / d) * 4.2f; gems[g].y += (dy / d) * 4.2f; }
                if (d < 14.0f) {
                    gems[g].alive = 0; xp += gems[g].val; play_sfx(880.0f, 500);
                    if (xp >= nextXp) {
                        xp -= nextXp; nextXp += 4; lv++;
                        levelUp = 1; play_sfx(1046.0f, 2000);
                    }
                }
            }

            for (int i = 0; i < 48; i++) if (parts[i].life > 0) {
                parts[i].x += parts[i].vx; parts[i].y += parts[i].vy; parts[i].life--;
            }
        }

        /* Hardware 2D Render via sceGu */
        vidx = 0;
        sceGuStart(GU_DIRECT, gulist);
        sceGuClearColor(RGB(18, 12, 22));
        sceGuClear(GU_COLOR_BUFFER_BIT);

        /* Gothic floor grid */
        int gx0 = -((int)camX % 32), gy0 = -((int)camY % 32);
        for (int gx = gx0 - 32; gx < SCR_W + 32; gx += 32) gu_rect(gx, 0, 1, SCR_H, RGB(34, 22, 42));
        for (int gy = gy0 - 32; gy < SCR_H + 32; gy += 32) gu_rect(0, gy, SCR_W, 1, RGB(34, 22, 42));

        if (!inTitle && !gameOver) {
            for (int g = 0; g < 40; g++) if (gems[g].alive) {
                int sx = SCR_W / 2 + (int)(gems[g].x - camX), sy = SCR_H / 2 + (int)(gems[g].y - camY);
                gu_rect(sx - 3, sy - 3, 6, 6, gems[g].val > 1 ? RGB(255, 220, 60) : RGB(60, 210, 255));
            }
            for (int i = 0; i < 48; i++) if (parts[i].life > 0) {
                int sx = SCR_W / 2 + (int)(parts[i].x - camX), sy = SCR_H / 2 + (int)(parts[i].y - camY);
                gu_rect(sx - 2, sy - 2, 4, 4, parts[i].col);
            }
            for (int i = 0; i < 48; i++) if (enemies[i].alive) {
                int sx = SCR_W / 2 + (int)(enemies[i].x - camX), sy = SCR_H / 2 + (int)(enemies[i].y - camY);
                int s = enemies[i].isBoss ? 16 : 8;
                unsigned int ec = enemies[i].hitFlash ? RGB(255,255,255) : (enemies[i].isBoss ? RGB(255,40,90) : RGB(210,50,60));
                gu_rect(sx - s, sy - s, s * 2, s * 2, ec);
            }
            for (int k = 0; k < 24; k++) if (knives[k].alive) {
                int sx = SCR_W / 2 + (int)(knives[k].x - camX), sy = SCR_H / 2 + (int)(knives[k].y - camY);
                gu_rect(sx - 3, sy - 3, 6, 6, RGB(255, 240, 120));
            }
            for (int o = 0; o < orbCount; o++) {
                float a = orbAng + o * (6.28318f / orbCount);
                int ox = SCR_W / 2 + (int)(cosf(a) * 44.0f), oy = SCR_H / 2 + (int)(sinf(a) * 44.0f);
                gu_rect(ox - 5, oy - 5, 10, 10, RGB(0, 235, 255));
            }
            if ((iframes & 2) == 0) {
                gu_rect(SCR_W / 2 - 8, SCR_H / 2 - 10, 16, 20, RGB(220, 40, 60));
                gu_rect(SCR_W / 2 - 5, SCR_H / 2 - 7, 10, 6, RGB(255, 220, 180));
            }
            /* XP Bar */
            gu_rect(0, 0, SCR_W, 5, RGB(30, 30, 50));
            gu_rect(0, 0, (xp * SCR_W) / (nextXp > 0 ? nextXp : 1), 5, RGB(60, 210, 255));
        }

        if (levelUp) gu_rect(80, 66, 320, 140, RGB(20, 16, 36));
        if (inTitle || gameOver) gu_rect(90, 72, 300, 124, RGB(20, 14, 30));

        sceGuFinish();
        sceGuSync(0, 0);

        unsigned int *fb = (unsigned int*)(0x44000000 + (cur_fb ? (BUF_W * SCR_H * 4) : 0));
        char buf[64];
        if (inTitle) {
            draw_text(fb, 145, 90, "C R I M S O N   S U R V I V O R S", RGB(255, 70, 90));
            sprintf(buf, "RECORD BAJAS: %d", bestKills);
            draw_text(fb, 185, 122, buf, RGB(255, 230, 100));
            draw_text(fb, 148, 152, "Pulsa [X] o [START] para Sobrevivir", RGB(255, 255, 255));
        } else if (gameOver) {
            draw_text(fb, 190, 92, "HAS CAIDO...", RGB(255, 70, 90));
            sprintf(buf, "BAJAS:%d  NIVEL:%d  RECORD:%d", kills, lv, bestKills);
            draw_text(fb, 140, 126, buf, RGB(255, 255, 255));
            draw_text(fb, 155, 158, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else if (levelUp) {
            draw_text(fb, 175, 80, "SUBISTE DE NIVEL!", RGB(255, 230, 80));
            draw_text(fb, 110, 112, "[[]] +1 Orbe Giratorio", RGB(0, 235, 255));
            draw_text(fb, 110, 136, "[Tri] +Dano y Velocidad Cuchillos", RGB(255, 240, 120));
            draw_text(fb, 110, 160, "[O] Curar Vida al 100%", RGB(120, 255, 140));
        } else {
            sprintf(buf, "HP:%d  NV:%d  BAJAS:%d", hp, lv, kills);
            draw_text(fb, 10, 10, buf, RGB(255, 235, 100));
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
