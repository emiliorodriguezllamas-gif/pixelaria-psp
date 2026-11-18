#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("BrickBreakerNeon", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define ROWS 6
#define COLS 12
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

static unsigned int __attribute__((aligned(16))) fb0[BUF_W * SCR_H];
static int running = 1;
static unsigned int rng = 0x33445566u;
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
    const float bass[4] = { 98.00f, 110.00f, 130.81f, 146.83f };
    while (arun) {
        short *o = abuf[cur];
        for (int i = 0; i < 1024; i++) {
            float m = 0.0f;
            if (sfx_on) {
                int st = (int)(bgm_t * 4.0f);
                float bf = bass[st & 3];
                float bt = bgm_t * bf - floorf(bgm_t * bf);
                m += (bt < 0.5f ? 1.0f : -1.0f) * 0.08f;
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

typedef struct { float x, y, vx, vy; int alive, stuck; } Ball;
typedef struct { float x, y; int type, alive; } PowerUp;

static int bricks[ROWS][COLS];
static Ball balls[4];
static PowerUp pups[6];

static float padX = 240, padW = 72;
static int lives = 3, level = 1, score = 0, best = 0;
static int inTitle = 1, gameOver = 0;

static void load_best(void) { FILE *f = fopen("bb_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("bb_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static void init_level(int lv) {
    level = lv;
    padW = 72;
    memset(balls, 0, sizeof(balls));
    memset(pups, 0, sizeof(pups));
    balls[0].alive = 1; balls[0].stuck = 1;
    balls[0].x = padX; balls[0].y = 240;
    balls[0].vx = 2.6f; balls[0].vy = -3.2f;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            bricks[r][c] = (r == 0 && lv >= 2) ? 2 : 1;
}

static void start_game(void) {
    padX = 240; lives = 3; score = 0;
    init_level(1);
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
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("bb_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

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
            if (fabsf(ax) < 0.18f) ax = 0;
            if (pad.Buttons & PSP_CTRL_LEFT) ax = -1;
            if (pad.Buttons & PSP_CTRL_RIGHT) ax = 1;
            padX += ax * 5.2f;
            if (padX < padW * 0.5f + 6) padX = padW * 0.5f + 6;
            if (padX > SCR_W - padW * 0.5f - 6) padX = SCR_W - padW * 0.5f - 6;

            if (pressed & PSP_CTRL_CROSS) {
                for (int i = 0; i < 4; i++) if (balls[i].alive && balls[i].stuck) {
                    balls[i].stuck = 0; play_sfx(520.0f, 1200);
                }
            }

            int activeBalls = 0;
            for (int i = 0; i < 4; i++) {
                Ball *b = &balls[i];
                if (!b->alive) continue;
                activeBalls++;
                if (b->stuck) { b->x = padX; b->y = 240; continue; }
                b->x += b->vx; b->y += b->vy;
                if (b->x < 6) { b->x = 6; b->vx = fabsf(b->vx); play_sfx(440.0f, 600); }
                if (b->x > SCR_W - 6) { b->x = SCR_W - 6; b->vx = -fabsf(b->vx); play_sfx(440.0f, 600); }
                if (b->y < 24) { b->y = 24; b->vy = fabsf(b->vy); play_sfx(440.0f, 600); }

                if (b->y >= 244 && b->y <= 254 && fabsf(b->x - padX) < padW * 0.5f + 4 && b->vy > 0) {
                    float hit = (b->x - padX) / (padW * 0.5f);
                    b->vx = hit * 4.2f;
                    b->vy = -fabsf(b->vy);
                    play_sfx(580.0f, 800);
                }

                for (int r = 0; r < ROWS; r++) {
                    for (int c = 0; c < COLS; c++) {
                        if (bricks[r][c] <= 0) continue;
                        int bx = 12 + c * 38, by = 32 + r * 16;
                        if (b->x > bx - 3 && b->x < bx + 37 && b->y > by - 3 && b->y < by + 15) {
                            bricks[r][c]--;
                            b->vy = -b->vy;
                            score += 50;
                            play_sfx(740.0f, 900);
                            if (bricks[r][c] == 0 && (xr() % 6) == 0) {
                                for (int p = 0; p < 6; p++) if (!pups[p].alive) {
                                    pups[p].alive = 1; pups[p].x = bx + 16; pups[p].y = by + 6;
                                    pups[p].type = xr() % 2; break;
                                }
                            }
                        }
                    }
                }
                if (b->y > SCR_H + 6) b->alive = 0;
            }

            if (activeBalls == 0) {
                lives--;
                if (lives <= 0) { gameOver = 1; save_best(); play_sfx(140.0f, 3000); }
                else {
                    balls[0].alive = 1; balls[0].stuck = 1;
                    balls[0].x = padX; balls[0].y = 240;
                    balls[0].vx = 2.6f; balls[0].vy = -3.2f;
                }
            }

            for (int p = 0; p < 6; p++) if (pups[p].alive) {
                pups[p].y += 2.0f;
                if (pups[p].y > 240 && pups[p].y < 256 && fabsf(pups[p].x - padX) < padW * 0.5f + 6) {
                    pups[p].alive = 0;
                    if (pups[p].type == 0 && padW < 110) padW += 18;
                    else {
                        for (int i = 0; i < 4; i++) if (!balls[i].alive) {
                            balls[i].alive = 1; balls[i].stuck = 0;
                            balls[i].x = padX; balls[i].y = 236;
                            balls[i].vx = (i & 1) ? 2.8f : -2.8f; balls[i].vy = -3.2f;
                            break;
                        }
                    }
                    play_sfx(880.0f, 1500);
                }
                if (pups[p].y > SCR_H) pups[p].alive = 0;
            }

            int rem = 0;
            for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) if (bricks[r][c] > 0) rem++;
            if (rem == 0) init_level(level + 1);
        }

        fill_rect(0, 0, SCR_W, SCR_H, RGB(10, 12, 26));
        char buf[64];
        if (inTitle) {
            draw_text(135, 80, "B R I C K   B R E A K E R   N E O N", RGB(0, 240, 255));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(188, 120, buf, RGB(140, 255, 160));
            draw_text(148, 160, "Pulsa [X] o [START] para Jugar", RGB(255, 255, 255));
            draw_text(125, 210, "Stick / Cruceta: Mover | X: Lanzar Bola", RGB(180, 200, 230));
        } else if (gameOver) {
            draw_text(190, 95, "GAME OVER", RGB(255, 70, 90));
            sprintf(buf, "PUNTOS: %d  RECORD: %d", score, best);
            draw_text(148, 130, buf, RGB(255, 255, 255));
            draw_text(155, 165, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else {
            unsigned int rcols[6] = { RGB(255,60,90), RGB(255,150,40), RGB(255,230,60), RGB(60,235,120), RGB(0,220,255), RGB(180,90,255) };
            for (int r = 0; r < ROWS; r++)
                for (int c = 0; c < COLS; c++)
                    if (bricks[r][c] > 0)
                        fill_rect(12 + c * 38, 32 + r * 16, 34, 12, bricks[r][c] > 1 ? RGB(230, 240, 255) : rcols[r]);
            for (int p = 0; p < 6; p++) if (pups[p].alive) fill_rect((int)pups[p].x - 5, (int)pups[p].y - 5, 10, 10, pups[p].type ? RGB(255, 230, 60) : RGB(60, 255, 140));
            for (int i = 0; i < 4; i++) if (balls[i].alive) fill_rect((int)balls[i].x - 3, (int)balls[i].y - 3, 6, 6, RGB(255, 255, 255));
            fill_rect((int)(padX - padW * 0.5f), 246, (int)padW, 8, RGB(0, 220, 255));
            sprintf(buf, "VIDAS:%d  NIVEL:%d  PTS:%d", lives, level, score);
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
