#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("MetalCommando", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define GROUND_Y 228
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

#include "sprites_pack.h"
#include "bg_synth.h"
static unsigned int * const fb_ptrs[2] = { (unsigned int *)0x44000000, (unsigned int *)(0x44000000 + BUF_W * SCR_H * 4) };
static int cur_fb = 0;
#define fb0 (fb_ptrs[cur_fb])
static int running = 1;
static unsigned int rng = 0x11223344u;
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
    const float bass[4] = { 82.41f, 82.41f, 98.00f, 110.00f };
    while (arun) {
        short *o = abuf[cur];
        for (int i = 0; i < 1024; i++) {
            float m = 0.0f;
            if (sfx_on) {
                int st = (int)(bgm_t * 5.0f);
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

typedef struct { float x, y, vx, vy; int fromP, isGrenade, alive; } Bullet;
typedef struct { float x, y, vx; int hp, isTank, fireCd, alive; } Enemy;

static Bullet bullets[40];
static Enemy enemies[14];
static float px = 80, py = GROUND_Y, pvy = 0;
static int facing = 1, onGround = 1, hp = 100, grenades = 8, score = 0, best = 0, fireCd = 0, iframes = 0, spawnT = 0;
static int inTitle = 1, gameOver = 0;

static void load_best(void) { FILE *f = fopen("mc_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("mc_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static void start_game(void) {
    px = 80; py = GROUND_Y; pvy = 0; facing = 1; onGround = 1; hp = 100; grenades = 8; score = 0; fireCd = 0; iframes = 0; spawnT = 0;
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

    load_best();
    ach = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1024, PSP_AUDIO_FORMAT_STEREO);
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("mc_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

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
            if (ax < -0.1f) facing = -1;
            else if (ax > 0.1f) facing = 1;

            px += ax * 2.8f;
            if (px < 16) px = 16; if (px > SCR_W - 16) px = SCR_W - 16;

            if ((pressed & PSP_CTRL_CROSS) && onGround) {
                pvy = -7.8f; onGround = 0; play_sfx(420.0f, 1200);
            }
            pvy += 0.38f;
            py += pvy;
            if (py >= GROUND_Y) { py = GROUND_Y; pvy = 0; onGround = 1; }
            if (iframes > 0) iframes--;

            if (fireCd > 0) fireCd--;
            if ((pad.Buttons & PSP_CTRL_SQUARE) && fireCd <= 0) {
                fireCd = 8;
                int aimUp = (pad.Buttons & PSP_CTRL_UP) ? 1 : 0;
                for (int i = 0; i < 40; i++) if (!bullets[i].alive) {
                    bullets[i].alive = 1; bullets[i].fromP = 1; bullets[i].isGrenade = 0;
                    bullets[i].x = px; bullets[i].y = py - 14;
                    bullets[i].vx = aimUp ? 0 : facing * 7.5f;
                    bullets[i].vy = aimUp ? -7.5f : 0;
                    break;
                }
                play_sfx(680.0f, 800);
            }

            if ((pressed & PSP_CTRL_TRIANGLE) && grenades > 0) {
                grenades--;
                for (int i = 0; i < 40; i++) if (!bullets[i].alive) {
                    bullets[i].alive = 1; bullets[i].fromP = 1; bullets[i].isGrenade = 1;
                    bullets[i].x = px; bullets[i].y = py - 16;
                    bullets[i].vx = facing * 4.5f; bullets[i].vy = -5.5f;
                    break;
                }
                play_sfx(300.0f, 1500);
            }

            spawnT++;
            if ((spawnT % 55) == 0) {
                for (int i = 0; i < 14; i++) if (!enemies[i].alive) {
                    enemies[i].alive = 1;
                    enemies[i].isTank = ((spawnT % 330) == 0);
                    int fromRight = (xr() & 1);
                    enemies[i].x = fromRight ? SCR_W + 16 : -16;
                    enemies[i].y = GROUND_Y;
                    enemies[i].vx = fromRight ? (enemies[i].isTank ? -0.9f : -1.5f) : (enemies[i].isTank ? 0.9f : 1.5f);
                    enemies[i].hp = enemies[i].isTank ? 10 : 2;
                    enemies[i].fireCd = 60;
                    break;
                }
            }

            for (int i = 0; i < 40; i++) {
                if (!bullets[i].alive) continue;
                if (bullets[i].isGrenade) bullets[i].vy += 0.25f;
                bullets[i].x += bullets[i].vx; bullets[i].y += bullets[i].vy;
                if (bullets[i].x < 0 || bullets[i].x > SCR_W || bullets[i].y < 0 || bullets[i].y > GROUND_Y + 4) {
                    bullets[i].alive = 0; continue;
                }
                if (bullets[i].fromP) {
                    for (int j = 0; j < 14; j++) if (enemies[j].alive) {
                        int r = enemies[j].isTank ? 18 : 10;
                        if (fabsf(bullets[i].x - enemies[j].x) < r && fabsf(bullets[i].y - (enemies[j].y - 12)) < r) {
                            enemies[j].hp -= bullets[i].isGrenade ? 5 : 1;
                            bullets[i].alive = 0;
                            if (enemies[j].hp <= 0) {
                                enemies[j].alive = 0;
                                score += enemies[j].isTank ? 400 : 100;
                                if (enemies[j].isTank) grenades += 2;
                                play_sfx(180.0f, 2000);
                            }
                            break;
                        }
                    }
                } else if (fabsf(bullets[i].x - px) < 9 && fabsf(bullets[i].y - (py - 12)) < 12 && iframes <= 0) {
                    bullets[i].alive = 0; hp -= 15; iframes = 35; play_sfx(120.0f, 2800);
                    if (hp <= 0) { hp = 0; gameOver = 1; save_best(); }
                }
            }

            for (int i = 0; i < 14; i++) {
                if (!enemies[i].alive) continue;
                enemies[i].x += enemies[i].vx;
                if (enemies[i].x < -30 || enemies[i].x > SCR_W + 30) enemies[i].alive = 0;
                enemies[i].fireCd--;
                if (enemies[i].fireCd <= 0) {
                    enemies[i].fireCd = enemies[i].isTank ? 55 : 95;
                    for (int b = 0; b < 40; b++) if (!bullets[b].alive) {
                        bullets[b].alive = 1; bullets[b].fromP = 0; bullets[b].isGrenade = 0;
                        bullets[b].x = enemies[i].x; bullets[b].y = enemies[i].y - 14;
                        bullets[b].vx = (px < enemies[i].x) ? -3.6f : 3.6f; bullets[b].vy = 0; break;
                    }
                }
                if (fabsf(enemies[i].x - px) < 12 && fabsf(enemies[i].y - py) < 16 && iframes <= 0) {
                    hp -= 20; iframes = 40; play_sfx(120.0f, 2800);
                    if (hp <= 0) { hp = 0; gameOver = 1; save_best(); }
                }
            }
        }

        fill_rect(0, 0, SCR_W, SCR_H, RGB(35, 55, 85));
        fill_rect(0, GROUND_Y, SCR_W, SCR_H - GROUND_Y, RGB(65, 50, 38));
        fill_rect(0, GROUND_Y, SCR_W, 4, RGB(95, 145, 65));

        char buf[64];
        if (inTitle) {
            for (int _y = 0; _y < SCR_H; _y++) memcpy(&fb0[_y * BUF_W], &bg_synth[_y * SCR_W], SCR_W * 4);
            draw_text(155, 78, "M E T A L   C O M M A N D O", RGB(255, 225, 80));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(188, 118, buf, RGB(140, 255, 160));
            draw_text(145, 158, "Pulsa [X] o [START] para Mision!", RGB(255, 255, 255));
            draw_text(105, 204, "Stick: Correr | X: Saltar | []: Rifle | Tri: Granada", RGB(200, 220, 245));
        } else if (gameOver) {
            draw_text(190, 95, "MISION FALLIDA", RGB(255, 70, 90));
            sprintf(buf, "PUNTOS: %d  RECORD: %d", score, best);
            draw_text(148, 130, buf, RGB(255, 255, 255));
            draw_text(155, 165, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else {
            for (int i = 0; i < 40; i++) if (bullets[i].alive) {
                unsigned int c = bullets[i].isGrenade ? RGB(80, 255, 100) : (bullets[i].fromP ? RGB(255, 230, 70) : RGB(255, 70, 70));
                fill_rect((int)bullets[i].x - 3, (int)bullets[i].y - 3, 6, 6, c);
            }
            for (int i = 0; i < 14; i++) if (enemies[i].alive) {
                if (enemies[i].isTank) fill_rect((int)enemies[i].x - 16, (int)enemies[i].y - 22, 32, 22, RGB(110, 125, 95));
                else fill_rect((int)enemies[i].x - 6, (int)enemies[i].y - 20, 12, 20, RGB(210, 70, 60));
            }
            if ((iframes & 2) == 0) {
                fill_rect((int)px - 6, (int)py - 22, 12, 22, RGB(70, 175, 255));
                fill_rect((int)px + facing * 4, (int)py - 16, 8, 4, RGB(230, 235, 245));
            }
            sprintf(buf, "HP:%d  GRANADAS:%d  PTS:%d", hp, grenades, score);
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
