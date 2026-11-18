#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("GalacticDefender", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

#include "sprites_pack.h"
#include "bg_synth.h"
static unsigned int * const fb_ptrs[2] = { (unsigned int *)0x44000000, (unsigned int *)(0x44000000 + BUF_W * SCR_H * 4) };
static int cur_fb = 0;
#define fb0 (fb_ptrs[cur_fb])
static int running = 1;
static unsigned int rng = 0x55667788u;

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
    const float bass[4] = { 73.42f, 82.41f, 98.00f, 110.00f };
    while (arun) {
        short *o = abuf[cur];
        for (int i = 0; i < 1024; i++) {
            float m = 0.0f;
            if (sfx_on) {
                int st = (int)(bgm_t * 4.5f);
                float bf = bass[st & 3];
                float bt = bgm_t * bf - floorf(bgm_t * bf);
                m += (2.0f * bt - 1.0f) * 0.11f;
                if (sfx_t > 0) {
                    sfx_p += sfx_f / 44100.0f;
                    m += sinf(sfx_p * 6.283f) * 0.25f * ((float)sfx_t / 4000.0f);
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

typedef struct { float x, y, vx, vy; int fromP, alive; } Bullet;
typedef struct { float x, y, vx, vy; int hp, isBoss, fireCd, alive; } Enemy;
typedef struct { float x, y; int spd; } Star;

static Bullet bullets[48];
static Enemy enemies[18];
static Star stars[40];

static float px = 60, py = 136;
static int hp = 100, bombs = 3, laserLv = 1, wave = 1, score = 0, best = 0, fireCd = 0, iframes = 0, spawnT = 0;
static int inTitle = 1, gameOver = 0;

static void load_best(void) { FILE *f = fopen("gd_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("gd_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static void start_game(void) {
    px = 60; py = 136; hp = 100; bombs = 3; laserLv = 1; wave = 1; score = 0; fireCd = 0; iframes = 0; spawnT = 0;
    memset(bullets, 0, sizeof(bullets));
    memset(enemies, 0, sizeof(enemies));
    inTitle = 0; gameOver = 0;
    play_sfx(660.0f, 2500);
}

int main(void) {
    int th = sceKernelCreateThread("cb", cb_th, 0x11, 0xFA0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    sceDisplaySetMode(0, SCR_W, SCR_H);
    sceDisplaySetFrameBuf(fb0, BUF_W, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_IMMEDIATE);

    for (int i = 0; i < 40; i++) { stars[i].x = (float)(xr() % SCR_W); stars[i].y = (float)(xr() % SCR_H); stars[i].spd = 1 + (xr() % 3); }
    load_best();
    ach = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1024, PSP_AUDIO_FORMAT_STEREO);
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("gd_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

    SceCtrlData pad; unsigned int prev = 0;
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;
        if (pressed & PSP_CTRL_SELECT) sfx_on = !sfx_on;

        for (int i = 0; i < 40; i++) { stars[i].x -= stars[i].spd; if (stars[i].x < 0) { stars[i].x = SCR_W; stars[i].y = (float)(xr() % SCR_H); } }

        if (inTitle || gameOver) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        } else {
            float ax = ((float)pad.Lx - 128.0f) / 128.0f, ay = ((float)pad.Ly - 128.0f) / 128.0f;
            if (fabsf(ax) < 0.18f) ax = 0; if (fabsf(ay) < 0.18f) ay = 0;
            if (pad.Buttons & PSP_CTRL_LEFT) ax = -1; if (pad.Buttons & PSP_CTRL_RIGHT) ax = 1;
            if (pad.Buttons & PSP_CTRL_UP) ay = -1; if (pad.Buttons & PSP_CTRL_DOWN) ay = 1;
            px += ax * 3.0f; py += ay * 2.8f;
            if (px < 16) px = 16; if (px > SCR_W - 40) px = SCR_W - 40;
            if (py < 24) py = 24; if (py > SCR_H - 16) py = SCR_H - 16;
            if (iframes > 0) iframes--;

            if (fireCd > 0) fireCd--;
            if ((pad.Buttons & PSP_CTRL_CROSS) && fireCd <= 0) {
                fireCd = 7;
                int shots = (laserLv >= 2) ? 2 : 1;
                for (int s = 0; s < shots; s++) {
                    for (int i = 0; i < 48; i++) if (!bullets[i].alive) {
                        bullets[i].alive = 1; bullets[i].fromP = 1;
                        bullets[i].x = px + 12; bullets[i].y = py + (shots == 2 ? (s ? 5 : -5) : 0);
                        bullets[i].vx = 8.0f; bullets[i].vy = 0;
                        break;
                    }
                }
                play_sfx(740.0f, 900);
            }

            if ((pressed & PSP_CTRL_CIRCLE) && bombs > 0) {
                bombs--;
                play_sfx(160.0f, 3500);
                for (int i = 0; i < 48; i++) if (bullets[i].alive && !bullets[i].fromP) bullets[i].alive = 0;
                for (int i = 0; i < 18; i++) if (enemies[i].alive) { enemies[i].hp -= 6; if (enemies[i].hp <= 0) { enemies[i].alive = 0; score += 100; } }
            }

            spawnT++;
            if ((spawnT % 42) == 0) {
                for (int i = 0; i < 18; i++) if (!enemies[i].alive) {
                    enemies[i].alive = 1;
                    enemies[i].isBoss = ((spawnT % 420) == 0);
                    enemies[i].x = SCR_W + 16;
                    enemies[i].y = enemies[i].isBoss ? 136 : (30 + (float)(xr() % (SCR_H - 60)));
                    enemies[i].vx = enemies[i].isBoss ? -0.8f : -(1.8f + wave * 0.2f);
                    enemies[i].vy = 0;
                    enemies[i].hp = enemies[i].isBoss ? (18 + wave * 4) : (1 + wave / 2);
                    enemies[i].fireCd = 55;
                    break;
                }
            }

            for (int i = 0; i < 48; i++) {
                if (!bullets[i].alive) continue;
                bullets[i].x += bullets[i].vx; bullets[i].y += bullets[i].vy;
                if (bullets[i].x < 0 || bullets[i].x > SCR_W) { bullets[i].alive = 0; continue; }
                if (bullets[i].fromP) {
                    for (int j = 0; j < 18; j++) if (enemies[j].alive) {
                        float r = enemies[j].isBoss ? 22 : 10;
                        if (fabsf(bullets[i].x - enemies[j].x) < r && fabsf(bullets[i].y - enemies[j].y) < r) {
                            bullets[i].alive = 0; enemies[j].hp--;
                            if (enemies[j].hp <= 0) {
                                enemies[j].alive = 0;
                                score += enemies[j].isBoss ? 800 : 100;
                                if (enemies[j].isBoss) { wave++; if (laserLv < 2) laserLv++; }
                                play_sfx(200.0f, 1500);
                            }
                            break;
                        }
                    }
                } else if (fabsf(bullets[i].x - px) < 10 && fabsf(bullets[i].y - py) < 8 && iframes <= 0) {
                    bullets[i].alive = 0; hp -= 20; iframes = 40; play_sfx(120.0f, 3000);
                    if (hp <= 0) { hp = 0; gameOver = 1; save_best(); }
                }
            }

            for (int i = 0; i < 18; i++) {
                if (!enemies[i].alive) continue;
                enemies[i].x += enemies[i].vx;
                if (enemies[i].isBoss && enemies[i].x < 390) enemies[i].vx = 0;
                if (enemies[i].x < -20) enemies[i].alive = 0;
                enemies[i].fireCd--;
                if (enemies[i].fireCd <= 0) {
                    enemies[i].fireCd = enemies[i].isBoss ? 35 : 85;
                    for (int b = 0; b < 48; b++) if (!bullets[b].alive) {
                        bullets[b].alive = 1; bullets[b].fromP = 0;
                        bullets[b].x = enemies[i].x - 10; bullets[b].y = enemies[i].y;
                        bullets[b].vx = -4.2f; bullets[b].vy = 0; break;
                    }
                }
                if (fabsf(enemies[i].x - px) < 14 && fabsf(enemies[i].y - py) < 12 && iframes <= 0) {
                    enemies[i].alive = 0; hp -= 25; iframes = 40; play_sfx(120.0f, 3000);
                    if (hp <= 0) { hp = 0; gameOver = 1; save_best(); }
                }
            }
        }

        fill_rect(0, 0, SCR_W, SCR_H, RGB(6, 8, 20));
        for (int i = 0; i < 40; i++) put_px((int)stars[i].x, (int)stars[i].y, RGB(180, 210, 255));

        char buf[64];
        if (inTitle) {
            for (int _y = 0; _y < SCR_H; _y++) memcpy(&fb0[_y * BUF_W], &bg_synth[_y * SCR_W], SCR_W * 4);
            draw_text(145, 80, "G A L A C T I C   D E F E N D E R", RGB(0, 235, 255));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(188, 120, buf, RGB(140, 255, 160));
            draw_text(145, 160, "Pulsa [X] o [START] para Despegar", RGB(255, 255, 255));
            draw_text(120, 210, "Stick: Mover | X: Laser | O: Bomba Nova", RGB(180, 200, 230));
        } else if (gameOver) {
            draw_text(190, 95, "NAVE DESTRUIDA", RGB(255, 70, 90));
            sprintf(buf, "PUNTOS: %d  RECORD: %d", score, best);
            draw_text(148, 130, buf, RGB(255, 255, 255));
            draw_text(155, 165, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else {
            for (int i = 0; i < 48; i++) if (bullets[i].alive) fill_rect((int)bullets[i].x - 4, (int)bullets[i].y - 1, 8, 3, bullets[i].fromP ? RGB(0, 240, 255) : RGB(255, 70, 70));
            for (int i = 0; i < 18; i++) if (enemies[i].alive) {
                int s = enemies[i].isBoss ? 20 : 8;
                fill_rect((int)enemies[i].x - s, (int)enemies[i].y - s, s * 2, s * 2, enemies[i].isBoss ? RGB(255, 60, 90) : RGB(255, 150, 50));
            }
            if ((iframes & 2) == 0) fill_rect((int)px - 10, (int)py - 5, 20, 10, RGB(80, 220, 255));
            sprintf(buf, "HP:%d  BOMBAS:%d  FASE:%d  PTS:%d", hp, bombs, wave, score);
            draw_text(10, 6, buf, RGB(255, 235, 100));
        }
        sceDisplayWaitVblankStart();
        sceDisplaySetFrameBuf(fb0, BUF_W, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_IMMEDIATE);
        cur_fb ^= 1;
    }

    save_best();
    arun = 0;
    if (ath >= 0) sceKernelWaitThreadEnd(ath, NULL);
    if (ach >= 0) sceAudioChRelease(ach);
    sceKernelExitGame();
    return 0;
}
