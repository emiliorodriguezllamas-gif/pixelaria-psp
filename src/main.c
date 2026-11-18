#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("StarColony", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

static unsigned int __attribute__((aligned(16))) fb0[BUF_W * SCR_H];
static int running = 1;
static unsigned int rng = 0x557799BBu;
static unsigned int xr(void) { unsigned int x = rng; x ^= x<<13; x ^= x>>17; x ^= x<<5; rng = x; return x; }
static int exit_cb(int a, int b, void *c) { (void)a; (void)b; (void)c; running = 0; return 0; }
static int cb_th(SceSize a, void *p) { (void)a; (void)p; sceKernelRegisterExitCallback(sceKernelCreateCallback("Exit", exit_cb, NULL)); sceKernelSleepThreadCB(); return 0; }

static void put_px(int x, int y, unsigned int c) { if ((unsigned)x < SCR_W && (unsigned)y < SCR_H) fb0[y * BUF_W + x] = c; }
static void fill_rect(int x, int y, int w, int h, unsigned int c) {
    if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x; if (y + h > SCR_H) h = SCR_H - y;
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) fb0[(y + j) * BUF_W + (x + i)] = c;
}
static void draw_outline(int x, int y, int w, int h, unsigned int c) {
    fill_rect(x, y, w, 1, c); fill_rect(x, y + h - 1, w, 1, c);
    fill_rect(x, y, 1, h, c); fill_rect(x + w - 1, y, 1, h, c);
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
    const float bass[4] = { 82.41f, 98.00f, 110.00f, 130.81f };
    while (arun) {
        short *o = abuf[cur];
        for (int i = 0; i < 1024; i++) {
            float m = 0.0f;
            if (sfx_on) {
                int st = (int)(bgm_t * 3.5f);
                float bf = bass[st & 3];
                float bt = bgm_t * bf - floorf(bgm_t * bf);
                m += (2.0f * bt - 1.0f) * 0.08f;
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

typedef struct { float x, y; int type, cd, alive; } Building; /* 0=turret, 1=gen */
typedef struct { float x, y; int hp, alive; } Alien;
typedef struct { float x, y, vx, vy; int alive; } Laser;

static Building blds[20];
static Alien aliens[20];
static Laser lasers[24];
static float cx = 240, cy = 136;
static int baseHp = 100, energy = 100, wave = 1, score = 0, best = 0, tick = 0;
static int inTitle = 1, gameOver = 0;

static void load_best(void) { FILE *f = fopen("sc_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("sc_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static void start_game(void) {
    cx = 240; cy = 136; baseHp = 100; energy = 120; wave = 1; score = 0; tick = 0;
    memset(blds, 0, sizeof(blds));
    memset(aliens, 0, sizeof(aliens));
    memset(lasers, 0, sizeof(lasers));
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
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("sc_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

    SceCtrlData pad; unsigned int prev = 0;
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;
        if (pressed & PSP_CTRL_SELECT) sfx_on = !sfx_on;

        if (inTitle || gameOver) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        } else {
            float ax = ((float)pad.Lx - 128.0f) / 128.0f, ay = ((float)pad.Ly - 128.0f) / 128.0f;
            if (fabsf(ax) < 0.18f) ax = 0; if (fabsf(ay) < 0.18f) ay = 0;
            if (pad.Buttons & PSP_CTRL_LEFT) ax = -1; if (pad.Buttons & PSP_CTRL_RIGHT) ax = 1;
            if (pad.Buttons & PSP_CTRL_UP) ay = -1; if (pad.Buttons & PSP_CTRL_DOWN) ay = 1;
            cx += ax * 3.5f; cy += ay * 3.5f;
            if (cx < 20) cx = 20; if (cx > SCR_W - 20) cx = SCR_W - 20;
            if (cy < 28) cy = 28; if (cy > SCR_H - 20) cy = SCR_H - 20;

            if ((pressed & PSP_CTRL_CROSS) && energy >= 50) {
                for (int i = 0; i < 20; i++) if (!blds[i].alive) {
                    blds[i].alive = 1; blds[i].type = 0; blds[i].x = cx; blds[i].y = cy; blds[i].cd = 0;
                    energy -= 50; play_sfx(660.0f, 1000); break;
                }
            }
            if ((pressed & PSP_CTRL_SQUARE) && energy >= 75) {
                for (int i = 0; i < 20; i++) if (!blds[i].alive) {
                    blds[i].alive = 1; blds[i].type = 1; blds[i].x = cx; blds[i].y = cy; blds[i].cd = 0;
                    energy -= 75; play_sfx(780.0f, 1000); break;
                }
            }
            if ((pressed & PSP_CTRL_TRIANGLE) && energy >= 60 && baseHp < 100) {
                energy -= 60; baseHp += 25; if (baseHp > 100) baseHp = 100; play_sfx(880.0f, 1200);
            }

            tick++;
            if ((tick % 60) == 0) energy += 4;
            if ((tick % 50) == 0) {
                for (int i = 0; i < 20; i++) if (!aliens[i].alive) {
                    aliens[i].alive = 1;
                    int s = xr() & 3;
                    aliens[i].x = (s == 0) ? 0 : (s == 1 ? SCR_W : (float)(xr() % SCR_W));
                    aliens[i].y = (s == 2) ? 24 : (s == 3 ? SCR_H : (float)(xr() % SCR_H));
                    aliens[i].hp = 2 + wave / 2;
                    break;
                }
                if ((tick % 500) == 0) wave++;
            }

            for (int i = 0; i < 20; i++) if (blds[i].alive) {
                if (blds[i].type == 1) {
                    if (++blds[i].cd >= 90) { blds[i].cd = 0; energy += 5; }
                } else {
                    if (blds[i].cd > 0) blds[i].cd--;
                    if (blds[i].cd <= 0) {
                        int tgt = -1; float bd = 130.0f * 130.0f;
                        for (int a = 0; a < 20; a++) if (aliens[a].alive) {
                            float dx = aliens[a].x - blds[i].x, dy = aliens[a].y - blds[i].y;
                            float d = dx * dx + dy * dy;
                            if (d < bd) { bd = d; tgt = a; }
                        }
                        if (tgt >= 0) {
                            blds[i].cd = 28;
                            float dx = aliens[tgt].x - blds[i].x, dy = aliens[tgt].y - blds[i].y;
                            float d = sqrtf(dx * dx + dy * dy) + 0.001f;
                            for (int l = 0; l < 24; l++) if (!lasers[l].alive) {
                                lasers[l].alive = 1; lasers[l].x = blds[i].x; lasers[l].y = blds[i].y;
                                lasers[l].vx = (dx / d) * 5.5f; lasers[l].vy = (dy / d) * 5.5f; break;
                            }
                        }
                    }
                }
            }

            for (int l = 0; l < 24; l++) if (lasers[l].alive) {
                lasers[l].x += lasers[l].vx; lasers[l].y += lasers[l].vy;
                if (lasers[l].x < 0 || lasers[l].x > SCR_W || lasers[l].y < 0 || lasers[l].y > SCR_H) { lasers[l].alive = 0; continue; }
                for (int a = 0; a < 20; a++) if (aliens[a].alive) {
                    if (fabsf(lasers[l].x - aliens[a].x) < 10 && fabsf(lasers[l].y - aliens[a].y) < 10) {
                        lasers[l].alive = 0; aliens[a].hp--;
                        if (aliens[a].hp <= 0) { aliens[a].alive = 0; score += 100; energy += 10; play_sfx(240.0f, 900); }
                        break;
                    }
                }
            }

            for (int a = 0; a < 20; a++) if (aliens[a].alive) {
                float dx = 240.0f - aliens[a].x, dy = 136.0f - aliens[a].y;
                float d = sqrtf(dx * dx + dy * dy) + 0.001f;
                aliens[a].x += (dx / d) * (0.75f + wave * 0.05f);
                aliens[a].y += (dy / d) * (0.75f + wave * 0.05f);
                if (d < 18.0f) {
                    aliens[a].alive = 0; baseHp -= 15; play_sfx(120.0f, 2500);
                    if (baseHp <= 0) { baseHp = 0; gameOver = 1; save_best(); }
                }
            }
        }

        fill_rect(0, 0, SCR_W, SCR_H, RGB(10, 14, 24));
        char buf[64];
        if (inTitle) {
            draw_text(170, 80, "S T A R   C O L O N Y", RGB(0, 235, 255));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(188, 120, buf, RGB(140, 255, 160));
            draw_text(148, 160, "Pulsa [X] o [START] para Jugar", RGB(255, 255, 255));
            draw_text(85, 204, "X: Torreta(50) | []: Generador(75) | Tri: Reparar(60)", RGB(190, 200, 230));
        } else if (gameOver) {
            draw_text(185, 95, "COLONIA DESTRUIDA", RGB(255, 70, 90));
            sprintf(buf, "PUNTOS: %d  RECORD: %d", score, best);
            draw_text(148, 130, buf, RGB(255, 255, 255));
            draw_text(155, 165, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else {
            fill_rect(226, 122, 28, 28, RGB(60, 160, 255)); /* Base */
            for (int i = 0; i < 20; i++) if (blds[i].alive) {
                fill_rect((int)blds[i].x - 6, (int)blds[i].y - 6, 12, 12, blds[i].type ? RGB(255, 220, 60) : RGB(0, 235, 255));
            }
            for (int a = 0; a < 20; a++) if (aliens[a].alive) fill_rect((int)aliens[a].x - 5, (int)aliens[a].y - 5, 10, 10, RGB(255, 60, 90));
            for (int l = 0; l < 24; l++) if (lasers[l].alive) fill_rect((int)lasers[l].x - 2, (int)lasers[l].y - 2, 4, 4, RGB(100, 255, 140));
            draw_outline((int)cx - 8, (int)cy - 8, 16, 16, RGB(255, 255, 255));
            sprintf(buf, "BASE:%d%%  EN:%d  OLEADA:%d  PTS:%d", baseHp, energy, wave, score);
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
