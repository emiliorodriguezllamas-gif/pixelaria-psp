#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspge.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"
#include "bg_arena.h"
#include "bg_title.h"
#include "world_sprites.h"

PSP_MODULE_INFO("SymphonyAbyss", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define HIT_Y 238
#define RGB(r,g,b) ((unsigned int)(0xFF000000u | ((unsigned int)(b)<<16) | ((unsigned int)(g)<<8) | (unsigned int)(r)))

static unsigned int __attribute__((aligned(16))) gulist[262144];
static unsigned int *vram_bufs[2];
static int cur_fb = 0;

static int running = 1;
static unsigned int rng = 0x77113355u;
static unsigned int xr(void) { unsigned int x = rng; x ^= x<<13; x ^= x>>17; x ^= x<<5; rng = x; return x; }
static int exit_cb(int a, int b, void *c) { (void)a; (void)b; (void)c; running = 0; return 0; }
static int cb_th(SceSize a, void *p) { (void)a; (void)p; sceKernelRegisterExitCallback(sceKernelCreateCallback("Exit", exit_cb, NULL)); sceKernelSleepThreadCB(); return 0; }

/* 2D Primitives & 32-bit Sprite Blitters */
static void put_px(unsigned int *fb, int x, int y, unsigned int c) {
    if ((unsigned)x < SCR_W && (unsigned)y < SCR_H) fb[y * BUF_W + x] = c;
}

static void blend_px(unsigned int *fb, int x, int y, unsigned int c, int a) {
    if ((unsigned)x >= SCR_W || (unsigned)y >= SCR_H) return;
    unsigned int dst = fb[y * BUF_W + x];
    int ia = 255 - a;
    unsigned int r = (((c & 0xFFu) * a + (dst & 0xFFu) * ia) >> 8) & 0xFFu;
    unsigned int g = ((((c >> 8) & 0xFFu) * a + ((dst >> 8) & 0xFFu) * ia) >> 8) & 0xFFu;
    unsigned int b = ((((c >> 16) & 0xFFu) * a + ((dst >> 16) & 0xFFu) * ia) >> 8) & 0xFFu;
    fb[y * BUF_W + x] = 0xFF000000u | (b << 16) | (g << 8) | r;
}

static void fill_rect(unsigned int *fb, int x, int y, int w, int h, unsigned int col) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x;
    if (y + h > SCR_H) h = SCR_H - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        unsigned int *row = &fb[(y + j) * BUF_W + x];
        for (int i = 0; i < w; i++) row[i] = col;
    }
}

static void fill_rect_alpha(unsigned int *fb, int x, int y, int w, int h, unsigned int col, int a) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x;
    if (y + h > SCR_H) h = SCR_H - y;
    if (w <= 0 || h <= 0) return;
    int ia = 255 - a;
    unsigned int sr = (col & 0xFFu) * a;
    unsigned int sg = ((col >> 8) & 0xFFu) * a;
    unsigned int sb = ((col >> 16) & 0xFFu) * a;
    for (int j = 0; j < h; j++) {
        unsigned int *row = &fb[(y + j) * BUF_W + x];
        for (int i = 0; i < w; i++) {
            unsigned int dst = row[i];
            unsigned int r = ((sr + (dst & 0xFFu) * ia) >> 8) & 0xFFu;
            unsigned int g = ((sg + ((dst >> 8) & 0xFFu) * ia) >> 8) & 0xFFu;
            unsigned int b = ((sb + ((dst >> 16) & 0xFFu) * ia) >> 8) & 0xFFu;
            row[i] = 0xFF000000u | (b << 16) | (g << 8) | r;
        }
    }
}

static void fill_ellipse_alpha(unsigned int *fb, int cx, int cy, int rx, int ry, unsigned int col, int a) {
    if (rx <= 0 || ry <= 0) return;
    int rx2 = rx * rx, ry2 = ry * ry;
    for (int dy = -ry; dy <= ry; dy++) {
        int py = cy + dy;
        if ((unsigned)py >= SCR_H) continue;
        for (int dx = -rx; dx <= rx; dx++) {
            if (dx * dx * ry2 + dy * dy * rx2 <= rx2 * ry2) {
                blend_px(fb, cx + dx, py, col, a);
            }
        }
    }
}

static void draw_border(unsigned int *fb, int x, int y, int w, int h, unsigned int col) {
    fill_rect(fb, x, y, w, 1, col);
    fill_rect(fb, x, y + h - 1, w, 1, col);
    fill_rect(fb, x, y, 1, h, col);
    fill_rect(fb, x + w - 1, y, 1, h, col);
}

/* Classic Final Fantasy Semi-Transparent Gradient Blue Window with golden corner rivets */
static void draw_ff_window(unsigned int *fb, int x, int y, int w, int h) {
    for (int j = 0; j < h; j++) {
        int r = 24 - (j * 14) / (h > 0 ? h : 1);
        int g = 34 - (j * 20) / (h > 0 ? h : 1);
        int b = 102 - (j * 54) / (h > 0 ? h : 1);
        fill_rect_alpha(fb, x + 2, y + j, w - 4, 1, RGB(r, g, b), 222);
    }
    draw_border(fb, x, y, w, h, RGB(230, 236, 255));
    draw_border(fb, x + 1, y + 1, w - 2, h - 2, RGB(65, 82, 145));
    draw_border(fb, x + 2, y + 2, w - 4, h - 4, RGB(175, 195, 240));
    fill_rect(fb, x, y, 3, 3, RGB(245, 205, 85));
    fill_rect(fb, x + w - 3, y, 3, 3, RGB(245, 205, 85));
    fill_rect(fb, x, y + h - 3, 3, 3, RGB(245, 205, 85));
    fill_rect(fb, x + w - 3, y + h - 3, 3, 3, RGB(245, 205, 85));
}

static void draw_text(unsigned int *fb, int x, int y, const char *s, unsigned int c) {
    while (*s) {
        unsigned char ch = (unsigned char)*s++;
        if (ch < 32 || ch > 127) ch = '?';
        const unsigned char *g = font5x7[ch - 32];
        for (int cx = 0; cx < 5; cx++) {
            unsigned char b = g[cx];
            for (int cy = 0; cy < 7; cy++) {
                if (b & (1 << cy)) {
                    put_px(fb, x + cx + 1, y + cy + 1, RGB(0, 0, 0)); /* drop shadow */
                    put_px(fb, x + cx, y + cy, c);
                }
            }
        }
        x += 6;
    }
}

static void draw_text_2x(unsigned int *fb, int x, int y, const char *s, unsigned int c) {
    while (*s) {
        unsigned char ch = (unsigned char)*s++;
        if (ch < 32 || ch > 127) ch = '?';
        const unsigned char *g = font5x7[ch - 32];
        for (int cx = 0; cx < 5; cx++) {
            unsigned char b = g[cx];
            for (int cy = 0; cy < 7; cy++) {
                if (b & (1 << cy)) {
                    fill_rect(fb, x + cx * 2 + 2, y + cy * 2 + 2, 2, 2, RGB(0, 0, 0));
                    fill_rect(fb, x + cx * 2, y + cy * 2, 2, 2, c);
                }
            }
        }
        x += 12;
    }
}

static void draw_tile24(unsigned int *fb, const unsigned int *tile, int dx, int dy) {
    for (int j = 0; j < 24; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &tile[j * 24];
        for (int i = 0; i < 24; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) dst[px] = src[i];
        }
    }
}

static void draw_tile24_scroll(unsigned int *fb, const unsigned int *tile, int dx, int dy, int sxOff) {
    for (int j = 0; j < 24; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &tile[j * 24];
        for (int i = 0; i < 24; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) dst[px] = src[(i + sxOff) % 24];
        }
    }
}

static void draw_spr32(unsigned int *fb, const unsigned int *spr, int dx, int dy) {
    for (int j = 0; j < 32; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &spr[j * 32];
        for (int i = 0; i < 32; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[i];
                if (c) dst[px] = c;
            }
        }
    }
}

static void draw_spr16(unsigned int *fb, const unsigned int *spr, int dx, int dy) {
    for (int j = 0; j < 16; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &spr[j * 16];
        for (int i = 0; i < 16; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[i];
                if (c) dst[px] = c;
            }
        }
    }
}

static void draw_spr20(unsigned int *fb, const unsigned int *spr, int dx, int dy) {
    for (int j = 0; j < 20; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &spr[j * 20];
        for (int i = 0; i < 20; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[i];
                if (c) dst[px] = c;
            }
        }
    }
}

static void draw_spr_lr24(unsigned int *fb, int dx, int dy) {
    for (int j = 0; j < 14; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &spr_btn_lr24[j * 24];
        for (int i = 0; i < 24; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[i];
                if (c) dst[px] = c;
            }
        }
    }
}

static void draw_spr24_dir(unsigned int *fb, const unsigned int *spr, int dx, int dy, int flipX) {
    for (int j = 0; j < 24; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &spr[j * 24];
        for (int i = 0; i < 24; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[flipX ? (23 - i) : i];
                if (c) dst[px] = c;
            }
        }
    }
}

static void draw_spr24(unsigned int *fb, const unsigned int *spr, int dx, int dy) {
    draw_spr24_dir(fb, spr, dx, dy, 0);
}

static void draw_spr48(unsigned int *fb, const unsigned int *spr, int dx, int dy) {
    for (int j = 0; j < 48; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &spr[j * 48];
        for (int i = 0; i < 48; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[i];
                if (c) dst[px] = c;
            }
        }
    }
}

static void draw_ellipse_ring(unsigned int *fb, int cx, int cy, int rx, int ry, unsigned int col) {
    for (int deg = 0; deg < 360; deg += 4) {
        float rad = (float)deg * 0.01745329f;
        int px = cx + (int)(cosf(rad) * (float)rx);
        int py = cy + (int)(sinf(rad) * (float)ry);
        put_px(fb, px, py, col);
        put_px(fb, px, py + 1, col);
    }
}

static void draw_boss64_dir(unsigned int *fb, const unsigned int *spr, int dx, int dy, int flash, int flipX) {
    for (int j = 0; j < 64; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &spr[j * 64];
        for (int i = 0; i < 64; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[flipX ? (63 - i) : i];
                if (c) dst[px] = flash ? RGB(255, 255, 255) : c;
            }
        }
    }
}

static void draw_boss64(unsigned int *fb, const unsigned int *spr, int dx, int dy, int flash) {
    draw_boss64_dir(fb, spr, dx, dy, flash, 0);
}

/* Epic 44.1kHz Multi-Track PSP Synthesizer: 5 Distinct Songs (Overworld, Mobs, Boss 1 Rock, Boss 2 Synth, Boss 3 Symphonic) */
static int ach = -1, ath = -1;
static volatile int arun = 0, sfx_t = 0, sfx_max = 3000, sfx_on = 1, curStyle = 0, curSong = 0;
static volatile int bgm_step = 0;
static float sfx_f = 440.0f, sfx_p = 0.0f, sfx_p2 = 0.0f, bgm_t = 0.0f;
static short abuf[2][1024 * 2];

static void play_sfx(float f, int dur) {
    if (sfx_on) { sfx_f = f; sfx_t = dur; sfx_max = dur > 0 ? dur : 1; sfx_p = 0.0f; sfx_p2 = 0.0f; }
}

/* Harmonic 4-lane scale frequencies (Root, b3/4, 5th, Octave) per song so Guitar Hero notes are 100% in tune! */
static const float songScale[5][4] = {
    { 293.66f, 349.23f, 440.00f, 587.33f }, /* 0: D Minor (Overworld / Title) */
    { 329.63f, 392.00f, 493.88f, 659.25f }, /* 1: E Minor (Forest Mobs) */
    { 164.81f, 220.00f, 246.94f, 329.63f }, /* 2: E Minor Heavy Rock (Boss 1: Golem) */
    { 261.63f, 311.13f, 392.00f, 523.25f }, /* 3: C Minor Synth (Boss 2: Automata) */
    { 293.66f, 349.23f, 440.00f, 587.33f }  /* 4: D Minor Symphonic (Boss 3: Dragon) */
};

static int audio_th(SceSize a, void *p) {
    (void)a; (void)p;
    int cur = 0;
    /* 16-step Basslines & Epic Lead Melodies for each of the 5 songs */
    static const float bass[5][16] = {
        { 146.83f,146.83f,174.61f,146.83f, 130.81f,130.81f,164.81f,130.81f, 116.54f,116.54f,146.83f,116.54f, 130.81f,146.83f,164.81f,110.00f }, /* 0: Adventure D-min */
        { 164.81f,164.81f,196.00f,164.81f, 146.83f,146.83f,174.61f,146.83f, 130.81f,130.81f,164.81f,130.81f, 146.83f,164.81f,196.00f,123.47f }, /* 1: Forest Mobs E-min */
        {  82.41f, 82.41f, 82.41f, 98.00f,  82.41f, 82.41f,110.00f, 98.00f,  73.42f, 73.42f, 98.00f, 73.42f,  82.41f,110.00f,123.47f, 98.00f }, /* 2: Boss 1 Heavy Rock E-min */
        { 130.81f,261.63f,130.81f,196.00f, 116.54f,233.08f,116.54f,174.61f, 103.83f,207.65f,103.83f,155.56f, 116.54f,146.83f,196.00f,233.08f }, /* 3: Boss 2 Dark Synth C-min */
        {  73.42f,146.83f,110.00f,146.83f,  65.41f,130.81f, 98.00f,130.81f,  58.27f,116.54f, 87.31f,116.54f,  65.41f, 73.42f,110.00f, 82.41f }  /* 4: Boss 3 Epic Symphonic D-min */
    };
    static const float lead[5][16] = {
        { 293.66f,349.23f,440.00f,392.00f, 349.23f,329.63f,293.66f,261.63f, 233.08f,293.66f,349.23f,329.63f, 293.66f,329.63f,440.00f,587.33f }, /* 0: Adventure */
        { 329.63f,392.00f,493.88f,392.00f, 440.00f,392.00f,329.63f,293.66f, 261.63f,329.63f,392.00f,329.63f, 349.23f,392.00f,493.88f,659.25f }, /* 1: Forest Mobs */
        { 329.63f,329.63f,392.00f,440.00f, 493.88f,440.00f,392.00f,329.63f, 293.66f,329.63f,392.00f,493.88f, 587.33f,493.88f,440.00f,392.00f }, /* 2: Boss 1 Rock */
        { 523.25f,392.00f,311.13f,392.00f, 466.16f,349.23f,293.66f,349.23f, 415.30f,311.13f,261.63f,311.13f, 466.16f,523.25f,622.25f,783.99f }, /* 3: Boss 2 Synth */
        { 587.33f,523.25f,440.00f,349.23f, 523.25f,440.00f,392.00f,329.63f, 466.16f,440.00f,349.23f,293.66f, 440.00f,523.25f,587.33f,880.00f }  /* 4: Boss 3 Symphonic */
    };
    static const float tempo[5] = { 3.7f, 4.1f, 4.4f, 4.6f, 4.8f };
    unsigned int noiseSeed = 0x13579BDFu;

    while (arun) {
        short *o = abuf[cur];
        int sg = curSong; if (sg < 0 || sg > 4) sg = 0;
        int stStyle = curStyle; if (stStyle < 0 || stStyle > 2) stStyle = 0;
        float spd = tempo[sg];

        for (int i = 0; i < 1024; i++) {
            float lSig = 0.0f, rSig = 0.0f;
            if (sfx_on) {
                float stepF = bgm_t * spd;
                int st = ((int)stepF) & 15;
                bgm_step = (int)stepF;
                float sub = stepF - (float)((int)stepF);

                /* 1. Bass Track (Warm sub + 5th harmonic) */
                float bf = bass[sg][st];
                float bp = bgm_t * bf; bp -= (float)((int)bp);
                float bassEnv = expf(-sub * 2.4f);
                float bassWave = (bp < 0.5f ? 0.5f : -0.5f) * 0.35f + sinf(bp * 6.28318f) * 0.65f;
                float bOut = bassWave * 0.14f * bassEnv;

                /* 2. Lead / Chord Instrument Track (shaped by Song & Active Musical Style) */
                float lf = lead[sg][st];
                float lp1 = bgm_t * lf; lp1 -= (float)((int)lp1);
                float lp2 = bgm_t * (lf * 1.498f); lp2 -= (float)((int)lp2); /* Power 5th harmony */
                float lp3 = bgm_t * (lf * 1.004f); lp3 -= (float)((int)lp3); /* Stereo detune chorus */
                float leadEnv = expf(-sub * 1.8f);
                float leadL = 0.0f, leadR = 0.0f;
                if (stStyle == 0 || sg == 2) {
                    /* Distorted Epic Rock Power-Chord Guitar */
                    float rawL = (2.0f * lp1 - 1.0f) * 0.7f + (2.0f * lp2 - 1.0f) * 0.45f;
                    float rawR = (2.0f * lp3 - 1.0f) * 0.7f + (2.0f * lp2 - 1.0f) * 0.45f;
                    leadL = (rawL / (1.0f + fabsf(rawL))) * 0.11f * leadEnv;
                    leadR = (rawR / (1.0f + fabsf(rawR))) * 0.11f * leadEnv;
                } else if (stStyle == 1 || sg == 3) {
                    /* Detuned Arcane Supersaw Synth */
                    leadL = ((2.0f * lp1 - 1.0f) * 0.6f + sinf(lp2 * 6.28318f) * 0.4f) * 0.10f * leadEnv;
                    leadR = ((2.0f * lp3 - 1.0f) * 0.6f + sinf(lp2 * 6.28318f) * 0.4f) * 0.10f * leadEnv;
                } else {
                    /* Symphonic Lush String Ensemble */
                    float vibrato = 1.0f + sinf(bgm_t * 32.0f) * 0.004f;
                    float sp = bgm_t * lf * vibrato; sp -= (float)((int)sp);
                    leadL = (sinf(sp * 6.28318f) * 0.65f + (2.0f * lp2 - 1.0f) * 0.25f) * 0.11f * (0.4f + 0.6f * sinf(sub * 3.14159f));
                    leadR = (sinf(lp3 * 6.28318f) * 0.65f + (2.0f * lp2 - 1.0f) * 0.25f) * 0.11f * (0.4f + 0.6f * sinf(sub * 3.14159f));
                }

                /* 3. Percussion Track (Kick on 0,4,8,12 | Snare on 2,6,10,14 | Hi-Hat on every step) */
                float drum = 0.0f;
                if ((st & 3) == 0 && sub < 0.35f) {
                    float kf = 115.0f * expf(-sub * 14.0f) + 42.0f;
                    drum += sinf(bgm_t * kf * 6.28318f) * 0.15f * expf(-sub * 12.0f);
                }
                noiseSeed = noiseSeed * 1664525u + 1013904223u;
                float nz = ((float)(int)(noiseSeed >> 16) / 32768.0f);
                if ((st & 3) == 2 && sub < 0.28f) {
                    drum += nz * 0.085f * expf(-sub * 16.0f); /* Snare */
                } else if (sub < 0.09f) {
                    drum += nz * 0.030f * expf(-sub * 32.0f); /* Hi-hat */
                }

                lSig = bOut + leadL + drum;
                rSig = bOut + leadR + drum;

                /* 4. In-Key Guitar Hero Note Pluck / SFX Overlay */
                if (sfx_t > 0) {
                    sfx_p  += sfx_f / 44100.0f;
                    sfx_p2 += (sfx_f * 1.5f) / 44100.0f;
                    if (sfx_p >= 1.0f) sfx_p -= 1.0f;
                    if (sfx_p2 >= 1.0f) sfx_p2 -= 1.0f;
                    float env = (float)sfx_t / (float)sfx_max;
                    float noteWave = (stStyle == 0)
                        ? ((2.0f * sfx_p - 1.0f) * 0.6f + (2.0f * sfx_p2 - 1.0f) * 0.4f)
                        : (sinf(sfx_p * 6.28318f) * 0.7f + sinf(sfx_p2 * 6.28318f) * 0.3f);
                    float sOut = noteWave * 0.20f * env;
                    lSig += sOut; rSig += sOut;
                    sfx_t--;
                }
            }
            bgm_t += 1.0f / 44100.0f;
            int vl = (int)(lSig * 32767.0f), vr = (int)(rSig * 32767.0f);
            if (vl > 32767) vl = 32767; else if (vl < -32767) vl = -32767;
            if (vr > 32767) vr = 32767; else if (vr < -32767) vr = -32767;
            o[i * 2] = (short)vl; o[i * 2 + 1] = (short)vr;
        }
        sceAudioOutputPannedBlocking(ach, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, o);
        cur ^= 1;
    }
    sceKernelExitDeleteThread(0);
    return 0;
}

typedef struct { int lane; float y; int active; } Note;
static Note notes[24];
static const char *bossNames[3] = { "GOLEM DE HIERRO", "AUTOMATA ARCANO", "DRAGON SOMBRIO" };
static const char *mobNames[3]  = { "SLIME DEL BOSQUE", "TRASGO ERRANTE", "ESPECTRO MENOR" };
static const char *styleNames[3] = { "ROCK", "SYNTH", "SINFONICO" };
static const int bossWeak[3] = { 0, 1, 2 };

static int chapter = 0, isMobBattle = 0, bossHitFlash = 0;
static int pHp = 100, pMaxHp = 100, pMp = 50, pMaxMp = 50, bHp = 120, bMaxHp = 120;
static int pLevel = 1, pXp = 0, pNextXp = 60, pBaseAtk = 5;
static int combo = 0, score = 0, best = 0, tick = 0, phaseTimer = 0;
static int relicDmg = 0, relicVamp = 0, relicShield = 0;
static int inTitle = 1, inOverworld = 0, inStory = 0, inFFMenu = 0, inDefense = 0, inAttackResolve = 0, inRelic = 0, gameOver = 0, victory = 0;
static int ffCursor = 0, pendingDmg = 0;
static float bardX = 240.0f, bardY = 195.0f;
static float projX[6], projY[6], projVx[6], projVy[6];
static int projAlive[6];
static char judge[32] = "";
static int judgeT = 0;
static int laneFlash[4] = { 0, 0, 0, 0 };
static float fretScroll = 0.0f;
static int animFrame = 0, missFlash = 0;

typedef struct { float x, y, vx, vy; unsigned int col; int life; } Particle;
static Particle parts[32];

static void spawn_sparks(float x, float y, unsigned int col, int count) {
    for (int k = 0; k < count; k++) {
        for (int i = 0; i < 32; i++) if (parts[i].life <= 0) {
            float ang = (float)(xr() % 360) * 0.017453f;
            float sp = 0.8f + (float)(xr() % 24) * 0.1f;
            parts[i].x = x; parts[i].y = y;
            parts[i].vx = cosf(ang) * sp; parts[i].vy = sinf(ang) * sp - 0.6f;
            parts[i].col = col;
            parts[i].life = 14 + (xr() % 12);
            break;
        }
    }
}

static void update_draw_sparks(unsigned int *fb) {
    for (int i = 0; i < 32; i++) if (parts[i].life > 0) {
        parts[i].x += parts[i].vx;
        parts[i].y += parts[i].vy;
        parts[i].vy += 0.06f;
        parts[i].life--;
        int a = (parts[i].life * 255) / 26;
        if (a > 255) a = 255;
        fill_rect_alpha(fb, (int)parts[i].x - 1, (int)parts[i].y - 1, 3, 3, parts[i].col, a);
    }
}

/* PARTE 1: Super-Mapa Continental estilo Skyrim (256x192 tiles = 49,152 casillas) */
#define MW 256
#define MH 192
static unsigned char worldMap[MH][MW]; /* 0=pasto, 1=bosque, 2=montana, 3=agua, 4=reino, 5=dungeon, 6=puente, 7=camino, 8=muralla, 9=santuario */
static int mapX = 28, mapY = 84, prevX1 = 27, prevY1 = 84, prevX2 = 26, prevY2 = 84;
static int bardFlip = 0, band1Flip = 0, band2Flip = 0;
static int bossDefeated[3] = { 0, 0, 0 };
static int shrineClaimed[2] = { 0, 0 };
static const int dngX[3] = { 88, 178, 228 };
static const int dngY[3] = { 36, 138, 28 };
static const int shrineX[2] = { 132, 206 };
static const int shrineY[2] = { 24, 168 };
static const int npcX[8] = { 30, 80, 114, 114, 114, 154, 190, 210 };
static const int npcY[8] = { 82, 60, 43,  91,  145, 112, 72,  42 };
static const char *npcMsg[8] = {
    "CRONISTA: Sube nivel en el Valle antes del Golem!",
    "HERRERO DE BRUMA: El Golem al Norte teme al ROCK!",
    "CENTINELA: Al Norte espera la Cordillera Ceniza.",
    "TROVADOR: El Automata tras el puente teme al SYNTH!",
    "JUGLAR DEL SUR: Hay un Santuario en el Delta SE!",
    "TABERNERO: Descansa aqui. (+HP/MP) Sigue al SE!",
    "EXPLORADOR: Los Espectros golpean muy duro!",
    "SABIO DEL ABISMO: Solo lo SINFONICO vence al Dragon!"
};
static char dlgText[64] = "";
static int dlgTimer = 0;

static void carve_road(int x0, int y0, int x1, int y1) {
    int cx = x0, cy = y0;
    while (cx != x1) {
        if (worldMap[cy][cx] != 6 && worldMap[cy][cx] != 5) worldMap[cy][cx] = 7;
        if (cy + 1 < MH - 1 && worldMap[cy + 1][cx] != 6 && worldMap[cy + 1][cx] != 5) worldMap[cy + 1][cx] = 7;
        cx += (x1 > cx) ? 1 : -1;
    }
    while (cy != y1) {
        if (worldMap[cy][cx] != 6 && worldMap[cy][cx] != 5) worldMap[cy][cx] = 7;
        if (cx + 1 < MW - 1 && worldMap[cy][cx + 1] != 6 && worldMap[cy][cx + 1] != 5) worldMap[cy][cx + 1] = 7;
        cy += (y1 > cy) ? 1 : -1;
    }
}

static void build_walled_town(int x0, int y0, int x1, int y1) {
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (x == x0 || x == x1 || y == y0 || y == y1) worldMap[y][x] = 8;
            else worldMap[y][x] = 4;
        }
    }
    int mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
    worldMap[my][x1] = 7; worldMap[my][x0] = 7;
    worldMap[y0][mx] = 7; worldMap[y1][mx] = 7;
}

static void init_world(void) {
    for (int y = 0; y < MH; y++) {
        for (int x = 0; x < MW; x++) {
            if (x <= 1 || y <= 1 || x >= MW - 2 || y >= MH - 2) {
                worldMap[y][x] = 2; /* Cordillera fronteriza */
            } else {
                /* Rio Plateado serpenteando de Norte a Sur alrededor de x=118..122 */
                int riverX = 119 + (int)(sinf((float)y * 0.09f) * 5.0f);
                if (x >= riverX && x <= riverX + 2) {
                    worldMap[y][x] = (y == 44 || y == 45 || y == 92 || y == 93 || y == 146 || y == 147) ? 6 : 3;
                } else {
                    int h = (x * 37 + y * 53 + (x * y) * 7) % 101;
                    /* Cordillera Ceniza en el Noreste (x>165, y<85) tiene muchas mas montanas */
                    if (x > 165 && y < 85) {
                        if (h < 22) worldMap[y][x] = 2;
                        else if (h < 38) worldMap[y][x] = 1;
                        else worldMap[y][x] = 0;
                    } else {
                        if (h < 18) worldMap[y][x] = 1;
                        else if (h > 93) worldMap[y][x] = 2;
                        else worldMap[y][x] = 0;
                    }
                }
            }
        }
    }
    /* Lagos escenicos */
    for (int dy = -6; dy <= 6; dy++) for (int dx = -8; dx <= 8; dx++) {
        if (dx * dx + dy * dy <= 42) worldMap[142 + dy][66 + dx] = 3;
        if (dx * dx + dy * dy <= 36) worldMap[120 + dy][214 + dx] = 3;
    }

    /* 3 Asentamientos Amurallados: Solaria, Aldea de Bruma, Taberna del Trovador */
    build_walled_town(18, 74, 38, 94);   /* Ciudadela de Solaria */
    build_walled_town(76, 56, 86, 64);   /* Aldea de Bruma */
    build_walled_town(150, 108, 160, 116); /* Taberna del Trovador */

    /* Red de carreteras reales conectando Solaria -> Aldeas -> 3 Puentes -> 3 Dungeons */
    carve_road(38, 84, 81, 64);   /* Solaria -> Aldea de Bruma */
    carve_road(81, 56, 88, 36);   /* Bruma -> Dungeon 1 (Forja de Hierro) */
    carve_road(38, 84, 115, 92);  /* Solaria -> Puente Central */
    carve_road(115, 44, 115, 92); /* Camino ribera Oeste hacia Puente Norte */
    carve_road(115, 92, 115, 146);/* Camino ribera Oeste hacia Puente Sur */
    carve_road(88, 36, 115, 44);  /* Forja -> Puente Norte */

    /* Asegurar que los 3 puentes crucen limpiamente el Rio Plateado */
    int bridgeYs[3] = { 44, 92, 146 };
    for (int b = 0; b < 3; b++) {
        for (int by = bridgeYs[b]; by <= bridgeYs[b] + 1; by++) {
            for (int bx = 112; bx <= 128; bx++) {
                if (worldMap[by][bx] == 3 || worldMap[by][bx] == 6) worldMap[by][bx] = 6;
                else worldMap[by][bx] = 7;
            }
        }
    }

    carve_road(128, 92, 150, 112);  /* Puente Central -> Taberna */
    carve_road(160, 112, 178, 138); /* Taberna -> Dungeon 2 (Observatorio Cristal) */
    carve_road(128, 44, 190, 72);   /* Puente Norte -> Cordillera Ceniza */
    carve_road(190, 72, 228, 28);   /* Cordillera Ceniza -> Dungeon 3 (Ciudadela del Abismo) */
    carve_road(128, 146, 178, 138); /* Puente Sur -> Observatorio */

    for (int i = 0; i < 3; i++) {
        worldMap[dngY[i]][dngX[i]] = 5;
        bossDefeated[i] = 0;
    }
    for (int i = 0; i < 2; i++) {
        worldMap[shrineY[i]][shrineX[i]] = 9;
        shrineClaimed[i] = 0;
    }
    for (int i = 0; i < 8; i++) {
        if (worldMap[npcY[i]][npcX[i]] == 2 || worldMap[npcY[i]][npcX[i]] == 3 || worldMap[npcY[i]][npcX[i]] == 8) {
            worldMap[npcY[i]][npcX[i]] = 0;
        }
    }
    mapX = 28; mapY = 84; prevX1 = 27; prevY1 = 84; prevX2 = 26; prevY2 = 84;
    bardFlip = 0; band1Flip = 0; band2Flip = 0;
    dlgTimer = 0;
}

static void gain_xp(int amount) {
    pXp += amount;
    int leveled = 0;
    while (pXp >= pNextXp) {
        pXp -= pNextXp;
        pLevel++;
        pNextXp = (int)((float)pNextXp * 1.45f);
        pMaxHp += 20;
        pMaxMp += 8;
        pBaseAtk += 2;
        pHp = pMaxHp;
        pMp = pMaxMp;
        leveled = 1;
    }
    if (leveled) {
        snprintf(dlgText, sizeof(dlgText), "SUBISTE A NIVEL %d! (+20HP +8MP +2ATQ)", pLevel);
        dlgTimer = 210;
        play_sfx(880.0f, 1800);
    }
}

static void load_best(void) { FILE *f = fopen("cb_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("cb_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static void start_mob_battle(void) {
    isMobBattle = 1;
    /* Region-based difficulty tier: West=Slime(0), East-South=Goblin(1), Ash-NorthEast=Wraith(2) */
    if (mapX < 118) chapter = 0;
    else if (mapY >= 85) chapter = 1;
    else chapter = 2;
    static const int mobHpTbl[3] = { 55, 115, 190 };
    bHp = bMaxHp = mobHpTbl[chapter];
    combo = 0; tick = 0; judgeT = 0; phaseTimer = 0; bossHitFlash = 0;
    bardX = 240.0f; bardY = 195.0f;
    memset(notes, 0, sizeof(notes));
    memset(projAlive, 0, sizeof(projAlive));
    inOverworld = 0; inStory = 0; inFFMenu = 1; ffCursor = 0; inDefense = 0; inAttackResolve = 0; pendingDmg = 0;
}

static void start_chapter(int ch) {
    isMobBattle = 0;
    chapter = ch;
    static const int bossHpTbl[3] = { 240, 420, 680 };
    bHp = bMaxHp = bossHpTbl[ch];
    combo = 0; tick = 0; judgeT = 0; phaseTimer = 0; bossHitFlash = 0;
    bardX = 240.0f; bardY = 195.0f;
    memset(notes, 0, sizeof(notes));
    memset(projAlive, 0, sizeof(projAlive));
    inOverworld = 0; inStory = 1; inFFMenu = 0; ffCursor = 0; inDefense = 0; inAttackResolve = 0; pendingDmg = 0;
}

static void start_game(void) {
    pLevel = 1; pXp = 0; pNextXp = 60; pBaseAtk = 5;
    pHp = pMaxHp = 100; pMp = pMaxMp = 50; score = 0;
    relicDmg = 0; relicVamp = 0; relicShield = 0;
    init_world();
    inTitle = 0; inOverworld = 1; inStory = 0; inFFMenu = 0; inDefense = 0; inAttackResolve = 0; inRelic = 0; gameOver = 0; victory = 0; pendingDmg = 0;
    snprintf(dlgText, sizeof(dlgText), "Explora Aethel! Sube nivel antes de retar a los Bosses.");
    dlgTimer = 180;
    play_sfx(660.0f, 2000);
}

int main(void) {
    int th = sceKernelCreateThread("cb", cb_th, 0x11, 0xFA0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    unsigned int vram_base = ((unsigned int)sceGeEdramGetAddr()) | 0x40000000u;
    vram_bufs[0] = (unsigned int *)vram_base;
    vram_bufs[1] = (unsigned int *)(vram_base + BUF_W * SCR_H * 4);
    memset((void *)vram_base, 0, BUF_W * SCR_H * 4 * 2);

    sceGuInit();
    sceGuStart(GU_DIRECT, gulist);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)(BUF_W * SCR_H * 4), BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_FLAT);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    load_best();
    ach = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1024, PSP_AUDIO_FORMAT_STEREO);
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("cb_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

    SceCtrlData pad; unsigned int prev = 0;
    const unsigned int laneBtns[4] = { PSP_CTRL_SQUARE, PSP_CTRL_TRIANGLE, PSP_CTRL_CIRCLE, PSP_CTRL_CROSS };
    int moveCd = 0;

    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;
        if (pressed & PSP_CTRL_SELECT) sfx_on = !sfx_on;
        if (bossHitFlash > 0) bossHitFlash--;
        animFrame++;

        /* Select Song dynamically: 0=Overworld/Title, 1=Forest Mobs, 2=Boss1 Rock, 3=Boss2 Synth, 4=Boss3 Symphonic */
        if (inTitle || inOverworld) curSong = 0;
        else if (isMobBattle) curSong = 1;
        else curSong = 2 + chapter;

        /* ================= LOGIC ================= */
        if (inTitle || gameOver || victory) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        } else if (inOverworld) {
            if (dlgTimer > 0) dlgTimer--;
            if (moveCd > 0) moveCd--;

            int dx = 0, dy = 0;
            float ax = ((float)pad.Lx - 128.0f) / 128.0f, ay = ((float)pad.Ly - 128.0f) / 128.0f;
            if ((pad.Buttons & PSP_CTRL_LEFT)  || ax < -0.45f) dx = -1;
            else if ((pad.Buttons & PSP_CTRL_RIGHT) || ax >  0.45f) dx =  1;
            else if ((pad.Buttons & PSP_CTRL_UP)    || ay < -0.45f) dy = -1;
            else if ((pad.Buttons & PSP_CTRL_DOWN)  || ay >  0.45f) dy =  1;

            if ((dx != 0 || dy != 0) && moveCd <= 0) {
                moveCd = 9;
                int nx = mapX + dx, ny = mapY + dy;
                if (nx >= 0 && nx < MW && ny >= 0 && ny < MH && worldMap[ny][nx] != 2 && worldMap[ny][nx] != 3 && worldMap[ny][nx] != 8) {
                    band2Flip = (prevX2 > prevX1) ? 1 : (prevX2 < prevX1 ? 0 : band1Flip);
                    band1Flip = (prevX1 > mapX)   ? 1 : (prevX1 < mapX   ? 0 : bardFlip);
                    if (dx < 0) bardFlip = 1;
                    else if (dx > 0) bardFlip = 0;
                    prevX2 = prevX1; prevY2 = prevY1;
                    prevX1 = mapX;   prevY1 = mapY;
                    mapX = nx;       mapY = ny;
                    for (int i = 0; i < 3; i++) {
                        if (!bossDefeated[i] && mapX == dngX[i] && mapY == dngY[i]) {
                            start_chapter(i);
                            break;
                        }
                    }
                    for (int s = 0; s < 2; s++) {
                        if (!shrineClaimed[s] && mapX == shrineX[s] && mapY == shrineY[s]) {
                            shrineClaimed[s] = 1;
                            pMaxHp += 25; pHp = pMaxHp; pBaseAtk += 3;
                            snprintf(dlgText, sizeof(dlgText), "SANTUARIO ANTIGUO! +25 MaxHP y +3 ATQ!");
                            dlgTimer = 210;
                            play_sfx(920.0f, 1800);
                        }
                    }
                    if (inOverworld && worldMap[mapY][mapX] == 1 && (xr() % 7) == 0) {
                        start_mob_battle();
                    }
                }
            }
            if (pressed & PSP_CTRL_CROSS) {
                for (int i = 0; i < 8; i++) {
                    if (abs(mapX - npcX[i]) + abs(mapY - npcY[i]) <= 1) {
                        pHp = pMaxHp; pMp = pMaxMp;
                        snprintf(dlgText, sizeof(dlgText), "%s", npcMsg[i]);
                        dlgTimer = 220;
                        play_sfx(780.0f, 900);
                    }
                }
            }
        } else if (inStory) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) {
                inStory = 0; inFFMenu = 1; ffCursor = 0;
                play_sfx(520.0f, 1200);
            }
        } else if (inRelic) {
            int chosen = 0;
            if (pressed & PSP_CTRL_SQUARE)   { relicDmg += 4; chosen = 1; }
            else if (pressed & PSP_CTRL_TRIANGLE) { relicVamp += 3; chosen = 1; }
            else if (pressed & PSP_CTRL_CIRCLE)   { relicShield = 1; pHp = pMaxHp; chosen = 1; }
            if (chosen) {
                play_sfx(880.0f, 1500);
                inRelic = 0;
                bossDefeated[chapter] = 1;
                static const int bossXpTbl[3] = { 150, 320, 600 };
                gain_xp(bossXpTbl[chapter]);
                if (bossDefeated[0] && bossDefeated[1] && bossDefeated[2]) {
                    victory = 1; save_best();
                } else {
                    inOverworld = 1;
                    if (dlgTimer <= 0) {
                        snprintf(dlgText, sizeof(dlgText), "Reliquia obtenida! (+%d XP) Busca el siguiente Dungeon.", bossXpTbl[chapter]);
                        dlgTimer = 180;
                    }
                }
            }
        } else if (inFFMenu) {
            if (pressed & PSP_CTRL_UP)   { ffCursor = (ffCursor + 3) % 4; play_sfx(440.0f, 400); }
            if (pressed & PSP_CTRL_DOWN) { ffCursor = (ffCursor + 1) % 4; play_sfx(440.0f, 400); }
            if (pressed & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) {
                curStyle = (curStyle + 1) % 3; play_sfx(520.0f, 600);
            }
            if (pressed & PSP_CTRL_CROSS) {
                if (ffCursor == 0) {
                    inFFMenu = 0; inAttackResolve = 0; phaseTimer = 0; tick = 0; pendingDmg = 0; combo = 0;
                    memset(notes, 0, sizeof(notes));
                    play_sfx(660.0f, 800);
                } else if (ffCursor == 1) {
                    curStyle = (curStyle + 1) % 3;
                    play_sfx(520.0f, 600);
                } else if (ffCursor == 2) {
                    if (pMp >= 15) {
                        pMp -= 15; pHp = (pHp + 45 > pMaxHp) ? pMaxHp : pHp + 45;
                        inFFMenu = 0; inDefense = 1; phaseTimer = 0;
                        memset(projAlive, 0, sizeof(projAlive));
                        play_sfx(880.0f, 1200);
                    } else {
                        play_sfx(180.0f, 600);
                    }
                } else if (ffCursor == 3) {
                    if (isMobBattle) {
                        inFFMenu = 0; inOverworld = 1;
                        snprintf(dlgText, sizeof(dlgText), "Escapaste del combate!"); dlgTimer = 100;
                        play_sfx(300.0f, 800);
                    } else {
                        play_sfx(180.0f, 600);
                    }
                }
            }
        } else if (inDefense) {
            float ax = ((float)pad.Lx - 128.0f) / 128.0f, ay = ((float)pad.Ly - 128.0f) / 128.0f;
            if (fabsf(ax) < 0.18f) ax = 0; if (fabsf(ay) < 0.18f) ay = 0;
            if (pad.Buttons & PSP_CTRL_LEFT)  ax = -1; if (pad.Buttons & PSP_CTRL_RIGHT) ax = 1;
            if (pad.Buttons & PSP_CTRL_UP)    ay = -1; if (pad.Buttons & PSP_CTRL_DOWN)  ay = 1;
            bardX += ax * 3.2f; bardY += ay * 2.8f;
            if (bardX < 140) bardX = 140; if (bardX > 340) bardX = 340;
            if (bardY < 130) bardY = 130; if (bardY > 245) bardY = 245;

            if (judgeT > 0) judgeT--;
            phaseTimer++;
            int pRate = isMobBattle ? 52 : (42 - chapter * 7);
            if ((phaseTimer % pRate) == 0) {
                for (int p = 0; p < 6; p++) if (!projAlive[p]) {
                    projAlive[p] = 1;
                    projX[p] = (float)(145 + (xr() % 190));
                    projY[p] = 132.0f;
                    projVx[p] = (bardX - projX[p]) * 0.011f;
                    projVy[p] = isMobBattle ? 1.35f : (1.65f + chapter * 0.30f);
                    break;
                }
            }
            for (int p = 0; p < 6; p++) if (projAlive[p]) {
                projX[p] += projVx[p]; projY[p] += projVy[p];
                if (projY[p] > 256) { projAlive[p] = 0; continue; }
                if (fabsf(projX[p] - bardX) < 11 && fabsf(projY[p] - bardY) < 11) {
                    projAlive[p] = 0;
                    static const int mobEdmg[3]  = { 16, 24, 32 };
                    static const int bossEdmg[3] = { 28, 36, 46 };
                    int baseEdmg = isMobBattle ? mobEdmg[chapter] : bossEdmg[chapter];
                    int edmg = relicShield ? ((baseEdmg * 6) / 10) : baseEdmg;
                    pHp -= edmg; play_sfx(140.0f, 1200);
                    snprintf(judge, sizeof(judge), "-%d HP!", edmg);
                    judgeT = 26;
                    if (pHp <= 0) { pHp = 0; gameOver = 1; save_best(); }
                }
            }
            if (phaseTimer >= 240) {
                inDefense = 0; inFFMenu = 1; phaseTimer = 0;
                pMp = (pMp + 5 > pMaxMp) ? pMaxMp : pMp + 5;
                memset(projAlive, 0, sizeof(projAlive));
            }
        } else if (inAttackResolve) {
            /* Resolve accumulated Guitar Hero combo damage with sonic beam + impact + floating number! */
            if (judgeT > 0) judgeT--;
            phaseTimer++;
            if (phaseTimer == 18) {
                if (pendingDmg > 0) {
                    bHp -= pendingDmg;
                    if (bHp < 0) bHp = 0;
                    bossHitFlash = 16;
                    spawn_sparks(390.0f, 118.0f, RGB(255, 225, 80), 18);
                    play_sfx(980.0f, 1800);
                } else {
                    play_sfx(220.0f, 900);
                }
            }
            if (phaseTimer >= 92) {
                inAttackResolve = 0;
                phaseTimer = 0;
                pendingDmg = 0;
                if (bHp <= 0) {
                    bHp = 0;
                    if (isMobBattle) {
                        static const int mobXpTbl[3] = { 25, 55, 110 };
                        int xpGain = mobXpTbl[chapter];
                        score += 150 + chapter * 100;
                        inOverworld = 1;
                        snprintf(dlgText, sizeof(dlgText), "Enemigo vencido! +%d XP", xpGain);
                        dlgTimer = 140;
                        gain_xp(xpGain);
                    } else {
                        inRelic = 1;
                    }
                } else {
                    inDefense = 1;
                    bardX = 240.0f; bardY = 195.0f;
                    memset(projAlive, 0, sizeof(projAlive));
                }
            }
        } else {
            /* Guitar Hero Rhythm Attack Phase (Progressive & Readable) */
            if (pressed & PSP_CTRL_LTRIGGER) { curStyle = (curStyle + 2) % 3; play_sfx(440.0f, 600); }
            if (pressed & PSP_CTRL_RTRIGGER) { curStyle = (curStyle + 1) % 3; play_sfx(440.0f, 600); }
            if (judgeT > 0) judgeT--;
            if (missFlash > 0) missFlash--;
            for (int l = 0; l < 4; l++) if (laneFlash[l] > 0) laneFlash[l]--;

            static const float mobNoteSpd[3]  = { 0.95f, 1.20f, 1.45f };
            static const float bossNoteSpd[3] = { 1.25f, 1.52f, 1.80f };
            float spd = isMobBattle ? mobNoteSpd[chapter] : bossNoteSpd[chapter];
            fretScroll += spd;
            if (fretScroll >= 28.0f) fretScroll -= 28.0f;

            if (judgeT > 0) judgeT--;
            phaseTimer++;
            if (phaseTimer >= 420) {
                inAttackResolve = 1;
                phaseTimer = 0;
                memset(notes, 0, sizeof(notes));
            }

            static int lastBeatStep = -1;
            int stepMod = isMobBattle ? 4 : (3 - (chapter > 0 ? 1 : 0)); /* Mobs every 4 beats, Boss 1 every 3, Boss 2-3 every 2 */
            if (bgm_step != lastBeatStep && (bgm_step % stepMod) == 0 && phaseTimer < 330) {
                lastBeatStep = bgm_step;
                for (int i = 0; i < 24; i++) if (!notes[i].active) {
                    notes[i].active = 1; notes[i].lane = (bgm_step + (xr() & 1)) & 3; notes[i].y = 104.0f; break;
                }
            }

            for (int l = 0; l < 4; l++) if (pressed & laneBtns[l]) {
                laneFlash[l] = 8;
                int hit = -1; float bestD = 32.0f;
                for (int i = 0; i < 24; i++) if (notes[i].active && notes[i].lane == l) {
                    float d = fabsf(notes[i].y - HIT_Y);
                    if (d < bestD) { bestD = d; hit = i; }
                }
                if (hit >= 0) {
                    notes[hit].active = 0; combo++;
                    laneFlash[l] = 14;
                    int isPerf = (bestD < 14.0f);
                    int base = (isPerf ? 9 : 6) + pBaseAtk + relicDmg + (combo / 4);
                    if (curStyle == bossWeak[chapter]) base *= 2;
                    pendingDmg += base;
                    score += base * 10;
                    if (isPerf && relicVamp > 0) { pHp += relicVamp; if (pHp > pMaxHp) pHp = pMaxHp; }
                    snprintf(judge, sizeof(judge), isPerf ? "PERFECT! +%d" : "GOOD +%d", base);
                    judgeT = 30;
                    unsigned int scols[4] = { RGB(255,90,190), RGB(90,255,140), RGB(255,80,80), RGB(80,225,255) };
                    spawn_sparks((float)(164 + l * 50), (float)HIT_Y, isPerf ? RGB(255, 240, 90) : scols[l], isPerf ? 10 : 6);
                    play_sfx(songScale[curSong][l], 2600);
                } else {
                    /* Penalty for pressing wrong button / mashing */
                    combo = 0;
                    pendingDmg -= 6; if (pendingDmg < 0) pendingDmg = 0;
                    int wdmg = relicShield ? 3 : 5;
                    pHp -= wdmg;
                    missFlash = 10;
                    snprintf(judge, sizeof(judge), "NOTA FALSA! -%dHP", wdmg);
                    judgeT = 26;
                    spawn_sparks((float)(164 + l * 50), (float)HIT_Y, RGB(255, 60, 70), 6);
                    play_sfx(110.0f, 900);
                    if (pHp <= 0) { pHp = 0; gameOver = 1; save_best(); }
                }
            }

            for (int i = 0; i < 24; i++) if (notes[i].active) {
                notes[i].y += spd;
                if (notes[i].y > SCR_H - 10) {
                    int ml = notes[i].lane;
                    notes[i].active = 0; combo = 0;
                    pendingDmg -= 10; if (pendingDmg < 0) pendingDmg = 0;
                    static const int mobMissTbl[3]  = { 7, 10, 14 };
                    static const int bossMissTbl[3] = { 12, 15, 18 };
                    int baseMiss = isMobBattle ? mobMissTbl[chapter] : bossMissTbl[chapter];
                    int edmg = relicShield ? ((baseMiss * 6) / 10) : baseMiss;
                    pHp -= edmg;
                    missFlash = 14;
                    snprintf(judge, sizeof(judge), "FALLO! -%dHP / -10", edmg);
                    judgeT = 30;
                    spawn_sparks((float)(164 + ml * 50), (float)HIT_Y + 12.0f, RGB(255, 50, 70), 8);
                    play_sfx(130.0f, 1200);
                    if (pHp <= 0) { pHp = 0; gameOver = 1; save_best(); }
                }
            }
        }

        /* ================= RENDER ================= */
        unsigned int *fb = vram_bufs[cur_fb];
        char buf[64];

        if (inOverworld) {
            /* Render 20x12 textured 24x24 tiles with animated river water & sandy shorelines */
            int ox = mapX - 10, oy = mapY - 5;
            int waterOff = (animFrame / 8) % 24;
            for (int ty = 0; ty < 12; ty++) {
                int sy = ty * 24 - 8;
                for (int tx = 0; tx < 20; tx++) {
                    int sx = tx * 24;
                    int wx = ox + tx, wy = oy + ty;
                    if (wx < 0 || wy < 0 || wx >= MW || wy >= MH) {
                        draw_tile24(fb, tile_grass, sx, sy);
                        draw_spr24(fb, spr_mountain, sx, sy);
                    } else {
                        unsigned char t = worldMap[wy][wx];
                        if (t == 3) {
                            draw_tile24_scroll(fb, tile_water, sx, sy, waterOff);
                            if (wx > 0 && worldMap[wy][wx - 1] != 3 && worldMap[wy][wx - 1] != 6)
                                fill_rect_alpha(fb, sx, sy, 3, 24, RGB(205, 185, 130), 175);
                            if (wx < MW - 1 && worldMap[wy][wx + 1] != 3 && worldMap[wy][wx + 1] != 6)
                                fill_rect_alpha(fb, sx + 21, sy, 3, 24, RGB(205, 185, 130), 175);
                        } else if (t == 4) {
                            draw_tile24(fb, tile_castle, sx, sy);
                        } else if (t == 6) {
                            draw_tile24(fb, tile_bridge, sx, sy);
                        } else if (t == 7) {
                            draw_tile24(fb, tile_path, sx, sy);
                        } else if (t == 8) {
                            draw_tile24(fb, tile_wall, sx, sy);
                        } else {
                            draw_tile24(fb, tile_grass, sx, sy);
                            if (t == 2) draw_spr24(fb, spr_mountain, sx, sy);
                            else if (t == 5) {
                                int dIdx = -1;
                                for (int k = 0; k < 3; k++) if (dngX[k] == wx && dngY[k] == wy) dIdx = k;
                                if (dIdx >= 0 && bossDefeated[dIdx]) draw_spr24(fb, spr_chest, sx, sy);
                                else {
                                    int pulse = 55 + (int)(sinf((float)animFrame * 0.10f) * 35.0f);
                                    fill_ellipse_alpha(fb, sx + 12, sy + 13, 13, 11, RGB(170, 60, 255), pulse);
                                    draw_spr24(fb, spr_dungeon, sx, sy);
                                }
                            } else if (t == 9) {
                                int sIdx = (wx == shrineX[1] && wy == shrineY[1]) ? 1 : 0;
                                if (!shrineClaimed[sIdx]) {
                                    int pulse = 60 + (int)(sinf((float)animFrame * 0.12f) * 35.0f);
                                    fill_ellipse_alpha(fb, sx + 12, sy + 12, 12, 10, RGB(70, 225, 255), pulse);
                                }
                                draw_spr24(fb, spr_chest, sx, sy);
                            }
                        }
                    }
                }
            }
            /* 2.5D Overhanging Trees (spr_tree32) with soft elliptical drop shadows */
            for (int ty = 0; ty < 12; ty++) {
                int sy = ty * 24 - 8;
                for (int tx = 0; tx < 20; tx++) {
                    int sx = tx * 24;
                    int wx = ox + tx, wy = oy + ty;
                    if (wx >= 0 && wy >= 0 && wx < MW && wy < MH && worldMap[wy][wx] == 1) {
                        fill_ellipse_alpha(fb, sx + 12, sy + 20, 11, 5, RGB(0, 0, 0), 115);
                        draw_spr32(fb, spr_tree32, sx - 4, sy - 7);
                    }
                }
            }
            /* 8 NPCs across the 6 regions dynamically turning to look towards your Bard */
            for (int i = 0; i < 8; i++) {
                int sx = (npcX[i] - ox) * 24, sy = (npcY[i] - oy) * 24 - 8;
                if (sx >= -24 && sx < SCR_W && sy >= -24 && sy < SCR_H) {
                    int npcFlip = (mapX < npcX[i]) ? 1 : 0;
                    int bob = ((animFrame / 20 + i) & 1);
                    fill_ellipse_alpha(fb, sx + 12, sy + 21, 8, 3, RGB(0, 0, 0), 120);
                    draw_spr24_dir(fb, (i & 1) ? spr_troubadour : spr_elder, sx, sy - bob, npcFlip);
                    draw_text(fb, sx + 9, sy - 8 - bob, "!", RGB(255, 235, 60));
                }
            }
            /* Your 3-Piece Band following in formation with soft shadows */
            int bBob = ((animFrame / 16) & 1);
            fill_ellipse_alpha(fb, (prevX2 - ox) * 24 + 12, (prevY2 - oy) * 24 + 13, 8, 3, RGB(0, 0, 0), 120);
            fill_ellipse_alpha(fb, (prevX1 - ox) * 24 + 12, (prevY1 - oy) * 24 + 13, 8, 3, RGB(0, 0, 0), 120);
            fill_ellipse_alpha(fb, 10 * 24 + 12, 5 * 24 + 13, 9, 4, RGB(0, 0, 0), 135);
            draw_spr24_dir(fb, spr_band2, (prevX2 - ox) * 24, (prevY2 - oy) * 24 - 8 - bBob, band2Flip);
            draw_spr24_dir(fb, spr_band1, (prevX1 - ox) * 24, (prevY1 - oy) * 24 - 8 - (bBob ^ 1), band1Flip);
            draw_spr24_dir(fb, spr_bard,  10 * 24, 5 * 24 - 8 - bBob, bardFlip);

            /* Top Adventure HUD Window with Level, XP, HP & MP bars + Region Name */
            draw_ff_window(fb, 4, 3, 472, 22);
            sprintf(buf, "NV.%d", pLevel);
            draw_text(fb, 10, 10, buf, RGB(255, 225, 90));
            draw_text(fb, 42, 10, "HP", RGB(255, 130, 130));
            fill_rect(fb, 58, 10, 50, 7, RGB(40, 20, 25));
            fill_rect(fb, 58, 10, (pHp * 50) / pMaxHp, 7, RGB(70, 235, 100));
            draw_border(fb, 58, 10, 50, 7, RGB(220, 235, 255));
            draw_text(fb, 114, 10, "MP", RGB(130, 220, 255));
            fill_rect(fb, 130, 10, 36, 7, RGB(20, 25, 50));
            fill_rect(fb, 130, 10, (pMp * 36) / pMaxMp, 7, RGB(50, 200, 255));
            draw_border(fb, 130, 10, 36, 7, RGB(220, 235, 255));
            draw_text(fb, 172, 10, "XP", RGB(255, 215, 90));
            fill_rect(fb, 188, 10, 36, 7, RGB(35, 28, 14));
            fill_rect(fb, 188, 10, (pXp * 36) / (pNextXp > 0 ? pNextXp : 1), 7, RGB(255, 205, 65));
            draw_border(fb, 188, 10, 36, 7, RGB(220, 235, 255));

            const char *reg = "VALLE ESMERALDA";
            if (mapX >= 18 && mapX <= 38 && mapY >= 74 && mapY <= 94) reg = "SOLARIA";
            else if (mapX >= 165 && mapY < 85) reg = "CORDILLERA CENIZA";
            else if (mapX >= 118) reg = "BOSQUE ARCANO";
            sprintf(buf, "%s | BOSS:%d/3 | %d,%d", reg, bossDefeated[0]+bossDefeated[1]+bossDefeated[2], mapX, mapY);
            draw_text(fb, 232, 10, buf, RGB(255, 255, 255));

            /* Top-Right Continental Mini-Map (64x48 = 4x4 downsampled Skyrim-scale 256x192 world) */
            draw_ff_window(fb, 404, 28, 72, 56);
            for (int my = 0; my < 48; my++) {
                for (int mx = 0; mx < 64; mx++) {
                    unsigned char mt = worldMap[my * 4 + 1][mx * 4 + 1];
                    unsigned int mc = RGB(48, 128, 56);
                    if (mt == 1) mc = RGB(24, 82, 34);
                    else if (mt == 2) mc = RGB(115, 115, 125);
                    else if (mt == 3) mc = RGB(36, 98, 205);
                    else if (mt == 4 || mt == 8) mc = RGB(215, 185, 85);
                    else if (mt == 6) mc = RGB(180, 115, 55);
                    else if (mt == 7) mc = RGB(165, 130, 85);
                    put_px(fb, 408 + mx, 32 + my, mc);
                }
            }
            for (int i = 0; i < 3; i++) {
                fill_rect(fb, 408 + dngX[i] / 4 - 1, 32 + dngY[i] / 4 - 1, 3, 3, bossDefeated[i] ? RGB(90, 240, 120) : RGB(215, 65, 255));
            }
            for (int s = 0; s < 2; s++) if (!shrineClaimed[s]) {
                fill_rect(fb, 408 + shrineX[s] / 4, 32 + shrineY[s] / 4, 2, 2, RGB(80, 235, 255));
            }
            if ((animFrame / 15) & 1) fill_rect(fb, 408 + mapX / 4 - 1, 32 + mapY / 4 - 1, 3, 3, RGB(255, 255, 255));

            if (dlgTimer > 0) {
                draw_ff_window(fb, 10, 228, 460, 38);
                fill_rect(fb, 16, 235, 24, 24, RGB(10, 14, 42));
                draw_spr24(fb, spr_troubadour, 16, 235);
                draw_border(fb, 16, 235, 24, 24, RGB(255, 215, 80));
                draw_text(fb, 48, 236, "CRONISTA DEL REINO:", RGB(255, 225, 90));
                draw_text(fb, 48, 250, dlgText, RGB(255, 255, 255));
            }
        } else if (inTitle) {
            /* Exclusive Illustrated 32-Bit Title Screen Cover Art + Translucent FF Logo Box */
            for (int y = 0; y < SCR_H; y++) memcpy(&fb[y * BUF_W], &bg_title[y * SCR_W], SCR_W * 4);
            if ((animFrame % 10) == 0) spawn_sparks((float)(60 + (xr() % 360)), 255.0f, (xr() & 1) ? RGB(80, 225, 255) : RGB(255, 120, 235), 1);
            update_draw_sparks(fb);

            draw_ff_window(fb, 64, 154, 352, 106);
            draw_text(fb, 115, 166, "S Y M P H O N Y   O F   T H E   A B Y S S", RGB(255, 225, 80));
            draw_text(fb, 138, 182, "32-BIT GUITAR HERO + RPG ROGUELIKE", RGB(100, 235, 255));
            sprintf(buf, "RECORD MAXIMO: %d PTS", best);
            draw_text(fb, 174, 200, buf, RGB(140, 255, 160));
            if (((animFrame / 22) & 1) == 0) {
                draw_text(fb, 148, 224, "PULSA     PARA EMPEZAR AVENTURA", RGB(255, 255, 255));
                draw_spr16(fb, spr_btn_cr16, 187, 220);
            }
            draw_text(fb, 92, 242, "Explora el Reino, Toca Melodias y Vence al Mal", RGB(190, 205, 235));
        } else {
            /* Copy 32-bit Gothic Cathedral Arena Backdrop + Flickering Torch Glows */
            for (int y = 0; y < SCR_H; y++) memcpy(&fb[y * BUF_W], &bg_arena[y * SCR_W], SCR_W * 4);
            const int torchX[4] = { 118, 188, 292, 362 };
            int tRad = 18 + (int)(sinf((float)animFrame * 0.16f) * 3.0f);
            for (int t = 0; t < 4; t++) {
                fill_ellipse_alpha(fb, torchX[t], 56, tRad, tRad, RGB(255, 165, 50), 48);
                fill_ellipse_alpha(fb, torchX[t], 56, 8, 8, RGB(255, 230, 120), 85);
            }

            if (inStory) {
                draw_ff_window(fb, 64, 56, 352, 160);
                const unsigned int *bspr = (chapter == 0) ? boss_golem : (chapter == 1 ? boss_automaton : boss_dragon);
                draw_boss64_dir(fb, bspr, 82, 86, 0, 1);
                sprintf(buf, "DUNGEON %d: %s", chapter + 1, bossNames[chapter]);
                draw_text(fb, 158, 78, buf, RGB(255, 225, 80));
                sprintf(buf, "DEBILIDAD: %s (Dano x2)", styleNames[bossWeak[chapter]]);
                draw_text(fb, 158, 104, buf, RGB(100, 255, 160));
                draw_text(fb, 158, 130, "Combate por turnos clasico FF.", RGB(220, 230, 255));
                draw_text(fb, 158, 146, "Toca las notas para atacar!", RGB(220, 230, 255));
                draw_text(fb, 172, 182, "PULSA     PARA LUCHAR", RGB(255, 235, 90));
                draw_spr16(fb, spr_btn_cr16, 211, 178);
            } else if (inRelic) {
                /* 32-Bit Roguelike Relic Selection Cards with Real 20x20 PSP Button Sprites! */
                draw_spr48(fb, spr_band2_48, 18, 76);
                draw_spr48(fb, spr_bard_48,  98, 80);
                draw_spr48(fb, spr_band1_48, 58, 88);

                draw_ff_window(fb, 68, 52, 344, 198);
                fill_rect(fb, 74, 58, 332, 18, RGB(68, 48, 16));
                draw_spr24(fb, spr_chest, 78, 54);
                draw_text(fb, 108, 63, "4. BOTIN ROGUELIKE (ELIGE 1 RELIQUIA):", RGB(255, 225, 80));

                /* Card 1: Amplificador Runico (Square Button Sprite) */
                fill_rect(fb, 78, 84, 324, 46, RGB(42, 18, 48));
                draw_border(fb, 78, 84, 324, 46, RGB(255, 130, 210));
                fill_rect(fb, 84, 91, 32, 32, RGB(14, 12, 26));
                draw_border(fb, 84, 91, 32, 32, RGB(255, 130, 210));
                fill_rect(fb, 92, 99, 16, 16, RGB(235, 60, 170));
                fill_rect(fb, 96, 103, 8, 8, RGB(255, 240, 90));
                draw_text(fb, 126, 93, "AMPLIFICADOR RUNICO", RGB(255, 255, 255));
                draw_text(fb, 126, 109, "+4 DANO POR CADA NOTA ACERTADA", RGB(255, 155, 220));
                draw_spr20(fb, spr_btn_sq20, 368, 97);

                /* Card 2: Pua Vampirica Carmesi (Triangle Button Sprite) */
                fill_rect(fb, 78, 138, 324, 46, RGB(16, 44, 30));
                draw_border(fb, 78, 138, 324, 46, RGB(95, 245, 145));
                fill_rect(fb, 84, 145, 32, 32, RGB(14, 12, 26));
                draw_border(fb, 84, 145, 32, 32, RGB(95, 245, 145));
                fill_rect(fb, 93, 152, 14, 18, RGB(220, 40, 70));
                fill_rect(fb, 97, 156, 6, 6, RGB(255, 230, 80));
                draw_text(fb, 126, 147, "PUA VAMPIRICA CARMESI", RGB(255, 255, 255));
                draw_text(fb, 126, 163, "+3 HP CURADOS POR NOTA PERFECT", RGB(120, 255, 165));
                draw_spr20(fb, spr_btn_tr20, 368, 151);

                /* Card 3: Escudo Ritmico Astral (Circle Button Sprite) */
                fill_rect(fb, 78, 192, 324, 46, RGB(16, 32, 58));
                draw_border(fb, 78, 192, 324, 46, RGB(100, 215, 255));
                fill_rect(fb, 84, 199, 32, 32, RGB(14, 12, 26));
                draw_border(fb, 84, 199, 32, 32, RGB(100, 215, 255));
                fill_rect(fb, 92, 207, 16, 16, RGB(45, 155, 255));
                fill_rect(fb, 98, 209, 4, 12, RGB(255, 255, 255));
                fill_rect(fb, 94, 213, 12, 4, RGB(255, 255, 255));
                draw_text(fb, 126, 201, "ESCUDO RITMICO ASTRAL", RGB(255, 255, 255));
                draw_text(fb, 126, 217, "-40% DANO RECIBIDO + CURA 100%", RGB(130, 225, 255));
                draw_spr20(fb, spr_btn_ci20, 368, 205);
            } else if (gameOver || victory) {
                draw_ff_window(fb, 80, 66, 320, 140);
                draw_text(fb, 168, 90, victory ? "LEYENDA DEL ROCK!" : "CAISTE EN EL ABISMO", victory ? RGB(255,230,80) : RGB(255,80,90));
                draw_text(fb, 118, 112, victory ? "Has liberado los 3 Reinos del Mal!" : "Muerte permanente: la historia reinicia.", RGB(210, 225, 255));
                sprintf(buf, "PUNTOS: %d   RECORD: %d", score, best);
                draw_text(fb, 148, 140, buf, RGB(140, 255, 160));
                draw_text(fb, 148, 172, "Pulsa     para Nueva Partida", RGB(255, 255, 255));
                draw_spr16(fb, spr_btn_cr16, 187, 168);
            } else {
                const unsigned int * const btns20_arr[4] = { spr_btn_sq20, spr_btn_tr20, spr_btn_ci20, spr_btn_cr20 };
                /* Battle Scene: Top Status Windows + 32-bit High-Res Band (48x48) & Boss (64x64) */
                draw_ff_window(fb, 8, 6, 206, 42);
                fill_rect(fb, 14, 12, 26, 28, RGB(14, 18, 48));
                draw_spr24(fb, spr_bard, 15, 14);
                draw_border(fb, 14, 12, 26, 28, RGB(240, 200, 80));
                sprintf(buf, "BANDA DEL BARDO  NV.%d", pLevel);
                draw_text(fb, 46, 11, buf, RGB(255, 225, 85));
                draw_text(fb, 46, 23, "HP", RGB(120, 255, 140));
                fill_rect(fb, 62, 23, 80, 7, RGB(20, 35, 25));
                fill_rect(fb, 62, 23, (pHp * 80) / pMaxHp, 7, RGB(50, 225, 100));
                draw_border(fb, 62, 23, 80, 7, RGB(230, 240, 255));
                sprintf(buf, "%d/%d", pHp, pMaxHp);
                draw_text(fb, 146, 23, buf, RGB(255, 255, 255));

                draw_text(fb, 46, 34, "MP", RGB(100, 225, 255));
                fill_rect(fb, 62, 34, 80, 6, RGB(16, 28, 48));
                fill_rect(fb, 62, 34, (pMp * 80) / pMaxMp, 6, RGB(45, 195, 255));
                draw_border(fb, 62, 34, 80, 6, RGB(200, 235, 255));
                sprintf(buf, "%d/%d", pMp, pMaxMp);
                draw_text(fb, 146, 34, buf, RGB(140, 235, 255));

                /* Top-Right Boss/Enemy Status Window */
                draw_ff_window(fb, 266, 6, 206, 42);
                draw_text(fb, 276, 11, isMobBattle ? mobNames[chapter] : bossNames[chapter], RGB(255, 255, 255));
                draw_text(fb, 276, 23, "HP", RGB(255, 130, 140));
                fill_rect(fb, 294, 23, 104, 7, RGB(45, 14, 20));
                fill_rect(fb, 294, 23, (bHp * 104) / bMaxHp, 7, RGB(240, 50, 70));
                draw_border(fb, 294, 23, 104, 7, RGB(255, 230, 235));
                sprintf(buf, "%d/%d", bHp, bMaxHp);
                draw_text(fb, 402, 23, buf, RGB(255, 255, 255));
                sprintf(buf, "DEBILIDAD: %s (x2!)", styleNames[bossWeak[chapter]]);
                draw_text(fb, 276, 35, buf, RGB(255, 215, 80));

                /* Soft Alpha Shadows + Glowing Runic Pads + Idle Breathing Animation on Band & Boss! */
                int bIdle0 = (int)(sinf((float)animFrame * 0.08f) * 2.0f);
                int bIdle1 = (int)(sinf((float)animFrame * 0.08f + 1.5f) * 2.0f);
                int bIdle2 = (int)(sinf((float)animFrame * 0.08f + 3.0f) * 2.0f);

                fill_ellipse_alpha(fb, 42,  144, 21, 6, RGB(0, 0, 0), 130);
                fill_ellipse_alpha(fb, 122, 148, 21, 6, RGB(0, 0, 0), 130);
                fill_ellipse_alpha(fb, 82,  158, 21, 6, RGB(0, 0, 0), 130);
                fill_ellipse_alpha(fb, 42,  144, 22, 6, RGB(175, 105, 255), 60);
                fill_ellipse_alpha(fb, 122, 148, 22, 6, RGB(65, 225, 255), 65);
                fill_ellipse_alpha(fb, 82,  158, 22, 6, RGB(80, 235, 145), 60);
                draw_ellipse_ring(fb, 42,  144, 22, 6, RGB(175, 105, 255));
                draw_ellipse_ring(fb, 122, 148, 22, 6, RGB(65, 225, 255));
                draw_ellipse_ring(fb, 82,  158, 22, 6, RGB(80, 235, 145));

                draw_spr48(fb, spr_band2_48, 18, 100 + bIdle2);
                draw_spr48(fb, spr_bard_48,  98, 104 + bIdle0);
                draw_spr48(fb, spr_band1_48, 58, 114 + bIdle1);

                /* Boss Summoning Circle & Floating/Breathing 64x64 Boss on Right */
                int bossBob = (int)(sinf((float)animFrame * 0.06f) * 3.0f);
                fill_ellipse_alpha(fb, 390, 148, 40, 10, RGB(0, 0, 0), 150);
                fill_ellipse_alpha(fb, 390, 148, 42, 11, RGB(255, 55, 80), 65);
                draw_ellipse_ring(fb, 390, 148, 42, 11, RGB(255, 65, 85));
                draw_ellipse_ring(fb, 390, 148, 32, 8,  RGB(255, 185, 90));
                const unsigned int *espr;
                if (isMobBattle) espr = (chapter == 0) ? mob_slime : (chapter == 1 ? mob_goblin : mob_wraith);
                else             espr = (chapter == 0) ? boss_golem : (chapter == 1 ? boss_automaton : boss_dragon);
                int shakeX = (inAttackResolve && phaseTimer >= 18 && phaseTimer < 38) ? ((phaseTimer & 2) ? 6 : -6) : 0;
                draw_boss64(fb, espr, 358 + shakeX, 86 + bossBob, bossHitFlash > 0);

                if (inFFMenu) {
                    /* Classic Final Fantasy Turn-Based Command Menu + Partitura Preview with REAL 32-Bit PSP Button Sprites! */
                    draw_ff_window(fb, 8, 170, 224, 96);
                    fill_rect(fb, 12, 174, 216, 12, RGB(38, 52, 128));
                    draw_text(fb, 16, 176, "COMANDOS DE BATALLA (TURNO)", RGB(255, 220, 80));

                    fill_rect(fb, 14, 189 + ffCursor * 18, 212, 15, RGB(65, 85, 185));
                    draw_border(fb, 14, 189 + ffCursor * 18, 212, 15, RGB(255, 215, 85));

                    const char *opts[4] = {
                        "ATACAR (Tocar Melodia)",
                        "CAMBIAR ESTILO MUSICAL",
                        "HIMNO CURATIVO (+45 HP)",
                        "HUIR DEL COMBATE"
                    };
                    for (int i = 0; i < 4; i++) {
                        int ry = 193 + i * 18;
                        draw_text(fb, 20, ry, opts[i], ffCursor == i ? RGB(255, 240, 90) : RGB(255, 255, 255));
                    }
                    draw_spr16(fb, spr_btn_cr16, 204, 188);
                    draw_spr_lr24(fb, 198, 207);
                    draw_text(fb, 196, 229, "15MP", RGB(130, 235, 215));
                    draw_spr16(fb, spr_btn_ci16, 204, 242);

                    /* Right Tactical & Partitura Window with Real 20x20 PSP Button Sprites */
                    draw_ff_window(fb, 238, 170, 234, 96);
                    fill_rect(fb, 242, 174, 226, 12, RGB(38, 52, 128));
                    draw_text(fb, 248, 176, "ARMONIA Y PARTITURA ENEMIGA", RGB(100, 235, 255));

                    fill_rect(fb, 246, 190, 218, 16, curStyle == bossWeak[chapter] ? RGB(18, 52, 36) : RGB(44, 34, 18));
                    draw_border(fb, 246, 190, 218, 16, curStyle == bossWeak[chapter] ? RGB(80, 235, 135) : RGB(240, 190, 75));
                    sprintf(buf, curStyle == bossWeak[chapter] ? "ESTILO: %s -> CRITICO x2!" : "ESTILO: %s", styleNames[curStyle]);
                    draw_text(fb, 252, 194, buf, curStyle == bossWeak[chapter] ? RGB(120, 255, 150) : RGB(255, 225, 95));
                    if (curStyle != bossWeak[chapter]) draw_spr_lr24(fb, 434, 191);

                    draw_text(fb, 246, 211, isMobBattle ? "MELODIA: SENCILLA (LENTA):" : "MELODIA BOSS (RAPIDA):", RGB(180, 225, 255));
                    for (int n = 0; n < 6; n++) {
                        int nl = (chapter + n * 3) & 3;
                        int nx = 248 + n * 36;
                        fill_rect(fb, nx, 221, 26, 22, RGB(18, 22, 54));
                        draw_border(fb, nx, 221, 26, 22, RGB(130, 150, 220));
                        draw_spr20(fb, btns20_arr[nl], nx + 3, 222);
                    }
                    sprintf(buf, "RELIQUIAS: +%dDMG  +%dVAMP  %s", relicDmg, relicVamp, relicShield ? "ESCUDO" : "-");
                    draw_text(fb, 246, 248, buf, RGB(255, 215, 85));
                } else if (inDefense) {
                    /* Enemy Turn: 32-Bit Runic Dodge Arena with Survival Bar & Fiery Shadow Orbs */
                    draw_ff_window(fb, 132, 110, 216, 156);
                    fill_rect(fb, 136, 114, 208, 13, RGB(88, 22, 42));
                    draw_text(fb, 140, 117, "3. ESQUIVA ATAQUE!", RGB(255, 235, 80));
                    fill_rect(fb, 266, 117, 72, 7, RGB(25, 10, 15));
                    fill_rect(fb, 266, 117, ((240 - phaseTimer) * 72) / 240, 7, RGB(255, 185, 50));
                    draw_border(fb, 266, 117, 72, 7, RGB(255, 255, 255));

                    /* Inner Runic Grid Arena (flashes crimson when hit!) */
                    fill_rect(fb, 138, 130, 204, 130, (judgeT > 18) ? RGB(72, 14, 24) : RGB(14, 11, 28));
                    for (int gx = 158; gx < 340; gx += 22) fill_rect(fb, gx, 131, 1, 128, RGB(34, 28, 64));
                    for (int gy = 148; gy < 258; gy += 18) fill_rect(fb, 139, gy, 202, 1, RGB(34, 28, 64));
                    draw_border(fb, 138, 130, 204, 130, (judgeT > 18) ? RGB(255, 80, 90) : RGB(140, 110, 225));

                    /* Glowing Cyan Aegis Shield Ring around Bard */
                    draw_ellipse_ring(fb, (int)bardX, (int)bardY, 15, 14, (judgeT > 16) ? RGB(255, 80, 90) : RGB(70, 235, 255));
                    draw_spr24(fb, spr_bard, (int)bardX - 12, (int)bardY - 12);
                    if (judgeT > 0) draw_text(fb, (int)bardX - 18, (int)bardY - 24 - (26 - judgeT) / 2, judge, RGB(255, 90, 90));

                    /* 32-Bit Layered Fiery Shadow-Flame Orbs with Comet Trails */
                    for (int p = 0; p < 6; p++) if (projAlive[p]) {
                        int ox = (int)projX[p], oy = (int)projY[p];
                        fill_rect(fb, ox - (int)(projVx[p]*3) - 3, oy - (int)(projVy[p]*3) - 3, 6, 6, RGB(160, 35, 190));
                        fill_rect(fb, ox - (int)(projVx[p]*1.5f) - 4, oy - (int)(projVy[p]*1.5f) - 4, 8, 8, RGB(230, 50, 95));
                        fill_rect(fb, ox - 6, oy - 6, 12, 12, RGB(255, 55, 75));
                        fill_rect(fb, ox - 4, oy - 4, 8, 8, RGB(255, 140, 50));
                        fill_rect(fb, ox - 2, oy - 2, 4, 4, RGB(255, 245, 170));
                    }
                } else if (inAttackResolve) {
                    /* Musical Combo Discharge: Sonic Beam -> Impact Flash -> Big Floating Damage Number! */
                    if (phaseTimer < 18) {
                        int bx = 142 + (phaseTimer * 240) / 18;
                        int by = 124 - (int)(sinf((float)phaseTimer * 0.17f) * 14.0f);
                        fill_ellipse_alpha(fb, bx, by, 22, 12, RGB(60, 190, 255), 110);
                        fill_rect(fb, bx - 16, by - 4, 24, 8, RGB(50, 180, 255));
                        fill_rect(fb, bx - 8,  by - 7, 16, 14, RGB(140, 240, 255));
                        fill_rect(fb, bx - 4,  by - 4, 8,  8, RGB(255, 255, 200));
                    } else {
                        int dt = phaseTimer - 18;
                        if (dt < 22 && pendingDmg > 0) {
                            /* Expanding Sonic Shockwave Rings & Sparks around Boss */
                            fill_ellipse_alpha(fb, 390, 118, 24 + dt, 24 + dt, RGB(255, 220, 80), 85 - dt * 3);
                            draw_ellipse_ring(fb, 390, 118, 16 + dt * 2, 16 + dt * 2, RGB(255, 225, 80));
                            draw_ellipse_ring(fb, 390, 118, 8 + dt * 2,  8 + dt * 2,  RGB(80, 235, 255));
                        }
                        /* Big 2x Floating Damage Number popping up above Enemy! */
                        int bounce = (dt < 14) ? (int)(sinf((float)dt * 0.22f) * 14.0f) : 14;
                        int fy = 66 - bounce - (dt > 24 ? (dt - 24) / 3 : 0);
                        if (pendingDmg > 0) {
                            sprintf(buf, "-%d!", pendingDmg);
                            draw_text_2x(fb, 366, fy, buf, (curStyle == bossWeak[chapter]) ? RGB(255, 235, 70) : RGB(255, 110, 110));
                        } else {
                            draw_text_2x(fb, 354, fy, "FALLO!", RGB(180, 190, 210));
                        }
                    }
                    update_draw_sparks(fb);
                    draw_ff_window(fb, 68, 196, 344, 48);
                    sprintf(buf, pendingDmg > 0 ? "DESCARGA DEL COMBO! -%d HP AL ENEMIGO!" : "NO ACERTASTE NOTAS EN ESTE TURNO...", pendingDmg);
                    draw_text(fb, 92, 216, buf, pendingDmg > 0 ? RGB(255, 230, 90) : RGB(210, 220, 240));
                } else {
                    /* Guitar Hero 4-Lane Rhythm Highway (Enhanced Fretboard, Scrolling Beat Bars, & Hit Flash!) */
                    draw_ff_window(fb, 130, 74, 220, 192);
                    fill_rect(fb, 134, 78, 212, 14, RGB(28, 42, 108));
                    sprintf(buf, "%s | COMBO x%d", styleNames[curStyle], combo);
                    draw_text(fb, 138, 81, buf, curStyle == bossWeak[chapter] ? RGB(100, 255, 140) : RGB(255, 235, 100));
                    draw_spr_lr24(fb, 318, 78);

                    /* Turn Timer Bar at top of fretboard */
                    fill_rect(fb, 136, 94, 208, 4, RGB(18, 22, 50));
                    fill_rect(fb, 136, 94, ((420 - phaseTimer) * 208) / 420, 4, RGB(80, 220, 255));

                    /* Fretboard Dark Stage & Scrolling Beat Lines (flashes red on miss/wrong note!) */
                    fill_rect(fb, 136, 100, 208, 160, (missFlash > 0) ? RGB(68, 14, 24) : RGB(11, 13, 30));
                    for (int fy = 100 + (int)fretScroll; fy < 258; fy += 28) {
                        fill_rect(fb, 137, fy, 206, 1, RGB(34, 40, 76));
                    }

                    unsigned int lcols[4] = { RGB(255,80,180), RGB(80,255,140), RGB(255,70,70), RGB(0,220,255) };
                    unsigned int ltint[4] = { RGB(34,16,32), RGB(14,34,24), RGB(34,16,18), RGB(14,26,40) };

                    /* Hit Zone Glowing Bar across all 4 lanes */
                    fill_rect(fb, 137, HIT_Y - 2, 206, 5, RGB(36, 44, 88));

                    for (int l = 0; l < 4; l++) {
                        int lx = 144 + l * 50;
                        fill_rect(fb, lx + 4, 100, 32, 158, ltint[l]);
                        fill_rect(fb, lx + 19, 100, 2, 158, RGB(65, 78, 135));
                        /* Receptor Box (lights up when pressed/hit!) */
                        fill_rect(fb, lx + 3, HIT_Y - 12, 34, 26, laneFlash[l] > 0 ? lcols[l] : RGB(16, 20, 48));
                        draw_border(fb, lx + 3, HIT_Y - 12, 34, 26, laneFlash[l] > 0 ? RGB(255, 255, 255) : lcols[l]);
                        draw_spr20(fb, btns20_arr[l], lx + 10, HIT_Y - 9);
                    }
                    for (int i = 0; i < 24; i++) if (notes[i].active) {
                        int l = notes[i].lane;
                        int lx = 144 + l * 50;
                        int ny = (int)notes[i].y;
                        fill_rect(fb, lx + 5, ny - 12, 30, 24, lcols[l]);
                        fill_rect(fb, lx + 7, ny - 10, 26, 20, RGB(20, 24, 58));
                        draw_border(fb, lx + 5, ny - 12, 30, 24, RGB(255, 255, 255));
                        draw_spr20(fb, btns20_arr[l], lx + 10, ny - 10);
                    }
                    sprintf(buf, "DANO ACUMULADO: %d", pendingDmg);
                    draw_text(fb, 174, 54, buf, RGB(120, 245, 255));
                    if (judgeT > 0) draw_text(fb, 182, 64, judge, (missFlash > 0 || judge[0] == 'F' || judge[0] == 'N') ? RGB(255, 85, 95) : RGB(255, 255, 120));
                    update_draw_sparks(fb);
                }
            }
        }

        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
        cur_fb ^= 1;
    }

    save_best();
    arun = 0;
    if (ath >= 0) sceKernelWaitThreadEnd(ath, NULL);
    if (ach >= 0) sceAudioChRelease(ach);
    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
