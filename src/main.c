#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("BlockStackerDX", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define BW 10
#define BH 20
#define CELL 12
#define OX 180
#define OY 16
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

static unsigned int __attribute__((aligned(16))) fb0[BUF_W * SCR_H];
static int running = 1;
static unsigned int rng = 0xAABBCCDDu;
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
    const float bass[4] = { 130.81f, 146.83f, 164.81f, 130.81f };
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

static const int shapes[7][4][4] = {
    {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}}, /* I */
    {{1,1,0,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}}, /* O */
    {{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}}, /* T */
    {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}}, /* S */
    {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}, /* Z */
    {{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}}, /* J */
    {{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}}  /* L */
};

static int board[BH][BW];
static int curP[4][4], curType = 0, px = 3, py = 0;
static int score = 0, lines = 0, best = 0, dropT = 0;
static int inTitle = 1, gameOver = 0;

static void load_best(void) { FILE *f = fopen("bs_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("bs_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static int fits(int p[4][4], int nx, int ny) {
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) if (p[y][x]) {
        int bx = nx + x, by = ny + y;
        if (bx < 0 || bx >= BW || by >= BH) return 0;
        if (by >= 0 && board[by][bx]) return 0;
    }
    return 1;
}

static void spawn_piece(void) {
    curType = xr() % 7;
    memcpy(curP, shapes[curType], sizeof(curP));
    px = 3; py = 0;
    if (!fits(curP, px, py)) { gameOver = 1; save_best(); play_sfx(140.0f, 3000); }
}

static void lock_piece(void) {
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++)
        if (curP[y][x] && py + y >= 0) board[py + y][px + x] = curType + 1;
    int cleared = 0;
    for (int y = BH - 1; y >= 0; y--) {
        int full = 1;
        for (int x = 0; x < BW; x++) if (!board[y][x]) { full = 0; break; }
        if (full) {
            cleared++;
            for (int yy = y; yy > 0; yy--) memcpy(board[yy], board[yy - 1], sizeof(board[0]));
            memset(board[0], 0, sizeof(board[0]));
            y++;
        }
    }
    if (cleared > 0) { lines += cleared; score += cleared * cleared * 100; play_sfx(880.0f, 1500); }
    else play_sfx(320.0f, 600);
    spawn_piece();
}

static void start_game(void) {
    memset(board, 0, sizeof(board));
    score = 0; lines = 0; dropT = 0;
    inTitle = 0; gameOver = 0;
    spawn_piece();
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
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("bs_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

    SceCtrlData pad; unsigned int prev = 0;
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;
        if (pressed & PSP_CTRL_SELECT) sfx_on = !sfx_on;

        if (inTitle || gameOver) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        } else {
            if ((pressed & PSP_CTRL_LEFT) && fits(curP, px - 1, py)) px--;
            if ((pressed & PSP_CTRL_RIGHT) && fits(curP, px + 1, py)) px++;
            if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_UP)) {
                int rot[4][4];
                for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) rot[x][3 - y] = curP[y][x];
                if (fits(rot, px, py)) { memcpy(curP, rot, sizeof(curP)); play_sfx(520.0f, 500); }
            }
            if (pressed & PSP_CTRL_TRIANGLE) {
                while (fits(curP, px, py + 1)) py++;
                lock_piece();
            }

            int spd = (pad.Buttons & PSP_CTRL_DOWN) ? 3 : (26 - (lines / 5) * 2);
            if (spd < 4) spd = 4;
            if (++dropT >= spd) {
                dropT = 0;
                if (fits(curP, px, py + 1)) py++;
                else lock_piece();
            }
        }

        fill_rect(0, 0, SCR_W, SCR_H, RGB(12, 14, 28));
        char buf[64];
        if (inTitle) {
            draw_text(155, 80, "B L O C K   S T A C K E R   D X", RGB(0, 235, 255));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(188, 120, buf, RGB(140, 255, 160));
            draw_text(148, 160, "Pulsa [X] o [START] para Jugar", RGB(255, 255, 255));
            draw_text(95, 204, "Cruceta: Mover | X: Rotar | Tri: Caida Rapida", RGB(190, 200, 230));
        } else if (gameOver) {
            draw_text(190, 95, "GAME OVER", RGB(255, 70, 90));
            sprintf(buf, "PUNTOS: %d  RECORD: %d", score, best);
            draw_text(148, 130, buf, RGB(255, 255, 255));
            draw_text(155, 165, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else {
            unsigned int pcols[8] = { 0, RGB(0,230,255), RGB(255,230,60), RGB(180,80,255), RGB(80,240,100), RGB(255,60,80), RGB(60,120,255), RGB(255,150,40) };
            draw_outline(OX - 1, OY - 1, BW * CELL + 2, BH * CELL + 2, RGB(80, 110, 160));
            for (int y = 0; y < BH; y++) for (int x = 0; x < BW; x++)
                if (board[y][x]) fill_rect(OX + x * CELL, OY + y * CELL, CELL - 1, CELL - 1, pcols[board[y][x]]);

            int gy = py;
            while (fits(curP, px, gy + 1)) gy++;
            for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) if (curP[y][x]) {
                draw_outline(OX + (px + x) * CELL, OY + (gy + y) * CELL, CELL - 1, CELL - 1, RGB(90, 100, 130));
                fill_rect(OX + (px + x) * CELL, OY + (py + y) * CELL, CELL - 1, CELL - 1, pcols[curType + 1]);
            }

            sprintf(buf, "PTS:%d", score); draw_text(320, 40, buf, RGB(255, 235, 100));
            sprintf(buf, "LINEAS:%d", lines); draw_text(320, 60, buf, RGB(140, 255, 170));
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
