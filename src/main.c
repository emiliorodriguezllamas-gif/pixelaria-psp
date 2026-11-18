#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("DinoDash", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define GROUND_Y 224
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

#include "sprites_pack.h"
#include "bg_synth.h"
static unsigned int * const fb_ptrs[2] = { (unsigned int *)0x44000000, (unsigned int *)(0x44000000 + BUF_W * SCR_H * 4) };
static int cur_fb = 0;
#define fb0 (fb_ptrs[cur_fb])
static int running = 1;
static unsigned int rng = 0x22446688u;
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
    const float bass[4] = { 130.81f, 146.83f, 164.81f, 196.00f };
    while (arun) {
        short *o = abuf[cur];
        for (int i = 0; i < 1024; i++) {
            float m = 0.0f;
            if (sfx_on) {
                int st = (int)(bgm_t * 5.0f);
                float bf = bass[st & 3];
                float bt = bgm_t * bf - floorf(bgm_t * bf);
                m += (bt < 0.5f ? 1.0f : -1.0f) * 0.09f;
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

typedef struct { float x, y; int w, h, isBird, alive; } Obs;
static Obs obs[6];
static float py = GROUND_Y, pvy = 0, spd = 4.0f;
static int onGround = 1, ducking = 0, score = 0, best = 0, spawnT = 0;
static int inTitle = 1, gameOver = 0;

static void load_best(void) { FILE *f = fopen("dd_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("dd_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static void start_game(void) {
    py = GROUND_Y; pvy = 0; spd = 4.0f; onGround = 1; ducking = 0; score = 0; spawnT = 0;
    memset(obs, 0, sizeof(obs));
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
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("dd_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

    SceCtrlData pad; unsigned int prev = 0;
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;
        if (pressed & PSP_CTRL_SELECT) sfx_on = !sfx_on;

        if (inTitle || gameOver) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        } else {
            ducking = (pad.Buttons & (PSP_CTRL_DOWN | PSP_CTRL_CIRCLE)) ? 1 : 0;
            if ((pressed & (PSP_CTRL_CROSS | PSP_CTRL_UP)) && onGround) {
                pvy = -8.4f; onGround = 0; play_sfx(560.0f, 1000);
            }
            pvy += 0.44f;
            py += pvy;
            if (py >= GROUND_Y) { py = GROUND_Y; pvy = 0; onGround = 1; }

            spd += 0.0015f;
            if (spd > 9.5f) spd = 9.5f;
            score++;

            spawnT++;
            if (spawnT > 55 + (int)(xr() % 35)) {
                spawnT = 0;
                for (int i = 0; i < 6; i++) if (!obs[i].alive) {
                    obs[i].alive = 1;
                    obs[i].x = SCR_W + 20;
                    obs[i].isBird = ((xr() % 3) == 0);
                    obs[i].w = obs[i].isBird ? 22 : 16;
                    obs[i].h = obs[i].isBird ? 14 : 28;
                    obs[i].y = obs[i].isBird ? (GROUND_Y - 28) : GROUND_Y;
                    break;
                }
            }

            int ph = ducking ? 16 : 28;
            int pw = 18;
            for (int i = 0; i < 6; i++) if (obs[i].alive) {
                obs[i].x -= spd;
                if (obs[i].x < -30) { obs[i].alive = 0; continue; }
                if (obs[i].x < 70 + pw && obs[i].x + obs[i].w > 70 &&
                    py > obs[i].y - obs[i].h && py - ph < obs[i].y) {
                    gameOver = 1; save_best(); play_sfx(120.0f, 3000);
                }
            }
        }

        fill_rect(0, 0, SCR_W, SCR_H, RGB(120, 200, 255));
        fill_rect(0, GROUND_Y, SCR_W, SCR_H - GROUND_Y, RGB(210, 175, 110));
        fill_rect(0, GROUND_Y, SCR_W, 4, RGB(80, 170, 70));

        char buf[64];
        if (inTitle) {
            for (int _y = 0; _y < SCR_H; _y++) memcpy(&fb0[_y * BUF_W], &bg_synth[_y * SCR_W], SCR_W * 4);
            draw_text(185, 78, "D I N O   D A S H", RGB(20, 40, 80));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(188, 118, buf, RGB(20, 100, 40));
            draw_text(148, 158, "Pulsa [X] o [START] para Correr", RGB(255, 255, 255));
            draw_text(125, 204, "X / Arriba: Saltar | O / Abajo: Agacharse", RGB(30, 50, 90));
        } else if (gameOver) {
            draw_text(190, 95, "EXTINCION!", RGB(220, 40, 50));
            sprintf(buf, "PUNTOS: %d  RECORD: %d", score, best);
            draw_text(148, 130, buf, RGB(20, 40, 80));
            draw_text(155, 165, "Pulsa [X] para Reintentar", RGB(255, 255, 255));
        } else {
            for (int i = 0; i < 6; i++) if (obs[i].alive) {
                fill_rect((int)obs[i].x, (int)obs[i].y - obs[i].h, obs[i].w, obs[i].h, obs[i].isBird ? RGB(180, 60, 180) : RGB(40, 140, 50));
            }
            int ph = ducking ? 16 : 28;
            fill_rect(70, (int)py - ph, 18, ph, RGB(50, 180, 70));
            sprintf(buf, "PTS:%d  RECORD:%d", score, best);
            draw_text(10, 8, buf, RGB(20, 40, 80));
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
