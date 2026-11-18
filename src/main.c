#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("StreetHoopz", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define FLOOR_Y 236
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
                int st = (int)(bgm_t * 4.5f);
                float bf = bass[st & 3];
                float bt = bgm_t * bf - floorf(bgm_t * bf);
                m += (2.0f * bt - 1.0f) * 0.09f;
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

static float px = 140, py = FLOOR_Y, pvy = 0;
static float cx = 340, cy = FLOOR_Y, cvy = 0;
static float bx = 240, by = 140, bvx = 0, bvy = 0;
static int owner = 0; /* 0=free, 1=player, 2=cpu */
static int pScore = 0, cScore = 0, timer = 60 * 60, best = 0;
static int inTitle = 1, gameOver = 0;

static void load_best(void) { FILE *f = fopen("sh_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (pScore > best) { best = pScore; FILE *f = fopen("sh_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static void start_game(void) {
    px = 140; py = FLOOR_Y; pvy = 0;
    cx = 340; cy = FLOOR_Y; cvy = 0;
    bx = 240; by = 120; bvx = 0; bvy = 0; owner = 0;
    pScore = 0; cScore = 0; timer = 60 * 60;
    inTitle = 0; gameOver = 0;
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
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("sh_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

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

            float ax = ((float)pad.Lx - 128.0f) / 128.0f;
            if (fabsf(ax) < 0.2f) ax = 0;
            if (pad.Buttons & PSP_CTRL_LEFT) ax = -1;
            if (pad.Buttons & PSP_CTRL_RIGHT) ax = 1;
            px += ax * 3.2f;
            if (px < 20) px = 20; if (px > SCR_W - 20) px = SCR_W - 20;

            if ((pressed & PSP_CTRL_CROSS) && py >= FLOOR_Y) { pvy = -8.6f; play_sfx(440.0f, 800); }
            pvy += 0.42f; py += pvy;
            if (py >= FLOOR_Y) { py = FLOOR_Y; pvy = 0; }

            if (pressed & PSP_CTRL_SQUARE) {
                if (owner == 1) {
                    owner = 0;
                    float dx = 445.0f - px, dy = 115.0f - (py - 28);
                    bvx = dx * 0.032f; bvy = -7.8f + dy * 0.02f;
                    play_sfx(660.0f, 1000);
                } else if (owner == 2 && fabsf(px - cx) < 28 && fabsf(py - cy) < 28) {
                    owner = 1; play_sfx(780.0f, 900);
                }
            }

            /* CPU */
            if (owner == 2) {
                cx -= 2.0f;
                if (cx < 140) {
                    owner = 0;
                    bvx = (35.0f - cx) * 0.032f; bvy = -7.8f;
                }
            } else {
                float tx = (owner == 1) ? px + 24 : bx;
                if (cx < tx - 4) cx += 2.1f; else if (cx > tx + 4) cx -= 2.1f;
            }
            if (cx < 20) cx = 20; if (cx > SCR_W - 20) cx = SCR_W - 20;

            /* Ball */
            if (owner == 1) { bx = px + 8; by = py - 24; }
            else if (owner == 2) { bx = cx - 8; by = cy - 24; }
            else {
                bvy += 0.34f; bx += bvx; by += bvy;
                if (bx < 10) { bx = 10; bvx = fabsf(bvx) * 0.7f; }
                if (bx > SCR_W - 10) { bx = SCR_W - 10; bvx = -fabsf(bvx) * 0.7f; }
                if (by > FLOOR_Y - 5) { by = FLOOR_Y - 5; bvy = -fabsf(bvy) * 0.65f; bvx *= 0.85f; }
                if (fabsf(bx - px) < 16 && fabsf(by - (py - 16)) < 20 && bvy > -2.0f) owner = 1;
                else if (fabsf(bx - cx) < 16 && fabsf(by - (cy - 16)) < 20 && bvy > -2.0f) owner = 2;

                /* Hoops */
                if (fabsf(bx - 445) < 12 && fabsf(by - 120) < 10 && bvy > 0) {
                    pScore += 2; bx = 240; by = 100; bvx = 0; bvy = 0; play_sfx(880.0f, 1800);
                }
                if (fabsf(bx - 35) < 12 && fabsf(by - 120) < 10 && bvy > 0) {
                    cScore += 2; bx = 240; by = 100; bvx = 0; bvy = 0; play_sfx(300.0f, 1800);
                }
            }
        }

        fill_rect(0, 0, SCR_W, SCR_H, RGB(28, 36, 58));
        fill_rect(0, FLOOR_Y, SCR_W, SCR_H - FLOOR_Y, RGB(180, 120, 60));
        fill_rect(25, 118, 22, 5, RGB(255, 80, 60));  /* Left Hoop */
        fill_rect(433, 118, 22, 5, RGB(255, 80, 60)); /* Right Hoop */

        char buf[64];
        if (inTitle) {
            for (int _y = 0; _y < SCR_H; _y++) memcpy(&fb0[_y * BUF_W], &bg_synth[_y * SCR_W], SCR_W * 4);
            draw_text(170, 80, "S T R E E T   H O O P Z", RGB(255, 180, 60));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(188, 120, buf, RGB(140, 255, 160));
            draw_text(148, 160, "Pulsa [X] o [START] para Jugar", RGB(255, 255, 255));
            draw_text(110, 204, "Stick: Correr | X: Saltar | []: Tirar / Robar", RGB(190, 210, 240));
        } else if (gameOver) {
            draw_text(185, 95, pScore >= cScore ? "VICTORIA EN CANCHA!" : "FIN DEL PARTIDO", RGB(255, 220, 80));
            sprintf(buf, "TU:%d  CPU:%d  RECORD:%d", pScore, cScore, best);
            draw_text(145, 130, buf, RGB(255, 255, 255));
            draw_text(155, 165, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else {
            fill_rect((int)px - 8, (int)py - 32, 16, 32, RGB(50, 150, 255));
            fill_rect((int)cx - 8, (int)cy - 32, 16, 32, RGB(230, 60, 70));
            fill_rect((int)bx - 5, (int)by - 5, 10, 10, RGB(255, 140, 40));
            sprintf(buf, "TU:%d  CPU:%d  TIEMPO:%ds", pScore, cScore, timer / 60);
            draw_text(145, 8, buf, RGB(255, 235, 100));
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
