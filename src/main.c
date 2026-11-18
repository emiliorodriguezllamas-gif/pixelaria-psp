#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("GemCascade", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define SZ 8
#define CELL 28
#define OX 128
#define OY 28
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

static unsigned int __attribute__((aligned(16))) fb0[BUF_W * SCR_H];
static int running = 1;
static unsigned int rng = 0x99881122u;
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
    const float bass[4] = { 130.81f, 164.81f, 196.00f, 146.83f };
    while (arun) {
        short *o = abuf[cur];
        for (int i = 0; i < 1024; i++) {
            float m = 0.0f;
            if (sfx_on) {
                int st = (int)(bgm_t * 4.0f);
                float bf = bass[st & 3];
                float bt = bgm_t * bf - floorf(bgm_t * bf);
                m += (bt < 0.5f ? 1.0f : -1.0f) * 0.07f;
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

static int grid[SZ][SZ];
static int cx = 3, cy = 3, sx = -1, sy = -1, score = 0, best = 0, timer = 90 * 60;
static int inTitle = 1, gameOver = 0;

static void load_best(void) { FILE *f = fopen("gc_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("gc_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static int clear_matches(void) {
    int mark[SZ][SZ];
    memset(mark, 0, sizeof(mark));
    int found = 0;
    for (int y = 0; y < SZ; y++) {
        for (int x = 0; x < SZ - 2; x++) {
            int c = grid[y][x];
            if (c && grid[y][x+1] == c && grid[y][x+2] == c) {
                mark[y][x] = mark[y][x+1] = mark[y][x+2] = 1;
            }
        }
    }
    for (int x = 0; x < SZ; x++) {
        for (int y = 0; y < SZ - 2; y++) {
            int c = grid[y][x];
            if (c && grid[y+1][x] == c && grid[y+2][x] == c) {
                mark[y][x] = mark[y+1][x] = mark[y+2][x] = 1;
            }
        }
    }
    for (int y = 0; y < SZ; y++) for (int x = 0; x < SZ; x++) if (mark[y][x]) { grid[y][x] = 0; found++; }
    if (found > 0) {
        score += found * 30;
        for (int x = 0; x < SZ; x++) {
            int wy = SZ - 1;
            for (int y = SZ - 1; y >= 0; y--) {
                if (grid[y][x]) grid[wy--][x] = grid[y][x];
            }
            while (wy >= 0) grid[wy--][x] = 1 + (xr() % 6);
        }
    }
    return found;
}

static void start_game(void) {
    for (int y = 0; y < SZ; y++) for (int x = 0; x < SZ; x++) grid[y][x] = 1 + (xr() % 6);
    while (clear_matches() > 0) {}
    score = 0; timer = 90 * 60; cx = 3; cy = 3; sx = -1; sy = -1;
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
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("gc_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

    SceCtrlData pad; unsigned int prev = 0;
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;
        if (pressed & PSP_CTRL_SELECT) sfx_on = !sfx_on;

        if (inTitle || gameOver) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        } else {
            if (--timer <= 0) { gameOver = 1; save_best(); play_sfx(520.0f, 3000); }
            if (pressed & PSP_CTRL_LEFT && cx > 0) cx--;
            if (pressed & PSP_CTRL_RIGHT && cx < SZ - 1) cx++;
            if (pressed & PSP_CTRL_UP && cy > 0) cy--;
            if (pressed & PSP_CTRL_DOWN && cy < SZ - 1) cy++;

            if (pressed & PSP_CTRL_CROSS) {
                if (sx < 0) { sx = cx; sy = cy; play_sfx(520.0f, 500); }
                else {
                    if (abs(cx - sx) + abs(cy - sy) == 1) {
                        int tmp = grid[cy][cx];
                        grid[cy][cx] = grid[sy][sx];
                        grid[sy][sx] = tmp;
                        int m = clear_matches();
                        if (m == 0) {
                            grid[sy][sx] = grid[cy][cx];
                            grid[cy][cx] = tmp;
                            play_sfx(200.0f, 700);
                        } else {
                            while (clear_matches() > 0) {}
                            play_sfx(880.0f, 1200);
                        }
                    }
                    sx = -1; sy = -1;
                }
            }
        }

        fill_rect(0, 0, SCR_W, SCR_H, RGB(16, 14, 30));
        char buf[64];
        if (inTitle) {
            draw_text(170, 80, "G E M   C A S C A D E", RGB(255, 225, 80));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(188, 120, buf, RGB(140, 255, 160));
            draw_text(148, 160, "Pulsa [X] o [START] para Jugar", RGB(255, 255, 255));
            draw_text(110, 204, "Cruceta: Cursor | X: Seleccionar e Intercambiar", RGB(190, 200, 230));
        } else if (gameOver) {
            draw_text(185, 95, "TIEMPO AGOTADO!", RGB(255, 220, 80));
            sprintf(buf, "PUNTOS: %d  RECORD: %d", score, best);
            draw_text(148, 130, buf, RGB(255, 255, 255));
            draw_text(155, 165, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else {
            unsigned int gcols[7] = { 0, RGB(255,60,80), RGB(60,220,255), RGB(80,255,120), RGB(255,220,60), RGB(200,80,255), RGB(255,140,40) };
            for (int y = 0; y < SZ; y++) for (int x = 0; x < SZ; x++) {
                fill_rect(OX + x * CELL + 3, OY + y * CELL + 3, CELL - 6, CELL - 6, gcols[grid[y][x]]);
            }
            if (sx >= 0) draw_outline(OX + sx * CELL + 1, OY + sy * CELL + 1, CELL - 2, CELL - 2, RGB(255, 255, 255));
            draw_outline(OX + cx * CELL, OY + cy * CELL, CELL, CELL, RGB(255, 235, 80));
            sprintf(buf, "TIEMPO:%ds  PTS:%d", timer / 60, score);
            draw_text(165, 8, buf, RGB(255, 235, 100));
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
