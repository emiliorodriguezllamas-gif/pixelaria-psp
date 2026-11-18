#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("RetroTennis", 0, 1, 0);
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
    const float bass[4] = { 130.81f, 164.81f, 196.00f, 164.81f };
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

static float px = 240, py = 230, cx = 240, cy = 42;
static float bx = 240, by = 136, bvx = 2.0f, bvy = 3.2f;
static int pPts = 0, cPts = 0, wins = 0, best = 0;
static int inTitle = 1, matchOver = 0;

static void load_best(void) { FILE *f = fopen("rt_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (wins > best) { best = wins; FILE *f = fopen("rt_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static void reset_ball(int toPlayer) {
    bx = 240; by = 136;
    bvx = ((pPts + cPts) & 1) ? 2.2f : -2.2f;
    bvy = toPlayer ? 3.2f : -3.2f;
}

static void start_match(void) {
    px = 240; py = 230; cx = 240; cy = 42;
    pPts = 0; cPts = 0;
    reset_ball(1);
    inTitle = 0; matchOver = 0;
    play_sfx(660.0f, 2000);
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
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("rt_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

    SceCtrlData pad; unsigned int prev = 0;
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;
        if (pressed & PSP_CTRL_SELECT) sfx_on = !sfx_on;

        if (inTitle || matchOver) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_match();
        } else {
            float ax = ((float)pad.Lx - 128.0f) / 128.0f, ay = ((float)pad.Ly - 128.0f) / 128.0f;
            if (fabsf(ax) < 0.18f) ax = 0; if (fabsf(ay) < 0.18f) ay = 0;
            if (pad.Buttons & PSP_CTRL_LEFT) ax = -1; if (pad.Buttons & PSP_CTRL_RIGHT) ax = 1;
            if (pad.Buttons & PSP_CTRL_UP) ay = -1; if (pad.Buttons & PSP_CTRL_DOWN) ay = 1;
            px += ax * 3.8f; py += ay * 2.8f;
            if (px < 60) px = 60; if (px > SCR_W - 60) px = SCR_W - 60;
            if (py < 150) py = 150; if (py > 250) py = 250;

            /* CPU */
            if (cx < bx - 4) cx += 2.7f; else if (cx > bx + 4) cx -= 2.7f;
            if (cx < 60) cx = 60; if (cx > SCR_W - 60) cx = SCR_W - 60;

            bx += bvx; by += bvy;
            if (bx < 56 || bx > SCR_W - 56) bvx = -bvx;

            /* Player hit */
            if (bvy > 0 && fabsf(bx - px) < 22 && fabsf(by - py) < 16) {
                int smash = (pad.Buttons & PSP_CTRL_SQUARE) ? 1 : 0;
                bvy = smash ? -5.6f : -3.8f;
                bvx = (bx - px) * 0.22f;
                play_sfx(smash ? 820.0f : 620.0f, 900);
            }
            /* CPU hit */
            if (bvy < 0 && fabsf(bx - cx) < 20 && fabsf(by - cy) < 14) {
                bvy = 3.8f;
                bvx = (bx - cx) * 0.18f;
                play_sfx(520.0f, 800);
            }

            if (by < 14) {
                pPts++; play_sfx(880.0f, 1500);
                if (pPts >= 7) { wins++; save_best(); matchOver = 1; } else reset_ball(0);
            } else if (by > SCR_H - 14) {
                cPts++; play_sfx(200.0f, 1500);
                if (cPts >= 7) { wins = 0; matchOver = 1; } else reset_ball(1);
            }
        }

        fill_rect(0, 0, SCR_W, SCR_H, RGB(35, 125, 60));
        draw_outline(50, 16, SCR_W - 100, SCR_H - 32, RGB(245, 245, 245));
        fill_rect(50, SCR_H / 2 - 1, SCR_W - 100, 3, RGB(230, 230, 230));

        char buf[64];
        if (inTitle) {
            for (int _y = 0; _y < SCR_H; _y++) memcpy(&fb0[_y * BUF_W], &bg_synth[_y * SCR_W], SCR_W * 4);
            draw_text(170, 80, "R E T R O   T E N N I S", RGB(255, 230, 80));
            sprintf(buf, "RACHA RECORD: %d PARTIDOS", best);
            draw_text(165, 120, buf, RGB(255, 255, 255));
            draw_text(148, 160, "Pulsa [X] o [START] para Sacar", RGB(255, 255, 255));
            draw_text(115, 204, "Stick: Mover | []: Remate Rapido (Smash)", RGB(220, 245, 220));
        } else if (matchOver) {
            draw_text(180, 95, pPts >= 7 ? "PARTIDO GANADO!" : "PARTIDO PERDIDO", RGB(255, 230, 80));
            sprintf(buf, "TU:%d  CPU:%d  RACHA:%d", pPts, cPts, wins);
            draw_text(160, 130, buf, RGB(255, 255, 255));
            draw_text(155, 165, "Pulsa [X] para Siguiente Partido", RGB(220, 255, 220));
        } else {
            fill_rect((int)px - 16, (int)py - 6, 32, 12, RGB(50, 160, 255));
            fill_rect((int)cx - 16, (int)cy - 6, 32, 12, RGB(240, 60, 80));
            fill_rect((int)bx - 4, (int)by - 4, 8, 8, RGB(230, 255, 60));
            sprintf(buf, "TU:%d   CPU:%d   (A 7 PTS)", pPts, cPts);
            draw_text(160, 4, buf, RGB(255, 255, 255));
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
