#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("BeatPulse", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define HIT_Y 224
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

#include "sprites_pack.h"
#include "bg_synth.h"
static unsigned int * const fb_ptrs[2] = { (unsigned int *)0x44000000, (unsigned int *)(0x44000000 + BUF_W * SCR_H * 4) };
static int cur_fb = 0;
#define fb0 (fb_ptrs[cur_fb])
static int running = 1;
static unsigned int rng = 0x44332211u;
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
    const float bass[8] = { 65.41f, 65.41f, 77.78f, 87.31f, 98.00f, 87.31f, 77.78f, 58.27f };
    const float arp[8]  = { 261.63f, 329.63f, 392.00f, 523.25f, 392.00f, 329.63f, 293.66f, 392.00f };
    while (arun) {
        short *o = abuf[cur];
        for (int i = 0; i < 1024; i++) {
            float m = 0.0f;
            if (sfx_on) {
                int st = (int)(bgm_t * 4.0f);
                float bf = bass[st & 7], af = arp[st & 7];
                float sub = bgm_t * 4.0f - floorf(bgm_t * 4.0f);
                float bt = bgm_t * bf - floorf(bgm_t * bf);
                float at = bgm_t * af - floorf(bgm_t * af);
                m += (2.0f * bt - 1.0f) * 0.12f * expf(-sub * 2.0f) + (at < 0.35f ? 1.0f : -1.0f) * 0.05f * expf(-sub * 3.0f);
                if (sfx_t > 0) {
                    sfx_p += sfx_f / 44100.0f;
                    m += sinf(sfx_p * 6.283f) * 0.20f * ((float)sfx_t / 2500.0f);
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

typedef struct { int lane; float y; int active; } Note;
static Note notes[24];
static int combo = 0, score = 0, best = 0, life = 100, tick = 0;
static int inTitle = 1, gameOver = 0;
static char judge[24] = "";
static int judgeT = 0;

static void load_best(void) { FILE *f = fopen("bp_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("bp_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static void start_game(void) {
    combo = 0; score = 0; life = 100; tick = 0; judgeT = 0;
    memset(notes, 0, sizeof(notes));
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
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("bp_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

    SceCtrlData pad; unsigned int prev = 0;
    const unsigned int laneBtns[4] = { PSP_CTRL_SQUARE, PSP_CTRL_TRIANGLE, PSP_CTRL_CIRCLE, PSP_CTRL_CROSS };
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;
        if (pressed & PSP_CTRL_SELECT) sfx_on = !sfx_on;

        if (inTitle || gameOver) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        } else {
            if (judgeT > 0) judgeT--;
            tick++;
            if ((tick % 22) == 0) {
                for (int i = 0; i < 24; i++) if (!notes[i].active) {
                    notes[i].active = 1; notes[i].lane = xr() & 3; notes[i].y = 16; break;
                }
            }

            for (int l = 0; l < 4; l++) if (pressed & laneBtns[l]) {
                int hit = -1; float bestD = 28.0f;
                for (int i = 0; i < 24; i++) if (notes[i].active && notes[i].lane == l) {
                    float d = fabsf(notes[i].y - HIT_Y);
                    if (d < bestD) { bestD = d; hit = i; }
                }
                if (hit >= 0) {
                    notes[hit].active = 0; combo++;
                    int pts = (bestD < 10.0f) ? 100 : 50;
                    score += pts + combo * 5;
                    snprintf(judge, sizeof(judge), bestD < 10.0f ? "PERFECT!" : "GOOD");
                    judgeT = 25;
                    play_sfx(523.25f + l * 80.0f, 900);
                }
            }

            float spd = 2.8f + (score / 4000.0f);
            if (spd > 5.5f) spd = 5.5f;
            for (int i = 0; i < 24; i++) if (notes[i].active) {
                notes[i].y += spd;
                if (notes[i].y > SCR_H) {
                    notes[i].active = 0; combo = 0; life -= 10;
                    snprintf(judge, sizeof(judge), "MISS"); judgeT = 25;
                    if (life <= 0) { life = 0; gameOver = 1; save_best(); }
                }
            }
        }

        fill_rect(0, 0, SCR_W, SCR_H, RGB(12, 10, 26));
        char buf[64];
        if (inTitle) {
            for (int _y = 0; _y < SCR_H; _y++) memcpy(&fb0[_y * BUF_W], &bg_synth[_y * SCR_W], SCR_W * 4);
            draw_text(175, 80, "B E A T   P U L S E", RGB(255, 80, 220));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(188, 120, buf, RGB(140, 255, 160));
            draw_text(148, 160, "Pulsa [X] o [START] para Jugar", RGB(255, 255, 255));
            draw_text(105, 204, "Carriles: []  Tri  O  X  (al ritmo de la musica)", RGB(190, 200, 230));
        } else if (gameOver) {
            draw_text(190, 95, "FIN DEL SHOW!", RGB(255, 70, 90));
            sprintf(buf, "PUNTOS: %d  RECORD: %d", score, best);
            draw_text(148, 130, buf, RGB(255, 255, 255));
            draw_text(155, 165, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else {
            unsigned int lcols[4] = { RGB(255,80,180), RGB(80,255,140), RGB(255,90,90), RGB(0,220,255) };
            for (int l = 0; l < 4; l++) {
                int lx = 140 + l * 50;
                fill_rect(lx, 20, 44, SCR_H - 30, RGB(22, 20, 42));
                draw_outline(lx + 2, HIT_Y - 8, 40, 16, lcols[l]);
            }
            for (int i = 0; i < 24; i++) if (notes[i].active) {
                int lx = 140 + notes[i].lane * 50 + 6;
                fill_rect(lx, (int)notes[i].y - 6, 32, 12, lcols[notes[i].lane]);
            }
            if (judgeT > 0) draw_text(210, 180, judge, RGB(255, 235, 100));
            sprintf(buf, "VIDA:%d%%  COMBO:x%d  PTS:%d", life, combo, score);
            draw_text(120, 6, buf, RGB(255, 235, 100));
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
