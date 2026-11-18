#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("ShadowNinja", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define GROUND_Y 236
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

static unsigned int __attribute__((aligned(16))) fb0[BUF_W * SCR_H];
static int running = 1;
static unsigned int rng = 0x55AA33CCu;
static unsigned int xr(void) { unsigned int x = rng; x ^= x<<13; x ^= x>>17; x ^= x<<5; rng = x; return x; }
static int exit_cb(int a, int b, void *c) { (void)a; (void)b; (void)c; running = 0; return 0; }
static int cb_th(SceSize a, void *p) { (void)a; (void)p; sceKernelRegisterExitCallback(sceKernelCreateCallback("Exit", exit_cb, NULL)); sceKernelSleepThreadCB(); return 0; }

static void put_px(int x, int y, unsigned int c) { if ((unsigned)x < SCR_W && (unsigned)y < SCR_H) fb0[y * BUF_W + x] = c; }
static void fill_rect(int x, int y, int w, int h, unsigned int c) {
    if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x; if (y + h > SCR_H) h = SCR_H - y;
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) fb0[(y + j) * BUF_W + (x + i)] = c;
}
static void draw_text(int x, int y, const char *s, unsigned int c) {
    while (*s) {
        unsigned char ch = (unsigned char)*s++;
        if (ch < 32 || ch > 127) ch = '?';
        const unsigned char *g = font5x7[ch - 32];
        for (int cx = 0; cx < 5; cx++) {
            unsigned char b = g[cx];
            for (int cy = 0; cy < 7; cy++) if (b & (1 << cy)) put_px(x + cx, y + cy, c);
        }
        x += 6;
    }
}

static int ach = -1, ath = -1;
static volatile int arun = 0, sfx_t = 0, sfx_on = 1;
static float sfx_f = 440.0f, sfx_p = 0.0f, bgm_t = 0.0f;
static short abuf[2][1024 * 2];
static void play_sfx(float f, int dur) { if (sfx_on) { sfx_f = f; sfx_t = dur; sfx_p = 0.0f; } }

static int audio_th(SceSize a, void *p) {
    (void)a; (void)p;
    int cur = 0;
    const float bass[4] = { 73.42f, 87.31f, 98.00f, 73.42f };
    while (arun) {
        short *o = abuf[cur];
        for (int i = 0; i < 1024; i++) {
            float m = 0.0f;
            if (sfx_on) {
                int st = (int)(bgm_t * 4.5f);
                float bf = bass[st & 3];
                float bt = bgm_t * bf - floorf(bgm_t * bf);
                m += (2.0f * bt - 1.0f) * 0.10f;
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

typedef struct { float x, y, vx; int alive; } Shuriken;
typedef struct { float x, y, vx; int hp, alive; } Enemy;

static Shuriken shurikens[16];
static Enemy enemies[14];
static float px = 240, py = GROUND_Y, pvy = 0;
static int facing = 1, jumps = 2, slashT = 0, dashT = 0, iframes = 0;
static int hp = 100, ammo = 12, wave = 1, score = 0, best = 0, spawnT = 0;
static int inTitle = 1, gameOver = 0;

static void load_best(void) { FILE *f = fopen("sn_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("sn_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static void start_game(void) {
    px = 240; py = GROUND_Y; pvy = 0; facing = 1; jumps = 2; slashT = 0; dashT = 0; iframes = 0;
    hp = 100; ammo = 12; wave = 1; score = 0; spawnT = 0;
    memset(shurikens, 0, sizeof(shurikens));
    memset(enemies, 0, sizeof(enemies));
    inTitle = 0; gameOver = 0;
    play_sfx(660.0f, 2000);
}

int main(void) {
    int th = sceKernelCreateThread("cb", cb_th, 0x11, 0xFA0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    sceDisplaySetMode(0, SCR_W, SCR_H);
    sceDisplaySetFrameBuf(fb0, BUF_W, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);

    load_best();
    ach = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1024, PSP_AUDIO_FORMAT_STEREO);
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("sn_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

    SceCtrlData pad; unsigned int prev = 0;
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;
        if (pressed & PSP_CTRL_SELECT) sfx_on = !sfx_on;

        if (inTitle || gameOver) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        } else {
            float ax = ((float)pad.Lx - 128.0f) / 128.0f;
            if (fabsf(ax) < 0.2f) ax = 0;
            if (pad.Buttons & PSP_CTRL_LEFT) ax = -1;
            if (pad.Buttons & PSP_CTRL_RIGHT) ax = 1;
            if (ax < -0.1f) facing = -1; else if (ax > 0.1f) facing = 1;

            if ((pressed & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) && dashT <= 0) {
                dashT = 10; iframes = 12; play_sfx(540.0f, 1200);
            }
            if (dashT > 0) dashT--;
            if (iframes > 0) iframes--;

            px += (dashT > 0 ? facing * 6.5f : ax * 3.2f);
            if (px < 14) px = 14; if (px > SCR_W - 14) px = SCR_W - 14;

            if ((pressed & PSP_CTRL_CROSS) && jumps > 0) {
                pvy = -7.6f; jumps--; play_sfx(480.0f, 1000);
            }
            pvy += 0.38f;
            py += pvy;
            if (py >= GROUND_Y) { py = GROUND_Y; pvy = 0; jumps = 2; }
            /* Platforms */
            if (pvy > 0 && py >= 166 && py <= 176 && ((px > 60 && px < 170) || (px > 310 && px < 420))) {
                py = 166; pvy = 0; jumps = 2;
            }

            if ((pressed & PSP_CTRL_SQUARE) && slashT <= 0) {
                slashT = 10; play_sfx(780.0f, 900);
                for (int i = 0; i < 14; i++) if (enemies[i].alive) {
                    float dx = enemies[i].x - (px + facing * 18);
                    if (fabsf(dx) < 22 && fabsf(enemies[i].y - py) < 24) {
                        enemies[i].hp -= 2;
                        if (enemies[i].hp <= 0) { enemies[i].alive = 0; score += 120; ammo++; }
                    }
                }
            }
            if (slashT > 0) slashT--;

            if ((pressed & PSP_CTRL_TRIANGLE) && ammo > 0) {
                ammo--;
                for (int i = 0; i < 16; i++) if (!shurikens[i].alive) {
                    shurikens[i].alive = 1; shurikens[i].x = px; shurikens[i].y = py - 12;
                    shurikens[i].vx = facing * 7.0f; break;
                }
                play_sfx(880.0f, 700);
            }

            spawnT++;
            if ((spawnT % 50) == 0) {
                for (int i = 0; i < 14; i++) if (!enemies[i].alive) {
                    enemies[i].alive = 1;
                    int r = xr() & 1;
                    enemies[i].x = r ? SCR_W + 12 : -12;
                    enemies[i].y = GROUND_Y;
                    enemies[i].vx = r ? -(1.4f + wave * 0.15f) : (1.4f + wave * 0.15f);
                    enemies[i].hp = 2 + wave / 2;
                    break;
                }
                if ((spawnT % 400) == 0) wave++;
            }

            for (int i = 0; i < 16; i++) if (shurikens[i].alive) {
                shurikens[i].x += shurikens[i].vx;
                if (shurikens[i].x < 0 || shurikens[i].x > SCR_W) { shurikens[i].alive = 0; continue; }
                for (int j = 0; j < 14; j++) if (enemies[j].alive) {
                    if (fabsf(shurikens[i].x - enemies[j].x) < 12 && fabsf(shurikens[i].y - (enemies[j].y - 12)) < 16) {
                        shurikens[i].alive = 0; enemies[j].hp--;
                        if (enemies[j].hp <= 0) { enemies[j].alive = 0; score += 100; play_sfx(200.0f, 1400); }
                        break;
                    }
                }
            }

            for (int i = 0; i < 14; i++) if (enemies[i].alive) {
                enemies[i].x += enemies[i].vx;
                if (enemies[i].x < -24 || enemies[i].x > SCR_W + 24) enemies[i].alive = 0;
                if (iframes <= 0 && fabsf(enemies[i].x - px) < 12 && fabsf(enemies[i].y - py) < 18) {
                    hp -= 20; iframes = 40; play_sfx(120.0f, 2800);
                    if (hp <= 0) { hp = 0; gameOver = 1; save_best(); }
                }
            }
        }

        fill_rect(0, 0, SCR_W, SCR_H, RGB(14, 12, 28));
        fill_rect(380, 28, 36, 36, RGB(240, 235, 190)); /* Moon */
        fill_rect(0, GROUND_Y, SCR_W, SCR_H - GROUND_Y, RGB(28, 24, 42));
        fill_rect(60, 166, 110, 6, RGB(70, 60, 100));
        fill_rect(310, 166, 110, 6, RGB(70, 60, 100));

        char buf[64];
        if (inTitle) {
            draw_text(165, 80, "S H A D O W   N I N J A", RGB(255, 80, 120));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(188, 120, buf, RGB(140, 255, 160));
            draw_text(148, 160, "Pulsa [X] o [START] para Jugar", RGB(255, 255, 255));
            draw_text(90, 210, "X: Doble Salto | []: Katana | Tri: Shuriken | L/R: Dash", RGB(190, 200, 230));
        } else if (gameOver) {
            draw_text(190, 95, "NINJA CAIDO", RGB(255, 70, 90));
            sprintf(buf, "PUNTOS: %d  RECORD: %d", score, best);
            draw_text(148, 130, buf, RGB(255, 255, 255));
            draw_text(155, 165, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else {
            for (int i = 0; i < 16; i++) if (shurikens[i].alive) fill_rect((int)shurikens[i].x - 3, (int)shurikens[i].y - 3, 6, 6, RGB(220, 240, 255));
            for (int i = 0; i < 14; i++) if (enemies[i].alive) fill_rect((int)enemies[i].x - 6, (int)enemies[i].y - 22, 12, 22, RGB(220, 60, 80));
            if ((iframes & 2) == 0) {
                fill_rect((int)px - 6, (int)py - 22, 12, 22, RGB(35, 35, 55));
                fill_rect((int)px - 4, (int)py - 19, 8, 3, RGB(255, 50, 80)); /* Headband */
            }
            if (slashT > 0) fill_rect((int)px + facing * 8, (int)py - 18, facing * 22, 6, RGB(120, 240, 255));
            sprintf(buf, "HP:%d  SHURIKEN:%d  OLEADA:%d  PTS:%d", hp, ammo, wave, score);
            draw_text(10, 6, buf, RGB(255, 235, 100));
        }
        sceDisplayWaitVblankStart();
    }

    save_best();
    arun = 0;
    if (ath >= 0) sceKernelWaitThreadEnd(ath, NULL);
    if (ach >= 0) sceAudioChRelease(ach);
    sceKernelExitGame();
    return 0;
}
