#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("RoboJump", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

static unsigned int __attribute__((aligned(16))) fb0[BUF_W * SCR_H];
static int running = 1;
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
    const float bass[4] = { 110.00f, 130.81f, 146.83f, 164.81f };
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

static float px = 30, py = 230, pvy = 0;
static int grav = 1, onSurface = 1, level = 1, score = 0, best = 0;
static int inTitle = 1, gameOver = 0;

static void load_best(void) { FILE *f = fopen("rj_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("rj_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static void start_game(void) {
    px = 30; py = 230; pvy = 0; grav = 1; onSurface = 1; level = 1; score = 0;
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
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("rj_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

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
            px += ax * 3.0f;
            if (px < 12) px = 12;

            if ((pressed & PSP_CTRL_CROSS) && onSurface) {
                pvy = -7.2f * grav; onSurface = 0; play_sfx(520.0f, 1000);
            }
            if ((pressed & PSP_CTRL_TRIANGLE) && onSurface) {
                grav = -grav; onSurface = 0; play_sfx(780.0f, 1200);
            }

            pvy += 0.38f * grav;
            py += pvy;
            if (py >= 238) { py = 238; pvy = 0; if (grav > 0) onSurface = 1; }
            if (py <= 34)  { py = 34;  pvy = 0; if (grav < 0) onSurface = 1; }

            /* Hazards */
            for (int h = 0; h < 3 + (level % 3); h++) {
                int hx = 110 + h * 95;
                int hy = (h & 1) ? 32 : 240;
                if (fabsf(px - hx) < 12 && fabsf(py - hy) < 14) {
                    gameOver = 1; save_best(); play_sfx(120.0f, 3000);
                }
            }

            /* Goal Portal */
            if (px > 445) {
                level++; score += 250 * level;
                px = 30; py = 230; pvy = 0; grav = 1;
                play_sfx(880.0f, 2000);
            }
        }

        fill_rect(0, 0, SCR_W, SCR_H, RGB(14, 18, 32));
        fill_rect(0, 24, SCR_W, 8, RGB(60, 80, 120));
        fill_rect(0, 240, SCR_W, 32, RGB(60, 80, 120));

        char buf[64];
        if (inTitle) {
            draw_text(180, 80, "R O B O - J U M P", RGB(0, 240, 255));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(188, 120, buf, RGB(140, 255, 160));
            draw_text(148, 160, "Pulsa [X] o [START] para Jugar", RGB(255, 255, 255));
            draw_text(110, 200, "Stick: Mover | X: Saltar | Tri: Invertir Gravedad", RGB(190, 200, 230));
        } else if (gameOver) {
            draw_text(190, 95, "CORTOCIRCUITO!", RGB(255, 70, 90));
            sprintf(buf, "PUNTOS: %d  RECORD: %d", score, best);
            draw_text(148, 130, buf, RGB(255, 255, 255));
            draw_text(155, 165, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else {
            for (int h = 0; h < 3 + (level % 3); h++) {
                int hx = 110 + h * 95;
                int hy = (h & 1) ? 32 : 230;
                fill_rect(hx - 6, hy, 12, 10, RGB(255, 60, 80));
            }
            fill_rect(450, 110, 16, 40, RGB(80, 255, 140));
            fill_rect((int)px - 6, (int)py - 8, 12, 16, RGB(0, 220, 255));
            sprintf(buf, "NIVEL:%d  PTS:%d", level, score);
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
