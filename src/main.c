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

static inline unsigned int blend_rgb_4(unsigned int c00, unsigned int c10, unsigned int c01, unsigned int c11, int fx, int fy) {
    int ifx = 256 - fx, ify = 256 - fy;
    int w00 = (ifx * ify) >> 8, w10 = (fx * ify) >> 8, w01 = (ifx * fy) >> 8, w11 = (fx * fy) >> 8;
    unsigned int r = (((c00 & 0xFF) * w00 + (c10 & 0xFF) * w10 + (c01 & 0xFF) * w01 + (c11 & 0xFF) * w11) >> 8);
    unsigned int g = ((((c00 >> 8) & 0xFF) * w00 + ((c10 >> 8) & 0xFF) * w10 + ((c01 >> 8) & 0xFF) * w01 + ((c11 >> 8) & 0xFF) * w11) >> 8);
    unsigned int b = ((((c00 >> 16) & 0xFF) * w00 + ((c10 >> 16) & 0xFF) * w10 + ((c01 >> 16) & 0xFF) * w01 + ((c11 >> 16) & 0xFF) * w11) >> 8);
    return (b << 16) | (g << 8) | r;
}

static const unsigned int* get_cached_tile30(const unsigned int *tile) {
    static const unsigned int *ptrs[8] = {0};
    static unsigned int data[8][30 * 30];
    static int count = 0;
    for (int k = 0; k < count; k++) if (ptrs[k] == tile) return data[k];
    int slot = (count < 8) ? count++ : 7;
    ptrs[slot] = tile;
    for (int j = 0; j < 30; j++) {
        int syfp = (j * 24 * 256) / 30;
        int sy0 = syfp >> 8, sy1 = (sy0 + 1) % 24, fy = syfp & 255;
        const unsigned int *r0 = &tile[sy0 * 24], *r1 = &tile[sy1 * 24];
        for (int i = 0; i < 30; i++) {
            int sxfp = (i * 24 * 256) / 30;
            int sx0 = sxfp >> 8, sx1 = (sx0 + 1) % 24, fx = sxfp & 255;
            data[slot][j * 30 + i] = blend_rgb_4(r0[sx0], r0[sx1], r1[sx0], r1[sx1], fx, fy);
        }
    }
    return data[slot];
}

static void draw_tile24_30(unsigned int *fb, const unsigned int *tile, int dx, int dy) {
    const unsigned int *c = get_cached_tile30(tile);
    for (int j = 0; j < 30; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &c[j * 30];
        for (int i = 0; i < 30; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) dst[px] = src[i];
        }
    }
}

static void draw_tile24_30_scroll(unsigned int *fb, const unsigned int *tile, int dx, int dy, int sxOff) {
    const unsigned int *c = get_cached_tile30(tile);
    int off = (sxOff * 30) / 24;
    for (int j = 0; j < 30; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &c[j * 30];
        for (int i = 0; i < 30; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) dst[px] = src[(i + off) % 30];
        }
    }
}

static void draw_spr32_scale2x(unsigned int *fb, const unsigned int *spr, int dx, int dy) {
    static const unsigned int *ptrs[4] = {0};
    static unsigned int data[4][64 * 64];
    static int count = 0;
    const unsigned int *cached = NULL;
    for (int k = 0; k < count; k++) if (ptrs[k] == spr) { cached = data[k]; break; }
    if (!cached) {
        int slot = (count < 4) ? count++ : 3;
        ptrs[slot] = spr;
        for (int y = 0; y < 32; y++) {
            int ym = (y > 0) ? y - 1 : 0, yp = (y < 31) ? y + 1 : 31;
            for (int x = 0; x < 32; x++) {
                unsigned int P = spr[y * 32 + x];
                int xm = (x > 0) ? x - 1 : 0, xp = (x < 31) ? x + 1 : 31;
                unsigned int A = spr[ym * 32 + x], B = spr[y * 32 + xp];
                unsigned int C = spr[y * 32 + xm], D = spr[yp * 32 + x];
                unsigned int e0 = P, e1 = P, e2 = P, e3 = P;
                if (C == A && C != D && A != B) e0 = A;
                if (A == B && A != C && B != D) e1 = B;
                if (D == C && D != B && C != A) e2 = C;
                if (B == D && B != A && D != C) e3 = D;
                int idx = (y * 2) * 64 + (x * 2);
                data[slot][idx] = e0; data[slot][idx + 1] = e1;
                data[slot][idx + 64] = e2; data[slot][idx + 65] = e3;
            }
        }
        cached = data[slot];
    }
    for (int j = 0; j < 64; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &cached[j * 64];
        for (int i = 0; i < 64; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[i];
                if (c) dst[px] = c;
            }
        }
    }
}

static void draw_spr32_96_scale2x(unsigned int *fb, const unsigned int *spr, int dx, int dy) {
    static const unsigned int *ptrs[4] = {0};
    static unsigned int data[4][96 * 96];
    static int count = 0;
    const unsigned int *cached = NULL;
    for (int k = 0; k < count; k++) if (ptrs[k] == spr) { cached = data[k]; break; }
    if (!cached) {
        int slot = (count < 4) ? count++ : 3;
        ptrs[slot] = spr;
        static unsigned int tmp64[64 * 64];
        for (int y = 0; y < 32; y++) {
            int ym = (y > 0) ? y - 1 : 0, yp = (y < 31) ? y + 1 : 31;
            for (int x = 0; x < 32; x++) {
                unsigned int P = spr[y * 32 + x];
                int xm = (x > 0) ? x - 1 : 0, xp = (x < 31) ? x + 1 : 31;
                unsigned int A = spr[ym * 32 + x], B = spr[y * 32 + xp];
                unsigned int C = spr[y * 32 + xm], D = spr[yp * 32 + x];
                unsigned int e0 = P, e1 = P, e2 = P, e3 = P;
                if (C == A && C != D && A != B) e0 = A;
                if (A == B && A != C && B != D) e1 = B;
                if (D == C && D != B && C != A) e2 = C;
                if (B == D && B != A && D != C) e3 = D;
                int idx = (y * 2) * 64 + (x * 2);
                tmp64[idx] = e0; tmp64[idx + 1] = e1;
                tmp64[idx + 64] = e2; tmp64[idx + 65] = e3;
            }
        }
        for (int j = 0; j < 96; j++) {
            int sj = (j * 64) / 96;
            for (int i = 0; i < 96; i++) data[slot][j * 96 + i] = tmp64[sj * 64 + (i * 64) / 96];
        }
        cached = data[slot];
    }
    for (int j = 0; j < 96; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &cached[j * 96];
        for (int i = 0; i < 96; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[i];
                if (c) dst[px] = c;
            }
        }
    }
}

static void draw_spr32_40_scale2x(unsigned int *fb, const unsigned int *spr, int dx, int dy) {
    static const unsigned int *cPtr = NULL;
    static unsigned int data[40 * 40];
    if (cPtr != spr) {
        cPtr = spr;
        static unsigned int tmp64[64 * 64];
        for (int y = 0; y < 32; y++) {
            int ym = (y > 0) ? y - 1 : 0, yp = (y < 31) ? y + 1 : 31;
            for (int x = 0; x < 32; x++) {
                unsigned int P = spr[y * 32 + x];
                int xm = (x > 0) ? x - 1 : 0, xp = (x < 31) ? x + 1 : 31;
                unsigned int A = spr[ym * 32 + x], B = spr[y * 32 + xp];
                unsigned int C = spr[y * 32 + xm], D = spr[yp * 32 + x];
                unsigned int e0 = P, e1 = P, e2 = P, e3 = P;
                if (C == A && C != D && A != B) e0 = A;
                if (A == B && A != C && B != D) e1 = B;
                if (D == C && D != B && C != A) e2 = C;
                if (B == D && B != A && D != C) e3 = D;
                int idx = (y * 2) * 64 + (x * 2);
                tmp64[idx] = e0; tmp64[idx + 1] = e1;
                tmp64[idx + 64] = e2; tmp64[idx + 65] = e3;
            }
        }
        for (int j = 0; j < 40; j++) {
            int sj = (j * 64) / 40;
            for (int i = 0; i < 40; i++) data[j * 40 + i] = tmp64[sj * 64 + (i * 64) / 40];
        }
    }
    for (int j = 0; j < 40; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &data[j * 40];
        for (int i = 0; i < 40; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[i];
                if (c) dst[px] = c;
            }
        }
    }
}

static void draw_spr24_30_dir(unsigned int *fb, const unsigned int *spr, int dx, int dy, int flipX) {
    static const unsigned int *ptrs[20] = {0};
    static unsigned int data[20][30 * 30];
    static int count = 0;
    const unsigned int *cached = NULL;
    for (int k = 0; k < count; k++) if (ptrs[k] == spr) { cached = data[k]; break; }
    if (!cached) {
        int slot = (count < 20) ? count++ : 19;
        ptrs[slot] = spr;
        static unsigned int tmp48[48 * 48];
        for (int y = 0; y < 24; y++) {
            int ym = (y > 0) ? y - 1 : 0, yp = (y < 23) ? y + 1 : 23;
            for (int x = 0; x < 24; x++) {
                int xm = (x > 0) ? x - 1 : 0, xp = (x < 23) ? x + 1 : 23;
                unsigned int P = spr[y * 24 + x];
                unsigned int A = spr[ym * 24 + x], B = spr[y * 24 + xp];
                unsigned int C = spr[y * 24 + xm], D = spr[yp * 24 + x];
                unsigned int e0 = P, e1 = P, e2 = P, e3 = P;
                if (C == A && C != D && A != B) e0 = A;
                if (A == B && A != C && B != D) e1 = B;
                if (D == C && D != B && C != A) e2 = C;
                if (B == D && B != A && D != C) e3 = D;
                int idx = (y * 2) * 48 + (x * 2);
                tmp48[idx] = e0; tmp48[idx + 1] = e1;
                tmp48[idx + 48] = e2; tmp48[idx + 49] = e3;
            }
        }
        for (int j = 0; j < 30; j++) {
            int sj = (j * 48) / 30;
            for (int i = 0; i < 30; i++) data[slot][j * 30 + i] = tmp48[sj * 48 + (i * 48) / 30];
        }
        cached = data[slot];
    }
    for (int j = 0; j < 30; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &cached[j * 30];
        for (int i = 0; i < 30; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[flipX ? (29 - i) : i];
                if (c) dst[px] = c;
            }
        }
    }
}

static void draw_spr24_30(unsigned int *fb, const unsigned int *spr, int dx, int dy) {
    draw_spr24_30_dir(fb, spr, dx, dy, 0);
}

static void draw_spr64_to_30(unsigned int *fb, const unsigned int *spr, int dx, int dy, int flipX) {
    static const unsigned int *ptrs[6] = {0};
    static unsigned int data[6][30 * 30];
    static int count = 0;
    const unsigned int *cached = NULL;
    for (int k = 0; k < count; k++) if (ptrs[k] == spr) { cached = data[k]; break; }
    if (!cached) {
        int slot = (count < 6) ? count++ : 5;
        ptrs[slot] = spr;
        for (int j = 0; j < 30; j++) {
            int sj0 = (j * 64) / 30, sj1 = (sj0 < 63) ? sj0 + 1 : 63;
            for (int i = 0; i < 30; i++) {
                int si0 = (i * 64) / 30, si1 = (si0 < 63) ? si0 + 1 : 63;
                unsigned int c0 = spr[sj0 * 64 + si0], c1 = spr[sj0 * 64 + si1];
                unsigned int c2 = spr[sj1 * 64 + si0], c3 = spr[sj1 * 64 + si1];
                int cnt = (c0 != 0) + (c1 != 0) + (c2 != 0) + (c3 != 0);
                if (cnt >= 2) {
                    unsigned int r = 0, g = 0, b = 0;
                    unsigned int cs[4] = { c0, c1, c2, c3 };
                    for (int k = 0; k < 4; k++) if (cs[k]) {
                        r += cs[k] & 0xFF; g += (cs[k] >> 8) & 0xFF; b += (cs[k] >> 16) & 0xFF;
                    }
                    data[slot][j * 30 + i] = ((b / cnt) << 16) | ((g / cnt) << 8) | (r / cnt);
                } else {
                    data[slot][j * 30 + i] = 0;
                }
            }
        }
        cached = data[slot];
    }
    for (int j = 0; j < 30; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *src = &cached[j * 30];
        for (int i = 0; i < 30; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                unsigned int c = src[flipX ? (29 - i) : i];
                if (c) dst[px] = c;
            }
        }
    }
}

static void draw_spr64_as24(unsigned int *fb, const unsigned int *spr, int dx, int dy, int flipX) {
    for (int j = 0; j < 24; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        unsigned int *dst = &fb[py * BUF_W];
        int sj = (j * 64) / 24;
        const unsigned int *src = &spr[sj * 64];
        for (int i = 0; i < 24; i++) {
            int px = dx + i;
            if ((unsigned)px < SCR_W) {
                int si = ((flipX ? (23 - i) : i) * 64) / 24;
                unsigned int c = src[si];
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

/* Epic 44.1kHz Multi-Track Stereo PSP Synthesizer: 6 Elaborate 64-Step Songs + Stereo Ping-Pong Delay */
static int ach = -1, ath = -1;
static volatile int arun = 0, sfx_t = 0, sfx_max = 3000, sfx_on = 1, curStyle = 0, curSong = 0;
static volatile int bgm_step = 0;
static volatile float bgm_sub = 0.0f;
static float sfx_f = 440.0f, sfx_p = 0.0f, sfx_p2 = 0.0f;
static short abuf[2][1024 * 2];
static float echoL[4096], echoR[4096];
static int echoIdx = 0;

static void play_sfx(float f, int dur) {
    if (sfx_on) { sfx_f = f; sfx_t = dur; sfx_max = dur > 0 ? dur : 1; sfx_p = 0.0f; sfx_p2 = 0.0f; }
}

/* Harmonic 4-lane scale frequencies per song */
static const float songScale[7][4] = {
    { 293.66f, 349.23f, 440.00f, 587.33f }, /* 0: Obertura Solaria (D-min Aventura) */
    { 329.63f, 392.00f, 493.88f, 659.25f }, /* 1: Bosque de los Ecos (E-min) */
    { 164.81f, 220.00f, 246.94f, 329.63f }, /* 2: Coloso de Hierro (E-min Heavy Rock) */
    { 261.63f, 311.13f, 392.00f, 523.25f }, /* 3: Ciudadela Arcana (C-min Synth) */
    { 220.00f, 261.63f, 329.63f, 440.00f }, /* 4: Abismo Volcanico (A-min Shred) */
    { 293.66f, 349.23f, 440.00f, 587.33f }, /* 5: Dragon Sombrio (D-min Symphonic) */
    { 349.23f, 440.00f, 523.25f, 659.25f }  /* 6: Jazz de Ciudad (Fmaj7 Swing) */
};

/* 64-Step (4-Bar) Basslines & Lead Melodies for 7 Distinct Songs */
static const float songBass[7][64] = {
    { /* 0: Obertura Solaria (D-min Folk-Rock) */
      146.83f,146.83f,174.61f,146.83f, 130.81f,130.81f,164.81f,130.81f, 116.54f,116.54f,146.83f,116.54f, 130.81f,146.83f,164.81f,110.00f,
      146.83f,220.00f,174.61f,146.83f, 130.81f,196.00f,164.81f,130.81f, 116.54f,174.61f,146.83f,116.54f, 110.00f,130.81f,146.83f,164.81f,
       98.00f, 98.00f,146.83f, 98.00f, 116.54f,116.54f,174.61f,116.54f, 130.81f,130.81f,196.00f,130.81f, 110.00f,138.59f,164.81f,110.00f,
      146.83f,146.83f,220.00f,174.61f, 130.81f,130.81f,196.00f,164.81f, 116.54f,146.83f,174.61f,146.83f, 110.00f,130.81f,146.83f,110.00f
    },
    { /* 1: Bosque de los Ecos (E-min Celtic Battle) */
      164.81f,164.81f,196.00f,164.81f, 146.83f,146.83f,174.61f,146.83f, 130.81f,130.81f,164.81f,130.81f, 146.83f,164.81f,196.00f,123.47f,
      164.81f,246.94f,196.00f,164.81f, 146.83f,220.00f,174.61f,146.83f, 130.81f,196.00f,164.81f,130.81f, 123.47f,146.83f,164.81f,196.00f,
      110.00f,164.81f,130.81f,164.81f, 123.47f,185.00f,146.83f,185.00f, 130.81f,196.00f,164.81f,196.00f, 146.83f,220.00f,196.00f,146.83f,
      164.81f,164.81f,246.94f,196.00f, 146.83f,146.83f,220.00f,174.61f, 130.81f,164.81f,196.00f,164.81f, 123.47f,146.83f,164.81f,123.47f
    },
    { /* 2: Coloso de Hierro (E-min Heavy Rock) */
       82.41f, 82.41f, 82.41f, 98.00f,  82.41f, 82.41f,110.00f, 98.00f,  73.42f, 73.42f, 98.00f, 73.42f,  82.41f,110.00f,123.47f, 98.00f,
       82.41f, 82.41f,123.47f, 82.41f,  98.00f, 98.00f,146.83f, 98.00f,  87.31f, 87.31f,130.81f, 87.31f,  82.41f, 98.00f,110.00f,123.47f,
       65.41f, 65.41f,130.81f, 65.41f,  73.42f, 73.42f,146.83f, 73.42f,  82.41f, 82.41f,164.81f, 82.41f,  98.00f,110.00f,123.47f, 82.41f,
       82.41f,164.81f, 82.41f, 98.00f, 110.00f,164.81f,123.47f,110.00f,  98.00f, 82.41f, 73.42f, 82.41f,  98.00f,110.00f,123.47f, 82.41f
    },
    { /* 3: Ciudadela Arcana (C-min Dark Synth) */
      130.81f,261.63f,130.81f,196.00f, 116.54f,233.08f,116.54f,174.61f, 103.83f,207.65f,103.83f,155.56f, 116.54f,146.83f,196.00f,233.08f,
      130.81f,261.63f,155.56f,196.00f, 116.54f,233.08f,146.83f,174.61f, 103.83f,207.65f,130.81f,155.56f,  98.00f,196.00f,146.83f,196.00f,
       87.31f,174.61f,130.81f,174.61f,  98.00f,196.00f,146.83f,196.00f, 103.83f,207.65f,155.56f,207.65f, 116.54f,233.08f,196.00f,146.83f,
      130.81f,261.63f,196.00f,261.63f, 116.54f,233.08f,174.61f,233.08f, 103.83f,207.65f,155.56f,207.65f,  98.00f,146.83f,196.00f,130.81f
    },
    { /* 4: Abismo Volcanico (A-min Neoclassical Shred) */
      110.00f,220.00f,164.81f,220.00f,  98.00f,196.00f,146.83f,196.00f,  87.31f,174.61f,130.81f,174.61f,  82.41f,164.81f,123.47f,164.81f,
      110.00f,220.00f,130.81f,164.81f,  98.00f,196.00f,123.47f,146.83f,  87.31f,174.61f,110.00f,130.81f,  82.41f,138.59f,164.81f,196.00f,
       73.42f,146.83f,110.00f,146.83f,  82.41f,164.81f,123.47f,164.81f,  87.31f,174.61f,130.81f,174.61f,  98.00f,196.00f,164.81f,123.47f,
      110.00f,110.00f,220.00f,164.81f,  98.00f, 98.00f,196.00f,146.83f,  87.31f, 87.31f,174.61f,130.81f,  82.41f,123.47f,164.81f,110.00f
    },
    { /* 5: Sinfonia del Dragon Sombrio (D-min Symphonic Metal) */
       73.42f,146.83f,110.00f,146.83f,  65.41f,130.81f, 98.00f,130.81f,  58.27f,116.54f, 87.31f,116.54f,  65.41f, 73.42f,110.00f, 82.41f,
       73.42f,146.83f,174.61f,146.83f,  65.41f,130.81f,164.81f,130.81f,  58.27f,116.54f,146.83f,116.54f,  55.00f,110.00f,138.59f,110.00f,
       49.00f, 98.00f,146.83f, 98.00f,  58.27f,116.54f,174.61f,116.54f,  65.41f,130.81f,196.00f,130.81f,  55.00f, 82.41f,110.00f,138.59f,
       73.42f, 73.42f,220.00f,146.83f,  65.41f, 65.41f,196.00f,130.81f,  58.27f, 87.31f,174.61f,116.54f,  55.00f, 73.42f,110.00f, 73.42f
    },
    { /* 6: Jazz de Ciudad (Walking Bass: Fmaj7 - Dm7 - Gm7 - C7) */
       87.31f,110.00f,130.81f,138.59f, 146.83f,130.81f,110.00f, 98.00f,  98.00f,116.54f,146.83f,130.81f, 130.81f,110.00f, 98.00f, 92.50f,
       87.31f,110.00f,130.81f,164.81f, 146.83f,164.81f,174.61f,138.59f,  98.00f,116.54f,146.83f,174.61f, 130.81f,146.83f,164.81f,123.47f,
      116.54f,146.83f,174.61f,164.81f, 110.00f,130.81f,164.81f,146.83f,  98.00f,116.54f,146.83f,130.81f, 130.81f,146.83f,164.81f,138.59f,
       87.31f,130.81f,110.00f, 98.00f, 146.83f,130.81f,110.00f,103.83f,  98.00f,116.54f,146.83f,138.59f, 130.81f,164.81f,130.81f, 98.00f
    }
};

static const float songLead[7][64] = {
    { /* 0: Obertura Solaria */
      293.66f,349.23f,440.00f,392.00f, 349.23f,329.63f,293.66f,261.63f, 233.08f,293.66f,349.23f,329.63f, 293.66f,329.63f,440.00f,587.33f,
      587.33f,523.25f,440.00f,392.00f, 440.00f,392.00f,349.23f,329.63f, 349.23f,392.00f,440.00f,523.25f, 440.00f,392.00f,349.23f,329.63f,
      392.00f,440.00f,493.88f,587.33f, 523.25f,440.00f,392.00f,349.23f, 329.63f,392.00f,440.00f,523.25f, 440.00f,415.30f,440.00f,554.37f,
      587.33f,440.00f,349.23f,440.00f, 523.25f,392.00f,329.63f,392.00f, 466.16f,349.23f,293.66f,349.23f, 440.00f,523.25f,587.33f,440.00f
    },
    { /* 1: Bosque de los Ecos */
      329.63f,392.00f,493.88f,392.00f, 440.00f,392.00f,329.63f,293.66f, 261.63f,329.63f,392.00f,329.63f, 349.23f,392.00f,493.88f,659.25f,
      659.25f,587.33f,493.88f,440.00f, 493.88f,440.00f,392.00f,329.63f, 392.00f,440.00f,493.88f,587.33f, 493.88f,440.00f,392.00f,349.23f,
      440.00f,523.25f,659.25f,523.25f, 493.88f,587.33f,659.25f,587.33f, 523.25f,659.25f,783.99f,659.25f, 587.33f,659.25f,739.99f,587.33f,
      659.25f,493.88f,392.00f,493.88f, 587.33f,440.00f,349.23f,440.00f, 523.25f,392.00f,329.63f,392.00f, 493.88f,587.33f,659.25f,493.88f
    },
    { /* 2: Coloso de Hierro */
      329.63f,329.63f,392.00f,440.00f, 493.88f,440.00f,392.00f,329.63f, 293.66f,329.63f,392.00f,493.88f, 587.33f,493.88f,440.00f,392.00f,
      329.63f,392.00f,493.88f,659.25f, 587.33f,493.88f,440.00f,392.00f, 349.23f,440.00f,523.25f,659.25f, 587.33f,523.25f,493.88f,440.00f,
      261.63f,329.63f,392.00f,523.25f, 293.66f,392.00f,440.00f,587.33f, 329.63f,440.00f,493.88f,659.25f, 783.99f,659.25f,587.33f,493.88f,
      659.25f,493.88f,392.00f,329.63f, 587.33f,440.00f,392.00f,293.66f, 523.25f,392.00f,329.63f,261.63f, 493.88f,587.33f,659.25f,493.88f
    },
    { /* 3: Ciudadela Arcana */
      523.25f,392.00f,311.13f,392.00f, 466.16f,349.23f,293.66f,349.23f, 415.30f,311.13f,261.63f,311.13f, 466.16f,523.25f,622.25f,783.99f,
      523.25f,622.25f,783.99f,622.25f, 466.16f,587.33f,698.46f,587.33f, 415.30f,523.25f,622.25f,523.25f, 392.00f,493.88f,587.33f,783.99f,
      698.46f,523.25f,415.30f,523.25f, 783.99f,587.33f,493.88f,587.33f, 830.61f,622.25f,523.25f,622.25f, 932.33f,783.99f,622.25f,523.25f,
      1046.5f,783.99f,622.25f,523.25f, 932.33f,698.46f,587.33f,466.16f, 830.61f,622.25f,523.25f,415.30f, 783.99f,698.46f,622.25f,523.25f
    },
    { /* 4: Abismo Volcanico */
      440.00f,523.25f,659.25f,523.25f, 392.00f,493.88f,587.33f,493.88f, 349.23f,440.00f,523.25f,440.00f, 329.63f,415.30f,493.88f,659.25f,
      440.00f,659.25f,523.25f,440.00f, 392.00f,587.33f,493.88f,392.00f, 349.23f,523.25f,440.00f,349.23f, 329.63f,493.88f,659.25f,830.61f,
      587.33f,698.46f,880.00f,698.46f, 659.25f,783.99f,987.77f,783.99f, 698.46f,880.00f,1046.5f,880.00f, 783.99f,659.25f,587.33f,493.88f,
      880.00f,659.25f,523.25f,440.00f, 783.99f,587.33f,493.88f,392.00f, 698.46f,523.25f,440.00f,349.23f, 659.25f,783.99f,880.00f,659.25f
    },
    { /* 5: Sinfonia del Dragon Sombrio */
      587.33f,523.25f,440.00f,349.23f, 523.25f,440.00f,392.00f,329.63f, 466.16f,440.00f,349.23f,293.66f, 440.00f,523.25f,587.33f,880.00f,
      880.00f,698.46f,587.33f,440.00f, 783.99f,659.25f,523.25f,392.00f, 698.46f,587.33f,466.16f,349.23f, 659.25f,554.37f,440.00f,329.63f,
      392.00f,466.16f,587.33f,783.99f, 466.16f,587.33f,698.46f,932.33f, 523.25f,659.25f,783.99f,1046.5f, 880.00f,659.25f,554.37f,440.00f,
      1174.6f,880.00f,698.46f,587.33f, 1046.5f,783.99f,659.25f,523.25f, 932.33f,698.46f,587.33f,466.16f, 880.00f,1046.5f,1174.6f,880.00f
    },
    { /* 6: Jazz de Ciudad (Lounge Sax / Rhodes Swing) */
      523.25f,440.00f,349.23f,440.00f, 587.33f,523.25f,440.00f,392.00f, 466.16f,392.00f,349.23f,392.00f, 440.00f,523.25f,587.33f,659.25f,
      698.46f,659.25f,587.33f,523.25f, 587.33f,523.25f,440.00f,392.00f, 466.16f,523.25f,587.33f,659.25f, 587.33f,523.25f,466.16f,440.00f,
      587.33f,698.46f,659.25f,587.33f, 523.25f,659.25f,587.33f,523.25f, 466.16f,587.33f,523.25f,466.16f, 440.00f,493.88f,523.25f,659.25f,
      698.46f,587.33f,523.25f,440.00f, 523.25f,440.00f,392.00f,349.23f, 466.16f,440.00f,392.00f,349.23f, 440.00f,523.25f,659.25f,523.25f
    }
};
static const float songTempo[7] = { 3.8f, 4.4f, 4.8f, 5.0f, 5.2f, 5.5f, 3.2f };

static int audio_th(SceSize a, void *p) {
    (void)a; (void)p;
    int cur = 0;
    unsigned int noiseSeed = 0x13579BDFu;
    float stepAcc = 0.0f, bp = 0.0f, lp1 = 0.0f, lp2 = 0.0f, lp3 = 0.0f, padP1 = 0.0f, padP2 = 0.0f, sp = 0.0f, dp = 0.0f, vibP = 0.0f;
    const float invSR = 1.0f / 44100.0f;

    while (arun) {
        short *o = abuf[cur];
        int sg = curSong; if (sg < 0 || sg > 6) sg = 0;
        int stStyle = curStyle; if (stStyle < 0 || stStyle > 2) stStyle = 0;
        float spd = songTempo[sg];

        for (int i = 0; i < 1024; i++) {
            float lSig = 0.0f, rSig = 0.0f;
            if (sfx_on) {
                stepAcc += spd * invSR;
                if (stepAcc >= 1.0f) {
                    stepAcc -= 1.0f;
                    bgm_step++;
                    dp = 0.0f;
                }
                bgm_sub = stepAcc;
                int st = bgm_step & 63;
                float sub = stepAcc;

                /* 1. Bass Track (Warm sub + octave punch) */
                float bf = songBass[sg][st];
                bp += bf * invSR; if (bp >= 1.0f) bp -= 1.0f;
                float bassEnv = expf(-sub * 2.4f);
                float bassWave = (bp < 0.5f ? 0.5f : -0.5f) * 0.35f + sinf(bp * 6.28318f) * 0.65f;
                float bOut = bassWave * 0.14f * bassEnv;

                /* 2. Stereo Harmonic Pad / Jazz 7th Chords */
                padP1 += (bf * (sg == 6 ? 2.50f : 2.0f)) * invSR;  if (padP1 >= 1.0f) padP1 -= 1.0f;
                padP2 += (bf * (sg == 6 ? 3.56f : 2.996f)) * invSR; if (padP2 >= 1.0f) padP2 -= 1.0f;
                float padL = sinf(padP1 * 6.28318f) * (sg == 6 ? 0.042f : 0.032f);
                float padR = sinf(padP2 * 6.28318f) * (sg == 6 ? 0.042f : 0.032f);

                /* 3. Lead / Chord Instrument Track (shaped by Song & Active Musical Style) */
                float lf = songLead[sg][st];
                lp1 += lf * invSR;            if (lp1 >= 1.0f) lp1 -= 1.0f;
                lp2 += (lf * 1.498f) * invSR; if (lp2 >= 1.0f) lp2 -= 1.0f; /* Power 5th harmony */
                lp3 += (lf * 1.004f) * invSR; if (lp3 >= 1.0f) lp3 -= 1.0f; /* Stereo detune chorus */
                vibP += 5.09f * invSR;        if (vibP >= 1.0f) vibP -= 1.0f;
                float leadEnv = expf(-sub * 1.8f);
                float leadL = 0.0f, leadR = 0.0f;
                if (sg == 6) {
                    /* Warm Lounge Rhodes / Saxophone Jazz Tone */
                    float vibrato = 1.0f + sinf(vibP * 6.28318f) * 0.003f;
                    sp += (lf * vibrato) * invSR; if (sp >= 1.0f) sp -= 1.0f;
                    float jEnv = expf(-sub * 1.3f);
                    float rhodes = sinf(sp * 6.28318f) * 0.72f + sinf(sp * 12.56636f) * 0.22f + sinf(lp2 * 6.28318f) * 0.14f;
                    leadL = rhodes * 0.11f * jEnv;
                    leadR = (sinf(lp3 * 6.28318f) * 0.72f + sinf(lp3 * 12.56636f) * 0.22f) * 0.11f * jEnv;
                } else if (stStyle == 0 || sg == 2 || sg == 4) {
                    /* Distorted Epic Rock Power-Chord Guitar */
                    float rawL = (2.0f * lp1 - 1.0f) * 0.7f + (2.0f * lp2 - 1.0f) * 0.45f;
                    float rawR = (2.0f * lp3 - 1.0f) * 0.7f + (2.0f * lp2 - 1.0f) * 0.45f;
                    leadL = (rawL / (1.0f + fabsf(rawL))) * 0.10f * leadEnv;
                    leadR = (rawR / (1.0f + fabsf(rawR))) * 0.10f * leadEnv;
                } else if (stStyle == 1 || sg == 3) {
                    /* Detuned Arcane Supersaw Synth */
                    leadL = ((2.0f * lp1 - 1.0f) * 0.6f + sinf(lp2 * 6.28318f) * 0.4f) * 0.095f * leadEnv;
                    leadR = ((2.0f * lp3 - 1.0f) * 0.6f + sinf(lp2 * 6.28318f) * 0.4f) * 0.095f * leadEnv;
                } else {
                    /* Symphonic Lush String Ensemble */
                    float vibrato = 1.0f + sinf(vibP * 6.28318f) * 0.004f;
                    sp += (lf * vibrato) * invSR; if (sp >= 1.0f) sp -= 1.0f;
                    leadL = (sinf(sp * 6.28318f) * 0.65f + (2.0f * lp2 - 1.0f) * 0.25f) * 0.10f * (0.4f + 0.6f * sinf(sub * 3.14159f));
                    leadR = (sinf(lp3 * 6.28318f) * 0.65f + (2.0f * lp2 - 1.0f) * 0.25f) * 0.10f * (0.4f + 0.6f * sinf(sub * 3.14159f));
                }

                /* 4. Percussion Track (Jazz Brushes on sg==6, Double Kick on Bosses) */
                float drum = 0.0f;
                int kickHit = (sg == 6) ? ((st & 7) == 0) : (((st & 3) == 0) || (sg >= 2 && (st & 7) == 6));
                if (kickHit && sub < 0.35f) {
                    float kf = 115.0f * expf(-sub * 14.0f) + 42.0f;
                    dp += kf * invSR; if (dp >= 1.0f) dp -= 1.0f;
                    drum += sinf(dp * 6.28318f) * (sg == 6 ? 0.07f : 0.14f) * expf(-sub * 12.0f);
                }
                noiseSeed = noiseSeed * 1664525u + 1013904223u;
                float nz = ((float)(int)(noiseSeed >> 16) / 32768.0f);
                if (sg == 6) {
                    if ((st & 1) == 0 && sub < 0.14f) drum += nz * 0.032f * expf(-sub * 22.0f); /* Jazz brushed ride */
                } else if ((st & 3) == 2 && sub < 0.28f) {
                    drum += nz * 0.085f * expf(-sub * 16.0f); /* Snare */
                } else if (sub < 0.09f) {
                    drum += nz * 0.030f * expf(-sub * 32.0f); /* Hi-hat */
                }

                /* 5. Stereo Ping-Pong Echo / Reverb on Lead */
                int tapR = (echoIdx - 2800) & 4095;
                int tapL = (echoIdx - 4095) & 4095;
                float wetL = echoR[tapL] * 0.28f;
                float wetR = echoL[tapR] * 0.28f;
                echoL[echoIdx] = leadL + wetL * 0.35f;
                echoR[echoIdx] = leadR + wetR * 0.35f;
                echoIdx = (echoIdx + 1) & 4095;

                lSig = bOut + padL + leadL + wetL + drum;
                rSig = bOut + padR + leadR + wetR + drum;

                /* 6. In-Key Guitar Hero Note Pluck / SFX Overlay */
                if (sfx_t > 0) {
                    sfx_p  += sfx_f * invSR;
                    sfx_p2 += (sfx_f * 1.5f) * invSR;
                    if (sfx_p >= 1.0f) sfx_p -= 1.0f;
                    if (sfx_p2 >= 1.0f) sfx_p2 -= 1.0f;
                    float env = (float)sfx_t / (float)sfx_max;
                    float noteWave = (stStyle == 0)
                        ? ((2.0f * sfx_p - 1.0f) * 0.6f + (2.0f * sfx_p2 - 1.0f) * 0.4f)
                        : (sinf(sfx_p * 6.28318f) * 0.7f + sinf(sfx_p2 * 6.28318f) * 0.3f);
                    float sOut = noteWave * 0.19f * env;
                    lSig += sOut; rSig += sOut;
                    sfx_t--;
                }
            }
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

typedef struct { int lane; float y; int active; int spawnStep; float freq; } Note;
static Note notes[24];
static const char *bossNames[3] = { "GOLEM DE HIERRO", "AUTOMATA ARCANO", "DRAGON SOMBRIO" };
static const char *mobNames[3]  = { "SLIME DEL BOSQUE", "TRASGO ERRANTE", "ESPECTRO MENOR" };
static const char *styleNames[3] = { "ROCK", "SYNTH", "SINFONICO" };
static const int bossWeak[3] = { 0, 1, 2 };

static int chapter = 0, isMobBattle = 0, bossHitFlash = 0;
static int pHp = 100, pMaxHp = 100, pMp = 50, pMaxMp = 50, bHp = 120, bMaxHp = 120;
static int numEnemies = 1, targetEnemy = 0, turnMember = 0, dodgeMember = 0, guardActive = 0, attackMode = 0;
static int eHp[3] = { 100, 0, 0 }, eMaxHp[3] = { 100, 0, 0 }, eType[3] = { 0, 0, 0 }, eIsBoss[3] = { 0, 0, 0 };
static int pLevel = 1, pXp = 0, pNextXp = 60, pBaseAtk = 5;
static int combo = 0, score = 0, best = 0, tick = 0, phaseTimer = 0;
static int relicDmg = 0, relicVamp = 0, relicShield = 0;
static int inTitle = 1, inOverworld = 0, inDungeon = 0, wasInDungeon = 0, inStory = 0, inFFMenu = 0, inDefense = 0, inAttackResolve = 0, inRelic = 0, gameOver = 0, victory = 0;
static int ffCursor = 0, pendingDmg = 0;
static float bardX = 240.0f, bardY = 195.0f;
static float projX[16], projY[16], projVx[16], projVy[16];
static int projAlive[16];
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

/* ================= VEGETACION, INSECTOS, ILUMINACION Y CLIMA ================= */
#define MAX_BUGS 22
#define MAX_RAIN 56
typedef struct { float x, y, vx, vy, phase; unsigned char type, active; } Bug;
typedef struct { float x, y, groundY, spd; unsigned char splash; } RainDrop;
static Bug bugs[MAX_BUGS];
static RainDrop rain[MAX_RAIN];
static int atmoInit = 0, weatherMode = 0, weatherTimer = 0; /* 0=Sol/Claro, 1=Niebla, 2=Lluvia, 3=Tormenta+Niebla */

static void init_atmosphere(void) {
    if (atmoInit) return;
    atmoInit = 1;
    for (int i = 0; i < MAX_BUGS; i++) {
        bugs[i].x = (float)(xr() % SCR_W);
        bugs[i].y = (float)(26 + (xr() % (SCR_H - 36)));
        bugs[i].vx = ((float)((int)(xr() % 13) - 6)) * 0.08f;
        bugs[i].vy = ((float)((int)(xr() % 11) - 5)) * 0.06f;
        bugs[i].phase = (float)(xr() % 628) * 0.01f;
        bugs[i].type = (unsigned char)(i % 3); /* 0=Mariposa, 1=Luciernaga, 2=Libelula */
        bugs[i].active = 1;
    }
    for (int i = 0; i < MAX_RAIN; i++) {
        rain[i].x = (float)(xr() % SCR_W);
        rain[i].y = (float)(xr() % SCR_H);
        rain[i].groundY = (float)(40 + (xr() % (SCR_H - 44)));
        rain[i].spd = 4.5f + (float)(xr() % 25) * 0.14f;
        rain[i].splash = 0;
    }
}

static void draw_tile_vegetation(unsigned int *fb, int wx, int wy, int sx, int sy, int isWater) {
    unsigned int h = ((unsigned int)wx * 73856093u) ^ ((unsigned int)wy * 19349663u);
    if (isWater) {
        if ((h % 19u) == 0u) {
            int lx = sx + 5 + (int)(h % 10u), ly = sy + 6 + (int)((h >> 4) % 10u);
            fill_ellipse_alpha(fb, lx + 4, ly + 3, 5, 3, RGB(32, 130, 62), 210);
            fill_rect_alpha(fb, lx + 3, ly + 1, 2, 2, RGB(245, 165, 205), 220);
        }
        return;
    }
    unsigned int kind = h % 29u;
    int sway = (int)(sinf((float)animFrame * 0.07f + (float)(wx + wy) * 0.7f) * 1.4f);
    if (kind < 6u) {
        /* Swaying tall grass blades */
        int gx = sx + 4 + (int)((h >> 3) % 13u);
        int gy = sy + 14 + (int)((h >> 7) % 6u);
        for (int b = 0; b < 3; b++) {
            int bx = gx + b * 3;
            fill_rect(fb, bx, gy - 2, 1, 4, RGB(24, 98, 36));
            fill_rect(fb, bx + sway, gy - 5, 1, 3, RGB(78, 192, 74));
        }
    } else if (kind < 9u) {
        /* Wildflowers (gold, sky blue, crimson) */
        static const unsigned int fcols[3] = { RGB(255, 220, 65), RGB(115, 205, 255), RGB(255, 110, 145) };
        unsigned int fc = fcols[(h >> 5) % 3u];
        for (int f = 0; f < 2; f++) {
            int fx = sx + 5 + f * 9 + (int)((h >> (f * 3)) % 5u);
            int fy = sy + 11 + (int)((h >> (f * 4 + 2)) % 8u);
            fill_rect(fb, fx, fy + 2, 1, 3, RGB(34, 125, 48));
            fill_rect(fb, fx - 1 + sway, fy, 3, 2, fc);
            fill_rect(fb, fx + sway, fy + 1, 1, 1, RGB(255, 250, 210));
        }
    } else if (kind == 10u) {
        /* Lush fern / bush cluster */
        int bx = sx + 12, by = sy + 14;
        fill_ellipse_alpha(fb, bx, by + 2, 7, 4, RGB(0, 0, 0), 75);
        fill_ellipse_alpha(fb, bx, by, 7, 5, RGB(22, 95, 38), 235);
        fill_ellipse_alpha(fb, bx + sway - 1, by - 2, 5, 3, RGB(64, 175, 68), 235);
    }
}

static void update_draw_bugs(unsigned int *fb, int isForest, int isDarkWeather) {
    init_atmosphere();
    int activeLimit = isForest ? MAX_BUGS : 14;
    for (int i = 0; i < activeLimit; i++) {
        Bug *b = &bugs[i];
        b->phase += 0.14f;
        if ((xr() % 45) == 0) {
            b->vx += ((float)((int)(xr() % 9) - 4)) * 0.05f;
            b->vy += ((float)((int)(xr() % 9) - 4)) * 0.04f;
            if (b->vx > 0.9f) b->vx = 0.9f; if (b->vx < -0.9f) b->vx = -0.9f;
            if (b->vy > 0.7f) b->vy = 0.7f; if (b->vy < -0.7f) b->vy = -0.7f;
        }
        b->x += b->vx + cosf(b->phase * 0.5f) * 0.25f;
        b->y += b->vy + sinf(b->phase) * 0.35f;
        if (b->x < -6) b->x = SCR_W + 4; if (b->x > SCR_W + 6) b->x = -4;
        if (b->y < 24) b->y = SCR_H - 8; if (b->y > SCR_H - 4) b->y = 26;

        int ix = (int)b->x, iy = (int)b->y;
        if (b->type == 1 || isDarkWeather || isForest) {
            /* Glowing Firefly */
            int glow = 65 + (int)(sinf(b->phase * 1.3f) * 45.0f);
            if (glow < 20) glow = 20;
            fill_ellipse_alpha(fb, ix, iy, 5, 5, RGB(175, 255, 85), glow);
            fill_rect(fb, ix, iy, 2, 2, RGB(245, 255, 170));
        } else if (b->type == 0) {
            /* Fluttering Butterfly */
            int flap = ((animFrame / 4 + i) & 1) ? 2 : 1;
            unsigned int wcol = (i & 1) ? RGB(255, 195, 70) : RGB(115, 225, 255);
            fill_rect_alpha(fb, ix - flap, iy - 1, flap, 2, wcol, 230);
            fill_rect_alpha(fb, ix + 1, iy - 1, flap, 2, wcol, 230);
            fill_rect(fb, ix, iy - 1, 1, 3, RGB(40, 28, 18));
        } else {
            /* Iridescent Dragonfly */
            fill_rect(fb, ix - 1, iy, 4, 1, RGB(70, 225, 210));
            if ((animFrame + i) & 1) fill_rect_alpha(fb, ix, iy - 2, 2, 4, RGB(215, 245, 255), 160);
        }
    }
}

static void update_draw_weather_and_lighting(unsigned int *fb, int mapXPos, int mapYPos) {
    init_atmosphere();
    weatherTimer++;
    if (weatherTimer >= 1200) { /* Shift weather every ~20 seconds */
        weatherTimer = 0;
        weatherMode = (weatherMode + 1 + (xr() & 1)) & 3;
    }

    int inVolcano = (mapXPos >= 165 && mapYPos < 85);
    int inArcane  = (mapXPos >= 118 && mapXPos < 165);

    if (inVolcano) {
        /* Warm volcanic ambient + drifting glowing embers */
        fill_rect_alpha(fb, 0, 24, SCR_W, SCR_H - 24, RGB(75, 22, 8), 28);
        for (int e = 0; e < 14; e++) {
            int ex = ((e * 37 + animFrame * (1 + (e & 1))) % SCR_W);
            int ey = SCR_H - ((animFrame * (1 + (e % 3)) + e * 29) % (SCR_H - 24));
            fill_rect_alpha(fb, ex, ey, 2, 2, (e & 1) ? RGB(255, 155, 45) : RGB(255, 85, 35), 185);
        }
    } else if (weatherMode == 2 || weatherMode == 3) {
        /* Overcast cool lighting + diagonal rain + ground splashes */
        fill_rect_alpha(fb, 0, 24, SCR_W, SCR_H - 24, RGB(14, 24, 52), (weatherMode == 3) ? 46 : 30);
        int dropCount = (weatherMode == 3) ? MAX_RAIN : 36;
        for (int i = 0; i < dropCount; i++) {
            RainDrop *r = &rain[i];
            if (r->splash > 0) {
                int rad = 5 - r->splash;
                fill_ellipse_alpha(fb, (int)r->x, (int)r->y, rad, 2, RGB(180, 220, 255), r->splash * 35);
                r->splash--;
                if (r->splash == 0) {
                    r->x = (float)(xr() % (SCR_W + 40));
                    r->y = 24.0f;
                    r->groundY = (float)(45 + (xr() % (SCR_H - 48)));
                }
            } else {
                r->x -= 1.2f;
                r->y += r->spd;
                int rx = (int)r->x, ry = (int)r->y;
                if (rx >= 0 && rx < SCR_W && ry >= 24 && ry < SCR_H - 6) {
                    fill_rect_alpha(fb, rx, ry, 1, 5, RGB(175, 215, 255), 155);
                    fill_rect_alpha(fb, rx + 1, ry - 4, 1, 4, RGB(140, 185, 235), 95);
                }
                if (r->y >= r->groundY || r->y >= SCR_H - 4) {
                    r->splash = 4;
                }
            }
        }
    } else if (weatherMode == 0) {
        /* Golden Sunlight Shafts (God Rays) */
        for (int g = 0; g < 3; g++) {
            int gx = ((g * 165 + (animFrame / 3)) % (SCR_W + 120)) - 60;
            for (int strip = 0; strip < 10; strip++) {
                int sx = gx + strip * 6;
                int sy = 25 + strip * 22;
                if (sy < SCR_H) fill_rect_alpha(fb, sx, sy, 36, 24, RGB(255, 240, 175), 14);
            }
        }
    }

    /* Drifting Parallax Mist / Fog Layers (stronger in Fog/Storm or Arcane Forest) */
    int fogAlpha = (weatherMode == 1) ? 32 : (weatherMode == 3 ? 24 : (inArcane ? 20 : 10));
    for (int f = 0; f < 5; f++) {
        int fx = ((f * 130 + animFrame * ((f & 1) ? 1 : 2) / 2) % (SCR_W + 180)) - 90;
        int fy = 52 + f * 44;
        fill_ellipse_alpha(fb, fx, fy, 88, 16, inArcane ? RGB(175, 235, 215) : RGB(215, 228, 245), fogAlpha);
    }

    /* Soft Radial Hero Light Aura + Screen-Edge Atmospheric Vignette */
    fill_ellipse_alpha(fb, 255, 130, 52, 40, RGB(255, 235, 175), (weatherMode >= 2) ? 26 : 14);
    fill_rect_alpha(fb, 0, 24, 12, SCR_H - 24, RGB(6, 8, 18), 32);
    fill_rect_alpha(fb, SCR_W - 12, 24, 12, SCR_H - 24, RGB(6, 8, 18), 32);
    fill_rect_alpha(fb, 0, SCR_H - 10, SCR_W, 10, RGB(6, 8, 18), 38);
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

#define MAX_ROAM 32
typedef struct { int x, y, type, alive, flip, respawn; } RoamMob;
static RoamMob roam[MAX_ROAM];

#define MAX_TOWN_NPC 10
typedef struct { int x, y, homeX, homeY, flip, kind; } TownNPC;
static TownNPC townNpcs[MAX_TOWN_NPC];

static int is_tile_blocked(int x, int y) {
    if (x < 0 || x >= MW || y < 0 || y >= MH) return 1;
    unsigned char t = worldMap[y][x];
    if (t == 1 || t == 2 || t == 3 || t == 8 || t >= 10) return 1;
    for (int i = 0; i < 8; i++) {
        if (x == npcX[i] && y == npcY[i]) return 1;
    }
    for (int i = 0; i < MAX_TOWN_NPC; i++) {
        if (x == townNpcs[i].x && y == townNpcs[i].y) return 1;
    }
    return 0;
}

static void spawn_roam_mob(int idx, int nearGate) {
    int rx = 50, ry = 84;
    if (nearGate == 0) { rx = 43; ry = 84; }
    else if (nearGate == 1) { rx = 48; ry = 78; }
    else if (nearGate == 2) { rx = 28; ry = 69; }
    else {
        for (int tries = 0; tries < 40; tries++) {
            int tx = 6 + (xr() % (MW - 12));
            int ty = 6 + (xr() % (MH - 12));
            unsigned char t = worldMap[ty][tx];
            if ((t == 0 || t == 7) && !is_tile_blocked(tx, ty) && (abs(tx - mapX) + abs(ty - mapY) > 10)) {
                rx = tx; ry = ty; break;
            }
        }
    }
    roam[idx].x = rx; roam[idx].y = ry;
    roam[idx].type = (rx < 118) ? 0 : ((ry >= 85) ? 1 : 2);
    roam[idx].alive = 1; roam[idx].flip = 0; roam[idx].respawn = 0;
}

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
    build_walled_town(18, 74, 38, 94);     /* Ciudadela de Solaria */
    build_walled_town(76, 56, 86, 64);     /* Aldea de Bruma */
    build_walled_town(150, 108, 160, 116); /* Taberna del Trovador */

    /* Decoracion de Solaria: Casas (10), Taberna (11), Fuente (12), Mercado (13), Farolas (14), Pozo (15) */
    worldMap[77][21] = 10; worldMap[77][25] = 10; worldMap[77][31] = 11; worldMap[77][35] = 10;
    worldMap[90][21] = 10; worldMap[90][26] = 10; worldMap[90][30] = 10; worldMap[90][35] = 10;
    worldMap[82][20] = 10; worldMap[82][36] = 10; worldMap[87][20] = 11; worldMap[87][36] = 10;
    worldMap[84][28] = 12; /* Fuente central de la plaza de Solaria */
    worldMap[80][24] = 13; worldMap[80][28] = 13; worldMap[80][32] = 13; /* Puestos de mercado */
    worldMap[84][24] = 13; worldMap[84][32] = 13;
    worldMap[88][24] = 15; worldMap[88][32] = 15; /* Pozos de piedra */
    worldMap[78][28] = 14; worldMap[82][22] = 14; worldMap[82][34] = 14;
    worldMap[86][22] = 14; worldMap[86][34] = 14; worldMap[89][28] = 14; /* Farolas */
    worldMap[79][20] = 1;  worldMap[79][36] = 1;  /* Arboles ornamentales */

    /* Decoracion de Aldea de Bruma */
    worldMap[58][78] = 10; worldMap[58][83] = 10;
    worldMap[61][78] = 13; worldMap[61][83] = 13; worldMap[62][80] = 15;
    worldMap[59][81] = 12; /* Fuente de Bruma */
    worldMap[60][77] = 14; worldMap[60][84] = 14; worldMap[57][81] = 14;

    /* Decoracion de Taberna del Trovador */
    worldMap[110][153] = 11; worldMap[110][157] = 10;
    worldMap[113][157] = 13; worldMap[113][153] = 13; worldMap[114][152] = 12;
    worldMap[112][151] = 14; worldMap[112][159] = 14; worldMap[114][158] = 15;

    /* Ciudadanos ambulantes en las 3 ciudades */
    static const int tnCoords[MAX_TOWN_NPC][2] = {
        {26, 83}, {30, 83}, {25, 86}, {31, 86}, {28, 80}, {28, 88},
        {79, 60}, {82, 60}, {154, 112}, {156, 113}
    };
    for (int i = 0; i < MAX_TOWN_NPC; i++) {
        townNpcs[i].x = townNpcs[i].homeX = tnCoords[i][0];
        townNpcs[i].y = townNpcs[i].homeY = tnCoords[i][1];
        townNpcs[i].flip = (i & 1);
        townNpcs[i].kind = (i % 3);
    }

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
    for (int i = 0; i < MAX_ROAM; i++) spawn_roam_mob(i, i < 3 ? i : -1);
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

/* PARTE 1B: Dungeons Aleatorios Estilo Diablo (Salas + Pasillos + Pisos + Niebla de Guerra) */
#define DW 44
#define DH 34
static unsigned char dngMap[DH][DW];  /* 0=muro, 1=suelo, 2=escalera/altar, 3=cofre, 4=salida */
static unsigned char dngSeen[DH][DW];
static int curDngId = 0, dngFloor = 1, dngMaxFloors = 2;
static int dngPx = 6, dngPy = 6, dngP1x = 6, dngP1y = 6, dngP2x = 6, dngP2y = 6;
static int dngExitX = 6, dngExitY = 6, dngStairX = 36, dngStairY = 26;
static RoamMob dngMobs[6];

static void reveal_dungeon_fog(int px, int py) {
    for (int dy = -5; dy <= 5; dy++) {
        for (int dx = -5; dx <= 5; dx++) {
            int nx = px + dx, ny = py + dy;
            if (nx >= 0 && nx < DW && ny >= 0 && ny < DH && (dx * dx + dy * dy <= 26)) {
                dngSeen[ny][nx] = 1;
            }
        }
    }
}

static void generate_dungeon_floor(int dId, int flr) {
    curDngId = dId;
    dngFloor = flr;
    dngMaxFloors = (dId == 0) ? 2 : 3;
    memset(dngMap, 0, sizeof(dngMap));
    memset(dngSeen, 0, sizeof(dngSeen));

    int rcx[8], rcy[8];
    for (int r = 0; r < 7; r++) {
        int rw = 4 + (xr() % 5);
        int rh = 4 + (xr() % 4);
        int rx = 2 + (xr() % (DW - rw - 4));
        int ry = 2 + (xr() % (DH - rh - 4));
        for (int y = ry; y < ry + rh; y++) {
            for (int x = rx; x < rx + rw; x++) dngMap[y][x] = 1;
        }
        rcx[r] = rx + rw / 2;
        rcy[r] = ry + rh / 2;
        if (r > 0) {
            int cx = rcx[r - 1], cy = rcy[r - 1];
            while (cx != rcx[r]) { dngMap[cy][cx] = 1; cx += (rcx[r] > cx) ? 1 : -1; }
            while (cy != rcy[r]) { dngMap[cy][cx] = 1; cy += (rcy[r] > cy) ? 1 : -1; }
        }
    }
    /* Player starts in Room 0, Exit/Up Stairs on Floor 1 */
    dngPx = rcx[0]; dngPy = rcy[0];
    dngP1x = dngPx; dngP1y = dngPy; dngP2x = dngPx; dngP2y = dngPy;
    dngExitX = rcx[0]; dngExitY = rcy[0] - 1;
    if (flr == 1 && dngExitY > 1) { dngMap[dngExitY][dngExitX] = 4; }

    /* Stairs Down / Boss Altar in Room 6 */
    dngStairX = rcx[6]; dngStairY = rcy[6];
    dngMap[dngStairY][dngStairX] = 2;

    /* 2 Treasure Chests in side rooms (Rooms 2 & 4) */
    dngMap[rcy[2]][rcx[2]] = 3;
    dngMap[rcy[4]][rcx[4]] = 3;

    /* 5 Visible Patrolling Dungeon Monsters */
    for (int m = 0; m < 5; m++) {
        int rm = 1 + (m % 5);
        dngMobs[m].x = rcx[rm]; dngMobs[m].y = rcy[rm];
        dngMobs[m].type = dId; dngMobs[m].alive = 1; dngMobs[m].flip = 0; dngMobs[m].respawn = 0;
    }
    reveal_dungeon_fog(dngPx, dngPy);
    snprintf(dlgText, sizeof(dlgText), "MAZMORRA %d - PISO %d/%d! Busca la escalera.", dId + 1, flr, dngMaxFloors);
    dlgTimer = 180;
}

static void enter_dungeon(int dId) {
    inOverworld = 0;
    inDungeon = 1;
    wasInDungeon = 1;
    generate_dungeon_floor(dId, 1);
    play_sfx(420.0f, 1400);
}

static void init_battle_enemies(int isMob, int ch) {
    static const int mobHpTbl[3]  = { 55, 95, 150 };
    static const int bossHpTbl[3] = { 260, 420, 660 };
    if (isMob) {
        numEnemies = 2 + (xr() & 1); /* 2 or 3 enemies from the very beginning! */
        for (int i = 0; i < numEnemies; i++) {
            eIsBoss[i] = 0;
            eType[i] = (i == 0) ? ch : ((ch + (xr() & 1)) % 3);
            if (eType[i] > ch) eType[i] = ch;
            eHp[i] = eMaxHp[i] = mobHpTbl[eType[i]];
        }
    } else {
        numEnemies = 3; /* 1 Boss + 2 Escorts! */
        eIsBoss[0] = 1; eType[0] = ch; eHp[0] = eMaxHp[0] = bossHpTbl[ch];
        for (int i = 1; i < 3; i++) {
            eIsBoss[i] = 0; eType[i] = ch;
            eHp[i] = eMaxHp[i] = mobHpTbl[ch];
        }
    }
    targetEnemy = 0;
    turnMember = 0;
    guardActive = 0;
    attackMode = 0;
    bHp = eHp[0]; bMaxHp = eMaxHp[0];
}

static void start_mob_battle_type(int mtype) {
    isMobBattle = 1;
    chapter = mtype;
    init_battle_enemies(1, mtype);
    combo = 0; tick = 0; judgeT = 0; phaseTimer = 0; bossHitFlash = 0;
    bardX = 240.0f; bardY = 195.0f;
    memset(notes, 0, sizeof(notes));
    memset(projAlive, 0, sizeof(projAlive));
    wasInDungeon = inDungeon;
    inOverworld = 0; inDungeon = 0; inStory = 0; inFFMenu = 1; ffCursor = 0; inDefense = 0; inAttackResolve = 0; pendingDmg = 0;
}

static void start_mob_battle(void) {
    int mtype = (mapX < 118) ? 0 : ((mapY >= 85) ? 1 : 2);
    start_mob_battle_type(mtype);
}

static void start_chapter(int ch) {
    isMobBattle = 0;
    chapter = ch;
    init_battle_enemies(0, ch);
    combo = 0; tick = 0; judgeT = 0; phaseTimer = 0; bossHitFlash = 0;
    bardX = 240.0f; bardY = 195.0f;
    memset(notes, 0, sizeof(notes));
    memset(projAlive, 0, sizeof(projAlive));
    inOverworld = 0; inDungeon = 0; wasInDungeon = 0; inStory = 1; inFFMenu = 0; ffCursor = 0; inDefense = 0; inAttackResolve = 0; pendingDmg = 0;
}

static int lastRegionId = -1, regionBannerTimer = 0;
static const char * const regionNames[7] = {
    "CIUDAD DE SOLARIA",
    "ALDEA DE BRUMA",
    "TABERNA DEL TROVADOR",
    "VALLE ESMERALDA",
    "BOSQUE ARCANO",
    "CORDILLERA CENIZA",
    "COSTA SOMBRIA"
};
static const char * const regionSubs[7] = {
    "Plaza Real, Mercado y Jazz",
    "Posada del Camino del Norte",
    "Refugio Musical del Bosque",
    "Caminos y Praderas de Aethel",
    "Niebla Mistica y Luciernagas",
    "Tierras del Dragon Sombrio",
    "Acantilados del Sur"
};
static int get_region_id(int x, int y) {
    if (x >= 18 && x <= 38 && y >= 74 && y <= 94) return 0; /* Solaria */
    if (x >= 74 && x <= 86 && y >= 56 && y <= 66) return 1; /* Villa del Puente */
    if (x >= 148 && x <= 162 && y >= 106 && y <= 116) return 2; /* Silvaluz */
    if (x >= 165 && y < 85) return 5; /* Cordillera Ceniza */
    if (x >= 165 && y >= 85) return 6; /* Costa Sombria */
    if (x >= 118) return 4; /* Bosque Arcano */
    return 3; /* Valle Esmeralda */
}

static void start_game(void) {
    pLevel = 1; pXp = 0; pNextXp = 60; pBaseAtk = 5;
    pHp = pMaxHp = 100; pMp = pMaxMp = 50; score = 0;
    relicDmg = 0; relicVamp = 0; relicShield = 0;
    lastRegionId = -1; regionBannerTimer = 0;
    init_world();
    inTitle = 0; inOverworld = 1; inDungeon = 0; wasInDungeon = 0; inStory = 0; inFFMenu = 0; inDefense = 0; inAttackResolve = 0; inRelic = 0; gameOver = 0; victory = 0; pendingDmg = 0;
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

        /* Select Song dynamically: 0=Aventura (Caminos/Bosques), 6=Jazz (Ciudades), 1-5=Combate/Dungeons */
        int curRegId = get_region_id(mapX, mapY);
        if (inTitle) curSong = 0;
        else if (inOverworld) curSong = (curRegId <= 2 || worldMap[mapY][mapX] == 4 || worldMap[mapY][mapX] >= 10) ? 6 : 0;
        else if (inDungeon) curSong = (curDngId == 0) ? 1 : (curDngId == 1 ? 3 : 4);
        else if (isMobBattle) curSong = (chapter == 0) ? 1 : (chapter == 1 ? 3 : 4);
        else curSong = (chapter == 0) ? 2 : (chapter == 1 ? 3 : 5);

        /* ================= LOGIC ================= */
        if (inTitle || gameOver || victory) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        } else if (inOverworld) {
            if (dlgTimer > 0) dlgTimer--;
            if (regionBannerTimer > 0) regionBannerTimer--;
            if (curRegId != lastRegionId) {
                lastRegionId = curRegId;
                regionBannerTimer = 165;
            }
            if (moveCd > 0) moveCd--;

            int dx = 0, dy = 0;
            float ax = ((float)pad.Lx - 128.0f) / 128.0f, ay = ((float)pad.Ly - 128.0f) / 128.0f;
            if ((pad.Buttons & PSP_CTRL_LEFT)  || ax < -0.45f) dx = -1;
            else if ((pad.Buttons & PSP_CTRL_RIGHT) || ax >  0.45f) dx =  1;
            else if ((pad.Buttons & PSP_CTRL_UP)    || ay < -0.45f) dy = -1;
            else if ((pad.Buttons & PSP_CTRL_DOWN)  || ay >  0.45f) dy =  1;

            if (dx == 0 && dy == 0) moveCd = 0;
            if ((dx != 0 || dy != 0) && moveCd <= 0) {
                moveCd = 6;
                int nx = mapX + dx, ny = mapY + dy;
                if (!is_tile_blocked(nx, ny)) {
                    band2Flip = (prevX2 > prevX1) ? 1 : (prevX2 < prevX1 ? 0 : band1Flip);
                    band1Flip = (prevX1 > mapX)   ? 1 : (prevX1 < mapX   ? 0 : bardFlip);
                    if (dx < 0) bardFlip = 1;
                    else if (dx > 0) bardFlip = 0;
                    prevX2 = prevX1; prevY2 = prevY1;
                    prevX1 = mapX;   prevY1 = mapY;
                    mapX = nx;       mapY = ny;
                    for (int i = 0; i < 3; i++) {
                        if (!bossDefeated[i] && mapX == dngX[i] && mapY == dngY[i]) {
                            enter_dungeon(i);
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
                    for (int i = 0; i < MAX_ROAM; i++) {
                        if (roam[i].alive && roam[i].x == mapX && roam[i].y == mapY) {
                            roam[i].alive = 0; roam[i].respawn = 300;
                            start_mob_battle_type(roam[i].type);
                            break;
                        }
                    }
                    if (inOverworld && worldMap[mapY][mapX] == 1 && (xr() % 11) == 0) {
                        start_mob_battle();
                    }
                }
            }
            /* Patrol visible roaming enemies every 45 frames & stroll town villagers every 36 frames */
            if (inOverworld && (animFrame % 36) == 0) {
                static const int tdx[4] = { 1, -1, 0, 0 };
                static const int tdy[4] = { 0, 0, 1, -1 };
                for (int i = 0; i < MAX_TOWN_NPC; i++) {
                    int dir = xr() & 3;
                    int nx = townNpcs[i].x + tdx[dir], ny = townNpcs[i].y + tdy[dir];
                    if (abs(nx - townNpcs[i].homeX) <= 4 && abs(ny - townNpcs[i].homeY) <= 4 &&
                        (nx != mapX || ny != mapY) && worldMap[ny][nx] == 4 && !is_tile_blocked(nx, ny)) {
                        if (tdx[dir] < 0) townNpcs[i].flip = 1;
                        else if (tdx[dir] > 0) townNpcs[i].flip = 0;
                        townNpcs[i].x = nx; townNpcs[i].y = ny;
                    }
                }
            }
            if (inOverworld && (animFrame % 45) == 0) {
                static const int rdx[4] = { 1, -1, 0, 0 };
                static const int rdy[4] = { 0, 0, 1, -1 };
                for (int i = 0; i < MAX_ROAM; i++) {
                    if (!roam[i].alive) {
                        if (--roam[i].respawn <= 0) spawn_roam_mob(i, -1);
                        continue;
                    }
                    int dir = xr() & 3;
                    int nx = roam[i].x + rdx[dir], ny = roam[i].y + rdy[dir];
                    if (nx > 4 && nx < MW - 4 && ny > 4 && ny < MH - 4) {
                        unsigned char rt = worldMap[ny][nx];
                        if ((rt == 0 || rt == 7) && !is_tile_blocked(nx, ny)) {
                            if (rdx[dir] > 0) roam[i].flip = 1;
                            else if (rdx[dir] < 0) roam[i].flip = 0;
                            roam[i].x = nx; roam[i].y = ny;
                            if (roam[i].x == mapX && roam[i].y == mapY) {
                                roam[i].alive = 0; roam[i].respawn = 300;
                                start_mob_battle_type(roam[i].type);
                                break;
                            }
                        }
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
        } else if (inDungeon) {
            if (dlgTimer > 0) dlgTimer--;
            if (moveCd > 0) moveCd--;
            int dx = 0, dy = 0;
            float ax = ((float)pad.Lx - 128.0f) / 128.0f, ay = ((float)pad.Ly - 128.0f) / 128.0f;
            if ((pad.Buttons & PSP_CTRL_LEFT)  || ax < -0.45f) dx = -1;
            else if ((pad.Buttons & PSP_CTRL_RIGHT) || ax >  0.45f) dx =  1;
            else if ((pad.Buttons & PSP_CTRL_UP)    || ay < -0.45f) dy = -1;
            else if ((pad.Buttons & PSP_CTRL_DOWN)  || ay >  0.45f) dy =  1;

            if (dx == 0 && dy == 0) moveCd = 0;
            if ((dx != 0 || dy != 0) && moveCd <= 0) {
                moveCd = 6;
                int nx = dngPx + dx, ny = dngPy + dy;
                if (nx >= 0 && nx < DW && ny >= 0 && ny < DH && dngMap[ny][nx] != 0) {
                    band2Flip = (dngP2x > dngP1x) ? 1 : (dngP2x < dngP1x ? 0 : band1Flip);
                    band1Flip = (dngP1x > dngPx)  ? 1 : (dngP1x < dngPx  ? 0 : bardFlip);
                    if (dx < 0) bardFlip = 1;
                    else if (dx > 0) bardFlip = 0;
                    dngP2x = dngP1x; dngP2y = dngP1y;
                    dngP1x = dngPx;  dngP1y = dngPy;
                    dngPx = nx;      dngPy = ny;
                    reveal_dungeon_fog(dngPx, dngPy);

                    unsigned char dt = dngMap[dngPy][dngPx];
                    if (dt == 3) {
                        dngMap[dngPy][dngPx] = 1;
                        pHp = (pHp + 35 > pMaxHp) ? pMaxHp : pHp + 35;
                        pMp = (pMp + 20 > pMaxMp) ? pMaxMp : pMp + 20;
                        pBaseAtk += 1;
                        snprintf(dlgText, sizeof(dlgText), "COFRE DE MAZMORRA! +35HP +20MP +1ATQ!");
                        dlgTimer = 180;
                        play_sfx(880.0f, 1200);
                    } else if (dt == 4) {
                        inDungeon = 0; wasInDungeon = 0; inOverworld = 1;
                        mapY += 1;
                        snprintf(dlgText, sizeof(dlgText), "Saliste de la Mazmorra al Mapa.");
                        dlgTimer = 140;
                    } else if (dt == 2) {
                        if (dngFloor < dngMaxFloors) {
                            generate_dungeon_floor(curDngId, dngFloor + 1);
                            play_sfx(660.0f, 1400);
                        } else {
                            start_chapter(curDngId);
                        }
                    }
                    if (inDungeon) {
                        for (int m = 0; m < 5; m++) {
                            if (dngMobs[m].alive && dngMobs[m].x == dngPx && dngMobs[m].y == dngPy) {
                                dngMobs[m].alive = 0;
                                start_mob_battle_type(dngMobs[m].type);
                                break;
                            }
                        }
                    }
                }
            }
            if (inDungeon && (animFrame % 42) == 0) {
                static const int ddx[4] = { 1, -1, 0, 0 };
                static const int ddy[4] = { 0, 0, 1, -1 };
                for (int m = 0; m < 5; m++) if (dngMobs[m].alive) {
                    int dir = xr() & 3;
                    int nx = dngMobs[m].x + ddx[dir], ny = dngMobs[m].y + ddy[dir];
                    if (nx > 0 && nx < DW - 1 && ny > 0 && ny < DH - 1 && dngMap[ny][nx] == 1) {
                        if (ddx[dir] > 0) dngMobs[m].flip = 1;
                        else if (ddx[dir] < 0) dngMobs[m].flip = 0;
                        dngMobs[m].x = nx; dngMobs[m].y = ny;
                        if (dngMobs[m].x == dngPx && dngMobs[m].y == dngPy) {
                            dngMobs[m].alive = 0;
                            start_mob_battle_type(dngMobs[m].type);
                            break;
                        }
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
            if (judgeT > 0) judgeT--;
            if (pressed & PSP_CTRL_UP)   { ffCursor = (ffCursor + 3) % 4; play_sfx(440.0f, 400); }
            if (pressed & PSP_CTRL_DOWN) { ffCursor = (ffCursor + 1) % 4; play_sfx(440.0f, 400); }
            if (pressed & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) {
                int dir = (pressed & PSP_CTRL_RIGHT) ? 1 : -1;
                for (int k = 1; k <= numEnemies; k++) {
                    int cand = (targetEnemy + dir * k + numEnemies * 2) % numEnemies;
                    if (eHp[cand] > 0) { targetEnemy = cand; break; }
                }
                play_sfx(520.0f, 400);
            }
            if (pressed & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) {
                curStyle = (curStyle + 1) % 3; play_sfx(520.0f, 600);
            }
            if (pressed & PSP_CTRL_CROSS) {
                int advanceTurn = 0;
                if (turnMember == 0) {
                    /* KAEL (Bardo - Guitarra) */
                    if (ffCursor == 0) {
                        attackMode = 0; inFFMenu = 0; inAttackResolve = 0; phaseTimer = 0; tick = 0; pendingDmg = 0; combo = 0;
                        memset(notes, 0, sizeof(notes)); play_sfx(660.0f, 800);
                    } else if (ffCursor == 1) {
                        curStyle = (curStyle + 1) % 3; play_sfx(520.0f, 600);
                    } else if (ffCursor == 2) {
                        if (pMp >= 12) {
                            pMp -= 12; attackMode = 2;
                            inFFMenu = 0; inAttackResolve = 0; phaseTimer = 0; tick = 0; pendingDmg = 0; combo = 0;
                            memset(notes, 0, sizeof(notes)); play_sfx(740.0f, 900);
                        } else play_sfx(180.0f, 600);
                    } else if (ffCursor == 3) {
                        if (isMobBattle) {
                            inFFMenu = 0;
                            if (wasInDungeon) inDungeon = 1; else inOverworld = 1;
                            snprintf(dlgText, sizeof(dlgText), "Escapaste del combate!"); dlgTimer = 100;
                            play_sfx(300.0f, 800);
                        } else play_sfx(180.0f, 600);
                    }
                } else if (turnMember == 1) {
                    /* LYRA (Elfa - Laud) */
                    if (ffCursor == 0) {
                        if (pMp >= 10) {
                            pMp -= 10; attackMode = 1; /* Hits ALL enemies! */
                            inFFMenu = 0; inAttackResolve = 0; phaseTimer = 0; tick = 0; pendingDmg = 0; combo = 0;
                            memset(notes, 0, sizeof(notes)); play_sfx(700.0f, 800);
                        } else play_sfx(180.0f, 600);
                    } else if (ffCursor == 1) {
                        if (pMp >= 15) {
                            pMp -= 15; pHp = (pHp + 45 > pMaxHp) ? pMaxHp : pHp + 45;
                            snprintf(judge, sizeof(judge), "LYRA CURA +45 HP!"); judgeT = 50;
                            play_sfx(880.0f, 1200); advanceTurn = 1;
                        } else play_sfx(180.0f, 600);
                    } else if (ffCursor == 2) {
                        pMp = (pMp + 20 > pMaxMp) ? pMaxMp : pMp + 20;
                        snprintf(judge, sizeof(judge), "CANTO MANA +20 MP!"); judgeT = 50;
                        play_sfx(780.0f, 1000); advanceTurn = 1;
                    } else {
                        if (pMp >= 8) {
                            pMp -= 8; guardActive = 1;
                            snprintf(judge, sizeof(judge), "ESCUDO ARMONICO!"); judgeT = 50;
                            play_sfx(640.0f, 900); advanceTurn = 1;
                        } else play_sfx(180.0f, 600);
                    }
                } else {
                    /* THORIN (Bajista Acorazado) */
                    if (ffCursor == 0) {
                        attackMode = 0; inFFMenu = 0; inAttackResolve = 0; phaseTimer = 0; tick = 0; pendingDmg = 0; combo = 0;
                        memset(notes, 0, sizeof(notes)); play_sfx(540.0f, 800);
                    } else if (ffCursor == 1) {
                        guardActive = 1;
                        snprintf(judge, sizeof(judge), "THORIN CUBRE (-50%%)!"); judgeT = 50;
                        play_sfx(480.0f, 800); advanceTurn = 1;
                    } else if (ffCursor == 2) {
                        int bdmg = 14 + pLevel * 2;
                        eHp[targetEnemy] -= bdmg; if (eHp[targetEnemy] < 0) eHp[targetEnemy] = 0;
                        bossHitFlash = 10;
                        snprintf(judge, sizeof(judge), "EMBESTIDA -%d HP!", bdmg); judgeT = 50;
                        play_sfx(320.0f, 900); advanceTurn = 1;
                    } else {
                        pMp = (pMp + 10 > pMaxMp) ? pMaxMp : pMp + 10;
                        snprintf(judge, sizeof(judge), "CONCENTRAR +10 MP"); judgeT = 50;
                        play_sfx(520.0f, 600); advanceTurn = 1;
                    }
                }
                if (advanceTurn) {
                    int allDead = 1;
                    for (int i = 0; i < numEnemies; i++) if (eHp[i] > 0) { allDead = 0; if (eHp[targetEnemy] <= 0) targetEnemy = i; }
                    if (allDead) {
                        if (isMobBattle) {
                            int xpGain = 25 * numEnemies + chapter * 20;
                            score += 150 * numEnemies;
                            inFFMenu = 0;
                            if (wasInDungeon) inDungeon = 1; else inOverworld = 1;
                            snprintf(dlgText, sizeof(dlgText), "Victoria! +%d XP", xpGain);
                            dlgTimer = 140; gain_xp(xpGain);
                        } else { inFFMenu = 0; inRelic = 1; }
                    } else {
                        turnMember++;
                        ffCursor = 0;
                        if (turnMember >= 3) {
                            turnMember = 0; inFFMenu = 0; inDefense = 1; phaseTimer = 0;
                            dodgeMember = guardActive ? 2 : ((dodgeMember + 1) % 3);
                            bardX = 240.0f; bardY = 195.0f; memset(projAlive, 0, sizeof(projAlive));
                        }
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
            int livingE = 0;
            for (int i = 0; i < numEnemies; i++) if (eHp[i] > 0) livingE++;
            int pRate = isMobBattle ? (32 - chapter * 3) : (18 - chapter * 3);
            pRate -= (livingE > 1 ? (livingE - 1) * (isMobBattle ? 2 : 3) : 0);
            int minRate = isMobBattle ? 14 : 9;
            if (pRate < minRate) pRate = minRate;
            if ((phaseTimer % pRate) == 0) {
                int burst = (!isMobBattle || (phaseTimer % (pRate * 4) == 0)) ? 2 : 1;
                for (int b = 0; b < burst; b++) {
                    for (int p = 0; p < 16; p++) if (!projAlive[p]) {
                        projAlive[p] = 1;
                        int side = xr() & 3;
                        float sx = 240.0f, sy = 132.0f;
                        if (side == 0)      { sx = (float)(142 + (xr() % 196)); sy = 132.0f; }
                        else if (side == 1) { sx = (float)(142 + (xr() % 196)); sy = 258.0f; }
                        else if (side == 2) { sx = 140.0f; sy = (float)(134 + (xr() % 122)); }
                        else                { sx = 340.0f; sy = (float)(134 + (xr() % 122)); }
                        projX[p] = sx; projY[p] = sy;
                        float tx = bardX + (float)((int)(xr() % 33) - 16);
                        float ty = bardY + (float)((int)(xr() % 33) - 16);
                        float dx = tx - sx, dy = ty - sy;
                        float len = sqrtf(dx * dx + dy * dy);
                        if (len < 1.0f) len = 1.0f;
                        float pSpd = isMobBattle ? (1.85f + chapter * 0.25f) : (2.65f + chapter * 0.40f);
                        projVx[p] = (dx / len) * pSpd;
                        projVy[p] = (dy / len) * pSpd;
                        break;
                    }
                }
            }
            for (int p = 0; p < 16; p++) if (projAlive[p]) {
                projX[p] += projVx[p]; projY[p] += projVy[p];
                if (projX[p] < 134 || projX[p] > 346 || projY[p] < 126 || projY[p] > 264) { projAlive[p] = 0; continue; }
                if (fabsf(projX[p] - bardX) < 11 && fabsf(projY[p] - bardY) < 11) {
                    projAlive[p] = 0;
                    static const int mobEdmg[3]  = { 18, 26, 36 };
                    static const int bossEdmg[3] = { 40, 56, 75 };
                    int baseEdmg = isMobBattle ? mobEdmg[chapter] : bossEdmg[chapter];
                    int edmg = relicShield ? ((baseEdmg * 6) / 10) : baseEdmg;
                    if (guardActive) edmg = (edmg + 1) / 2;
                    pHp -= edmg; play_sfx(140.0f, 1200);
                    snprintf(judge, sizeof(judge), "-%d HP!", edmg);
                    judgeT = 26;
                    if (pHp <= 0) { pHp = 0; gameOver = 1; save_best(); }
                }
            }
            if (phaseTimer >= 240) {
                inDefense = 0; inFFMenu = 1; phaseTimer = 0;
                turnMember = 0; guardActive = 0; ffCursor = 0;
                pMp = (pMp + 5 > pMaxMp) ? pMaxMp : pMp + 5;
                memset(projAlive, 0, sizeof(projAlive));
            }
        } else if (inAttackResolve) {
            /* Resolve accumulated Guitar Hero combo damage onto target or ALL enemies! */
            if (judgeT > 0) judgeT--;
            phaseTimer++;
            if (phaseTimer == 18) {
                if (pendingDmg > 0) {
                    int dmgApplied = (attackMode == 2) ? (pendingDmg * 3) / 2 : pendingDmg;
                    if (attackMode == 1) {
                        for (int i = 0; i < numEnemies; i++) if (eHp[i] > 0) {
                            eHp[i] -= dmgApplied; if (eHp[i] < 0) eHp[i] = 0;
                        }
                    } else {
                        eHp[targetEnemy] -= dmgApplied; if (eHp[targetEnemy] < 0) eHp[targetEnemy] = 0;
                    }
                    bossHitFlash = 16;
                    spawn_sparks(385.0f, 118.0f, RGB(255, 225, 80), 18);
                    play_sfx(980.0f, 1800);
                } else {
                    play_sfx(220.0f, 900);
                }
            }
            if (phaseTimer >= 92) {
                inAttackResolve = 0;
                phaseTimer = 0;
                pendingDmg = 0;
                int allDead = 1;
                for (int i = 0; i < numEnemies; i++) {
                    if (eHp[i] > 0) { allDead = 0; if (eHp[targetEnemy] <= 0) targetEnemy = i; }
                }
                if (allDead) {
                    if (isMobBattle) {
                        static const int mobXpTbl[3] = { 25, 55, 110 };
                        int xpGain = mobXpTbl[chapter] * numEnemies;
                        score += (150 + chapter * 100) * numEnemies;
                        if (wasInDungeon) inDungeon = 1; else inOverworld = 1;
                        snprintf(dlgText, sizeof(dlgText), "Grupo vencido! +%d XP", xpGain);
                        dlgTimer = 140; gain_xp(xpGain);
                    } else {
                        inRelic = 1;
                    }
                } else {
                    turnMember++;
                    ffCursor = 0;
                    if (turnMember >= 3) {
                        turnMember = 0;
                        inDefense = 1;
                        dodgeMember = guardActive ? 2 : ((dodgeMember + 1) % 3);
                        bardX = 240.0f; bardY = 195.0f;
                        memset(projAlive, 0, sizeof(projAlive));
                    } else {
                        inFFMenu = 1;
                    }
                }
            }
        } else {
            /* Guitar Hero Rhythm Attack Phase: 100% Beat-Synced to Active Song! */
            if (pressed & PSP_CTRL_LTRIGGER) { curStyle = (curStyle + 2) % 3; play_sfx(440.0f, 600); }
            if (pressed & PSP_CTRL_RTRIGGER) { curStyle = (curStyle + 1) % 3; play_sfx(440.0f, 600); }
            if (judgeT > 0) judgeT--;
            if (missFlash > 0) missFlash--;
            for (int l = 0; l < 4; l++) if (laneFlash[l] > 0) laneFlash[l]--;

            fretScroll = bgm_sub * 28.0f;

            if (judgeT > 0) judgeT--;
            phaseTimer++;
            if (phaseTimer >= 420) {
                inAttackResolve = 1;
                phaseTimer = 0;
                memset(notes, 0, sizeof(notes));
            }

            static int lastBeatStep = -1;
            int curBeat = bgm_step;
            float curSub = bgm_sub;
            int stepMod = (isMobBattle && chapter == 0) ? 2 : 1;
            if (curBeat != lastBeatStep && (curBeat % stepMod) == 0 && phaseTimer < 330) {
                /* Enforce minimum 38px vertical spacing so 2 buttons NEVER overlap/coincide! */
                int tooClose = 0;
                for (int i = 0; i < 24; i++) if (notes[i].active && notes[i].y < 142.0f) { tooClose = 1; break; }
                if (!tooClose) {
                    lastBeatStep = curBeat;
                    int sIdx = curBeat & 63;
                    float f = songLead[curSong][sIdx];
                    int mLane = (f < 360.0f) ? 0 : (f < 470.0f ? 1 : (f < 600.0f ? 2 : 3));
                    for (int i = 0; i < 24; i++) if (!notes[i].active) {
                        notes[i].active = 1;
                        notes[i].lane = mLane;
                        notes[i].spawnStep = curBeat;
                        notes[i].freq = f;
                        notes[i].y = 104.0f;
                        break;
                    }
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
                    /* Balanced chip damage across multiple combos & turns */
                    int base = (isPerf ? 4 : 2) + (pLevel - 1) + (relicDmg / 2) + (combo / 6);
                    if (curStyle == eType[targetEnemy]) base *= 2;
                    pendingDmg += base;
                    score += base * 10;
                    if (isPerf && relicVamp > 0) { pHp += relicVamp; if (pHp > pMaxHp) pHp = pMaxHp; }
                    snprintf(judge, sizeof(judge), isPerf ? "PERFECT! +%d" : "GOOD +%d", base);
                    judgeT = 30;
                    unsigned int scols[4] = { RGB(255,90,190), RGB(90,255,140), RGB(255,80,80), RGB(80,225,255) };
                    spawn_sparks((float)(164 + l * 50), (float)HIT_Y, isPerf ? RGB(255, 240, 90) : scols[l], isPerf ? 10 : 6);
                    play_sfx(notes[hit].freq > 50.0f ? notes[hit].freq : songScale[curSong][l], 2600);
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
                /* Phase-locked to audio clock: lands on HIT_Y (238) exactly 9 beats after spawnStep */
                float elapsedBeats = (float)(curBeat - notes[i].spawnStep) + curSub;
                notes[i].y = 104.0f + elapsedBeats * (134.0f / 9.0f);
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
            /* Render 16x10 native 30x30 smooth-filtered 32-bit tiles (25% closer, zero jagged zoom!) */
            int ox = mapX - 8, oy = mapY - 4;
            int waterOff = (animFrame / 8) % 24;
            for (int ty = 0; ty < 10; ty++) {
                int sy = ty * 30 - 4;
                for (int tx = 0; tx < 16; tx++) {
                    int sx = tx * 30;
                    int wx = ox + tx, wy = oy + ty;
                    if (wx < 0 || wy < 0 || wx >= MW || wy >= MH) {
                        draw_tile24_30(fb, tile_grass, sx, sy);
                        draw_spr24_30(fb, spr_mountain, sx, sy);
                    } else {
                        unsigned char t = worldMap[wy][wx];
                        if (t == 3) {
                            draw_tile24_30_scroll(fb, tile_water, sx, sy, waterOff);
                            if (wx > 0 && worldMap[wy][wx - 1] != 3 && worldMap[wy][wx - 1] != 6)
                                fill_rect_alpha(fb, sx, sy, 4, 30, RGB(205, 185, 130), 175);
                            if (wx < MW - 1 && worldMap[wy][wx + 1] != 3 && worldMap[wy][wx + 1] != 6)
                                fill_rect_alpha(fb, sx + 26, sy, 4, 30, RGB(205, 185, 130), 175);
                            draw_tile_vegetation(fb, wx, wy, sx + 3, sy + 3, 1);
                        } else if (t == 4 || t >= 10) {
                            draw_tile24_30(fb, tile_castle, sx, sy);
                            /* Rich Town Floor Details: Golden Plaza Carpets, Flowerbeds & Festive Banners */
                            if (t == 4) {
                                unsigned int th = ((unsigned int)wx * 2654435761u) ^ ((unsigned int)wy * 2246822519u);
                                if (wx == 28 || wy == 84 || wx == 81 || wy == 60 || wx == 155 || wy == 112) {
                                    fill_rect_alpha(fb, sx + 2, sy + 2, 26, 26, RGB(195, 145, 55), 55);
                                    draw_border(fb, sx + 3, sy + 3, 24, 24, RGB(230, 190, 95));
                                } else if ((th % 9u) == 0u) {
                                    /* Town Flowerbed Planter */
                                    fill_rect(fb, sx + 5, sy + 17, 20, 8, RGB(120, 75, 42));
                                    fill_rect(fb, sx + 7, sy + 14, 16, 5, RGB(34, 130, 48));
                                    fill_rect(fb, sx + 9, sy + 14, 3, 3, RGB(255, 110, 145));
                                    fill_rect(fb, sx + 18, sy + 14, 3, 3, RGB(255, 225, 70));
                                } else if ((th % 13u) == 0u) {
                                    /* Wooden Crate / Barrel Prop */
                                    fill_rect(fb, sx + 8, sy + 12, 12, 12, RGB(145, 92, 46));
                                    draw_border(fb, sx + 8, sy + 12, 12, 12, RGB(78, 45, 22));
                                    fill_rect(fb, sx + 8, sy + 16, 12, 2, RGB(95, 95, 110));
                                }
                            }
                            if (t == 12) {
                                draw_spr24_30(fb, spr_fountain, sx, sy);
                                int sp1 = (animFrame / 3) % 8;
                                fill_rect_alpha(fb, sx + 12, sy + 7 - sp1, 2, 3, RGB(165, 235, 255), 210);
                                fill_rect_alpha(fb, sx + 16, sy + 9 - ((sp1 + 4) % 8), 2, 3, RGB(165, 235, 255), 210);
                            }
                            else if (t == 13) draw_spr24_30(fb, spr_market, sx, sy);
                            else if (t == 14) {
                                int lGlow = 50 + (int)(sinf((float)animFrame * 0.14f) * 18.0f);
                                fill_ellipse_alpha(fb, sx + 15, sy + 11, 30, 22, RGB(255, 175, 55), lGlow / 2);
                                fill_ellipse_alpha(fb, sx + 15, sy + 9, 15, 12, RGB(255, 215, 95), lGlow);
                                draw_spr24_30(fb, spr_lamp, sx, sy);
                            } else if (t == 15) draw_spr24_30(fb, spr_well, sx, sy);
                        } else if (t == 6) {
                            draw_tile24_30(fb, tile_bridge, sx, sy);
                        } else if (t == 7) {
                            draw_tile24_30(fb, tile_path, sx, sy);
                        } else if (t == 8) {
                            draw_tile24_30(fb, tile_wall, sx, sy);
                        } else {
                            draw_tile24_30(fb, tile_grass, sx, sy);
                            if (t == 0) draw_tile_vegetation(fb, wx, wy, sx + 3, sy + 3, 0);
                            if (t == 2) draw_spr24_30(fb, spr_mountain, sx, sy);
                            else if (t == 5) {
                                int dIdx = -1;
                                for (int k = 0; k < 3; k++) if (dngX[k] == wx && dngY[k] == wy) dIdx = k;
                                if (dIdx >= 0 && bossDefeated[dIdx]) draw_spr24_30(fb, spr_chest, sx, sy);
                                else {
                                    int pulse = 55 + (int)(sinf((float)animFrame * 0.10f) * 35.0f);
                                    fill_ellipse_alpha(fb, sx + 15, sy + 16, 16, 13, RGB(170, 60, 255), pulse);
                                    draw_spr24_30(fb, spr_dungeon, sx, sy);
                                }
                            } else if (t == 9) {
                                int sIdx = (wx == shrineX[1] && wy == shrineY[1]) ? 1 : 0;
                                if (!shrineClaimed[sIdx]) {
                                    int pulse = 60 + (int)(sinf((float)animFrame * 0.12f) * 35.0f);
                                    fill_ellipse_alpha(fb, sx + 15, sy + 15, 15, 12, RGB(70, 225, 255), pulse);
                                }
                                draw_spr24_30(fb, spr_shrine, sx, sy);
                            }
                        }
                    }
                }
            }
            /* 2.5D Scale2x Overhanging Trees (40x40) & Large 96x96 Town Buildings with soft shadows */
            for (int ty = 0; ty < 13; ty++) {
                int sy = ty * 30 - 4;
                for (int tx = -2; tx < 18; tx++) {
                    int sx = tx * 30;
                    int wx = ox + tx, wy = oy + ty;
                    if (wx >= 0 && wy >= 0 && wx < MW && wy < MH) {
                        unsigned char wt = worldMap[wy][wx];
                        if (wt == 1) {
                            fill_ellipse_alpha(fb, sx + 15, sy + 25, 14, 6, RGB(0, 0, 0), 115);
                            draw_spr32_40_scale2x(fb, spr_tree32, sx - 5, sy - 10);
                        } else if (wt == 10 || wt == 11) {
                            fill_ellipse_alpha(fb, sx + 15, sy + 27, 46, 11, RGB(0, 0, 0), 135);
                            draw_spr32_96_scale2x(fb, (wt == 10) ? spr_house : spr_tavern, sx - 33, sy - 66);
                        }
                    }
                }
            }
            /* Visible Roaming Enemies across the map (2x2 Supersampled 64->30) */
            for (int i = 0; i < MAX_ROAM; i++) if (roam[i].alive) {
                int sx = (roam[i].x - ox) * 30, sy = (roam[i].y - oy) * 30 - 4;
                if (sx >= -30 && sx < SCR_W && sy >= -30 && sy < SCR_H) {
                    int mBob = ((animFrame / 14 + i) & 1);
                    fill_ellipse_alpha(fb, sx + 15, sy + 26, 11, 5, RGB(190, 30, 45), 115);
                    const unsigned int *mspr = (roam[i].type == 0) ? mob_slime : (roam[i].type == 1 ? mob_goblin : mob_wraith);
                    draw_spr64_to_30(fb, mspr, sx, sy - mBob, roam[i].flip);
                }
            }
            /* Wandering Town Villagers bringing cities to life */
            for (int i = 0; i < MAX_TOWN_NPC; i++) {
                int sx = (townNpcs[i].x - ox) * 30, sy = (townNpcs[i].y - oy) * 30 - 4;
                if (sx >= -30 && sx < SCR_W && sy >= -30 && sy < SCR_H) {
                    int bob = ((animFrame / 16 + i) & 1);
                    fill_ellipse_alpha(fb, sx + 15, sy + 26, 10, 4, RGB(0, 0, 0), 115);
                    const unsigned int *tspr = (townNpcs[i].kind == 0) ? spr_troubadour : (townNpcs[i].kind == 1 ? spr_elder : spr_band1);
                    draw_spr24_30_dir(fb, tspr, sx, sy - bob, townNpcs[i].flip);
                    if (((animFrame / 90 + i) % 5) == 0) {
                        draw_text(fb, sx + 11, sy - 9 - bob, "*", RGB(255, 230, 120));
                    }
                }
            }
            /* 8 NPCs across the 6 regions dynamically turning to look towards your Bard */
            for (int i = 0; i < 8; i++) {
                int sx = (npcX[i] - ox) * 30, sy = (npcY[i] - oy) * 30 - 4;
                if (sx >= -30 && sx < SCR_W && sy >= -30 && sy < SCR_H) {
                    int npcFlip = (mapX < npcX[i]) ? 1 : 0;
                    int bob = ((animFrame / 20 + i) & 1);
                    fill_ellipse_alpha(fb, sx + 15, sy + 26, 10, 4, RGB(0, 0, 0), 120);
                    draw_spr24_30_dir(fb, (i & 1) ? spr_troubadour : spr_elder, sx, sy - bob, npcFlip);
                    draw_text(fb, sx + 12, sy - 9 - bob, "!", RGB(255, 235, 60));
                }
            }
            /* Your 3-Piece Band following in formation with Scale2x crisp 30x30 sprites */
            int bBob = ((animFrame / 16) & 1);
            fill_ellipse_alpha(fb, (prevX2 - ox) * 30 + 15, (prevY2 - oy) * 30 + 22, 10, 4, RGB(0, 0, 0), 120);
            fill_ellipse_alpha(fb, (prevX1 - ox) * 30 + 15, (prevY1 - oy) * 30 + 22, 10, 4, RGB(0, 0, 0), 120);
            fill_ellipse_alpha(fb, 8 * 30 + 15, 4 * 30 + 22, 11, 5, RGB(0, 0, 0), 135);
            draw_spr24_30_dir(fb, spr_band2, (prevX2 - ox) * 30, (prevY2 - oy) * 30 - 4 - bBob, band2Flip);
            draw_spr24_30_dir(fb, spr_band1, (prevX1 - ox) * 30, (prevY1 - oy) * 30 - 4 - (bBob ^ 1), band1Flip);
            draw_spr24_30_dir(fb, spr_bard,  8 * 30, 4 * 30 - 4 - bBob, bardFlip);

            /* Living Insects (Fireflies/Butterflies/Dragonflies) + Dynamic Weather (Rain/Fog/God-Rays) & Lighting */
            update_draw_bugs(fb, (mapX >= 118 && mapX < 165), (weatherMode >= 2));
            update_draw_weather_and_lighting(fb, mapX, mapY);

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

            const char *reg = regionNames[curRegId];
            sprintf(buf, "%s | BOSS:%d/3 | %d,%d", reg, bossDefeated[0]+bossDefeated[1]+bossDefeated[2], mapX, mapY);
            draw_text(fb, 232, 10, buf, RGB(255, 255, 255));

            /* Ornamental RPG Zone Arrival Banner */
            if (regionBannerTimer > 0) {
                int slide = (regionBannerTimer > 153) ? (regionBannerTimer - 153) * 3 : (regionBannerTimer < 12 ? (12 - regionBannerTimer) * 3 : 0);
                int by = 32 - slide;
                int bAlpha = (regionBannerTimer < 12) ? (regionBannerTimer * 18) : 215;
                fill_rect_alpha(fb, 110, by, 260, 38, RGB(12, 16, 44), bAlpha);
                draw_border(fb, 110, by, 260, 38, RGB(245, 205, 85));
                draw_border(fb, 112, by + 2, 256, 34, RGB(110, 145, 225));
                fill_rect(fb, 114, by + 4, 4, 4, RGB(255, 225, 95));
                fill_rect(fb, 362, by + 4, 4, 4, RGB(255, 225, 95));
                fill_rect(fb, 114, by + 30, 4, 4, RGB(255, 225, 95));
                fill_rect(fb, 362, by + 30, 4, 4, RGB(255, 225, 95));
                const char *tag = (curRegId <= 2) ? "~ CIUDAD ~" : "~ TERRITORIO ~";
                int tLen = (int)strlen(tag) * 6;
                int nLen = (int)strlen(regionNames[curRegId]) * 6;
                int sLen = (int)strlen(regionSubs[curRegId]) * 6;
                draw_text(fb, 240 - tLen / 2, by + 5, tag, RGB(255, 215, 80));
                draw_text(fb, 240 - nLen / 2, by + 15, regionNames[curRegId], RGB(255, 250, 220));
                draw_text(fb, 240 - sLen / 2, by + 26, regionSubs[curRegId], RGB(130, 225, 255));
            }

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
            for (int i = 0; i < MAX_ROAM; i++) if (roam[i].alive) {
                fill_rect(fb, 408 + roam[i].x / 4, 32 + roam[i].y / 4, 2, 2, RGB(255, 65, 75));
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
        } else if (inDungeon) {
            /* Diablo-Style Procedural Dungeon Rendering (Native 30x30 smooth-filtered tiles) */
            int ox = dngPx - 8, oy = dngPy - 4;
            for (int ty = 0; ty < 10; ty++) {
                int sy = ty * 30 - 4;
                for (int tx = 0; tx < 16; tx++) {
                    int sx = tx * 30;
                    int wx = ox + tx, wy = oy + ty;
                    if (wx < 0 || wy < 0 || wx >= DW || wy >= DH || !dngSeen[wy][wx]) {
                        fill_rect(fb, sx, sy, 30, 30, RGB(8, 7, 14));
                    } else {
                        unsigned char dt = dngMap[wy][wx];
                        if (dt == 0) {
                            draw_tile24_30(fb, tile_wall, sx, sy);
                            fill_rect_alpha(fb, sx, sy, 30, 30, RGB(14, 10, 22), 105);
                        } else {
                            draw_tile24_30(fb, tile_castle, sx, sy);
                            fill_rect_alpha(fb, sx, sy, 30, 30, (curDngId == 0) ? RGB(28, 16, 14) : (curDngId == 1 ? RGB(14, 24, 42) : RGB(30, 10, 28)), 95);
                            if (dt == 2) {
                                int pulse = 65 + (int)(sinf((float)animFrame * 0.12f) * 35.0f);
                                fill_ellipse_alpha(fb, sx + 15, sy + 15, 16, 13, (dngFloor < dngMaxFloors) ? RGB(60, 225, 255) : RGB(215, 50, 255), pulse);
                                draw_spr24_30(fb, (dngFloor < dngMaxFloors) ? spr_stair_down : spr_boss_altar, sx, sy);
                            } else if (dt == 3) {
                                draw_spr24_30(fb, spr_chest_32bit, sx, sy);
                            } else if (dt == 4) {
                                fill_ellipse_alpha(fb, sx + 15, sy + 15, 13, 11, RGB(80, 235, 130), 75);
                                draw_text(fb, sx + 12, sy + 11, "^", RGB(140, 255, 170));
                            }
                        }
                        int dist2 = (wx - dngPx) * (wx - dngPx) + (wy - dngPy) * (wy - dngPy);
                        if (dist2 > 18) fill_rect_alpha(fb, sx, sy, 30, 30, RGB(6, 5, 12), 145);
                    }
                }
            }
            /* Visible Patrolling Dungeon Monsters inside Torch Radius */
            for (int m = 0; m < 5; m++) if (dngMobs[m].alive) {
                int dist2 = (dngMobs[m].x - dngPx) * (dngMobs[m].x - dngPx) + (dngMobs[m].y - dngPy) * (dngMobs[m].y - dngPy);
                if (dist2 <= 26) {
                    int sx = (dngMobs[m].x - ox) * 30, sy = (dngMobs[m].y - oy) * 30 - 4;
                    if (sx >= -30 && sx < SCR_W && sy >= -30 && sy < SCR_H) {
                        int mBob = ((animFrame / 14 + m) & 1);
                        fill_ellipse_alpha(fb, sx + 15, sy + 26, 11, 5, RGB(210, 35, 50), 120);
                        const unsigned int *mspr = (dngMobs[m].type == 0) ? mob_slime : (dngMobs[m].type == 1 ? mob_goblin : mob_wraith);
                        draw_spr64_to_30(fb, mspr, sx, sy - mBob, dngMobs[m].flip);
                    }
                }
            }
            /* Band in Dungeon */
            int dBob = ((animFrame / 16) & 1);
            fill_ellipse_alpha(fb, 8 * 30 + 15, 4 * 30 + 22, 11, 5, RGB(0, 0, 0), 145);
            draw_spr24_30_dir(fb, spr_band2, (dngP2x - ox) * 30, (dngP2y - oy) * 30 - 4 - dBob, band2Flip);
            draw_spr24_30_dir(fb, spr_band1, (dngP1x - ox) * 30, (dngP1y - oy) * 30 - 4 - (dBob ^ 1), band1Flip);
            draw_spr24_30_dir(fb, spr_bard,  8 * 30, 4 * 30 - 4 - dBob, bardFlip);

            /* Top Dungeon HUD */
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
            sprintf(buf, "MAZMORRA %d | PISO %d/%d | %s", curDngId + 1, dngFloor, dngMaxFloors, (dngFloor == dngMaxFloors) ? "ALTAR BOSS!" : "ESCALERA");
            draw_text(fb, 176, 10, buf, RGB(255, 225, 120));

            /* Diablo-Style Explored Dungeon Mini-Map (44x34) */
            draw_ff_window(fb, 422, 28, 52, 42);
            for (int my = 0; my < DH; my++) {
                for (int mx = 0; mx < DW; mx++) {
                    if (!dngSeen[my][mx]) continue;
                    unsigned char dm = dngMap[my][mx];
                    unsigned int mc = (dm == 0) ? RGB(52, 46, 72) : RGB(130, 115, 155);
                    if (dm == 2) mc = (dngFloor < dngMaxFloors) ? RGB(60, 225, 255) : RGB(225, 65, 255);
                    else if (dm == 3) mc = RGB(255, 215, 60);
                    else if (dm == 4) mc = RGB(90, 240, 130);
                    put_px(fb, 426 + mx, 32 + my, mc);
                }
            }
            if ((animFrame / 15) & 1) fill_rect(fb, 426 + dngPx - 1, 32 + dngPy - 1, 3, 3, RGB(255, 255, 255));

            if (dlgTimer > 0) {
                draw_ff_window(fb, 10, 228, 460, 38);
                fill_rect(fb, 16, 235, 24, 24, RGB(10, 14, 42));
                draw_spr24(fb, spr_bard, 16, 235);
                draw_border(fb, 16, 235, 24, 24, RGB(255, 215, 80));
                draw_text(fb, 48, 236, "EXPLORACION DE MAZMORRA:", RGB(255, 225, 90));
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
                const unsigned int * const partyAvatars[3] = { spr_bard, spr_band1, spr_band2 };
                const char * const partyNames[3] = { "KAEL", "LYRA", "THORIN" };
                int activeAvatar = inDefense ? dodgeMember : turnMember;
                /* Battle Scene: Top Status Windows + 32-bit High-Res Band (48x48) & Boss (64x64) */
                draw_ff_window(fb, 8, 6, 206, 42);
                fill_rect(fb, 14, 12, 26, 28, RGB(14, 18, 48));
                draw_spr24(fb, partyAvatars[activeAvatar], 15, 14);
                draw_border(fb, 14, 12, 26, 28, RGB(240, 200, 80));
                sprintf(buf, "%s (BANDA) NV.%d", partyNames[activeAvatar], pLevel);
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

                /* Top-Right Target Enemy Status Window */
                draw_ff_window(fb, 266, 6, 206, 42);
                sprintf(buf, "%s [%d/%d]", eIsBoss[targetEnemy] ? bossNames[chapter] : mobNames[chapter], targetEnemy + 1, numEnemies);
                draw_text(fb, 276, 11, buf, RGB(255, 255, 255));
                draw_text(fb, 276, 23, "HP", RGB(255, 130, 140));
                fill_rect(fb, 294, 23, 104, 7, RGB(45, 14, 20));
                fill_rect(fb, 294, 23, eMaxHp[targetEnemy] > 0 ? (eHp[targetEnemy] * 104) / eMaxHp[targetEnemy] : 0, 7, RGB(240, 50, 70));
                draw_border(fb, 294, 23, 104, 7, RGB(255, 230, 235));
                sprintf(buf, "%d/%d", eHp[targetEnemy], eMaxHp[targetEnemy]);
                draw_text(fb, 402, 23, buf, RGB(255, 255, 255));
                sprintf(buf, "DEBIL:%s(x2) </>OBJ", styleNames[eType[targetEnemy]]);
                draw_text(fb, 276, 35, buf, RGB(255, 215, 80));

                /* Soft Alpha Shadows + Glowing Runic Pads + Idle Breathing Animation on Band! */
                int bIdle0 = (int)(sinf((float)animFrame * 0.08f) * 2.0f);
                int bIdle1 = (int)(sinf((float)animFrame * 0.08f + 1.5f) * 2.0f);
                int bIdle2 = (int)(sinf((float)animFrame * 0.08f + 3.0f) * 2.0f);

                fill_ellipse_alpha(fb, 42,  144, 21, 6, RGB(0, 0, 0), 130);
                fill_ellipse_alpha(fb, 122, 148, 21, 6, RGB(0, 0, 0), 130);
                fill_ellipse_alpha(fb, 82,  158, 21, 6, RGB(0, 0, 0), 130);
                fill_ellipse_alpha(fb, 42,  144, 22, 6, (turnMember == 2) ? RGB(255, 220, 70) : RGB(175, 105, 255), (turnMember == 2) ? 110 : 60);
                fill_ellipse_alpha(fb, 122, 148, 22, 6, (turnMember == 0) ? RGB(255, 220, 70) : RGB(65, 225, 255),  (turnMember == 0) ? 110 : 65);
                fill_ellipse_alpha(fb, 82,  158, 22, 6, (turnMember == 1) ? RGB(255, 220, 70) : RGB(80, 235, 145),  (turnMember == 1) ? 110 : 60);
                draw_ellipse_ring(fb, 42,  144, 22, 6, (turnMember == 2) ? RGB(255, 235, 90) : RGB(175, 105, 255));
                draw_ellipse_ring(fb, 122, 148, 22, 6, (turnMember == 0) ? RGB(255, 235, 90) : RGB(65, 225, 255));
                draw_ellipse_ring(fb, 82,  158, 22, 6, (turnMember == 1) ? RGB(255, 235, 90) : RGB(80, 235, 145));

                draw_spr48(fb, spr_band2_48, 18, 100 + bIdle2);
                draw_spr48(fb, spr_bard_48,  98, 104 + bIdle0);
                draw_spr48(fb, spr_band1_48, 58, 114 + bIdle1);
                if (inFFMenu || (!inDefense && !inAttackResolve)) {
                    int px[3] = { 119, 79, 39 };
                    int py[3] = { 92 + bIdle0, 102 + bIdle1, 88 + bIdle2 };
                    draw_text(fb, px[turnMember], py[turnMember] + ((animFrame & 8) ? 2 : 0), "v", RGB(255, 235, 80));
                }
                if (guardActive) {
                    draw_ellipse_ring(fb, 82, 150, 56, 14, RGB(90, 210, 255));
                }

                /* Multi-Enemy Staggered Formation (Final Fantasy Style) on Right */
                int bossBob = (int)(sinf((float)animFrame * 0.06f) * 3.0f);
                int exPos[3] = { 356, 288, 408 };
                int eyPos[3] = { 82,  96,  100 };
                for (int ei = 0; ei < numEnemies; ei++) {
                    if (eHp[ei] <= 0) continue;
                    int isTgt = (ei == targetEnemy);
                    int cx = exPos[ei] + 32;
                    int cy = eyPos[ei] + 62;
                    fill_ellipse_alpha(fb, cx, cy, 32, 8, RGB(0, 0, 0), 145);
                    fill_ellipse_alpha(fb, cx, cy, 34, 9, isTgt ? RGB(255, 215, 65) : RGB(255, 55, 80), isTgt ? 95 : 55);
                    draw_ellipse_ring(fb, cx, cy, 34, 9, isTgt ? RGB(255, 235, 90) : RGB(255, 65, 85));
                    const unsigned int *espr;
                    if (!eIsBoss[ei]) espr = (chapter == 0) ? mob_slime : (chapter == 1 ? mob_goblin : mob_wraith);
                    else              espr = (chapter == 0) ? boss_golem : (chapter == 1 ? boss_automaton : boss_dragon);
                    int hitThis = (attackMode == 1 || ei == targetEnemy);
                    int shakeX = (inAttackResolve && hitThis && phaseTimer >= 18 && phaseTimer < 38) ? ((phaseTimer & 2) ? 6 : -6) : 0;
                    draw_boss64(fb, espr, exPos[ei] + shakeX, eyPos[ei] + bossBob + (ei & 1 ? 2 : 0), bossHitFlash > 0 && hitThis);
                    /* Individual Enemy HP Bar */
                    fill_rect(fb, exPos[ei] + 8, eyPos[ei] + 64, 48, 5, RGB(35, 10, 18));
                    fill_rect(fb, exPos[ei] + 8, eyPos[ei] + 64, eMaxHp[ei] > 0 ? (eHp[ei] * 48) / eMaxHp[ei] : 0, 5, RGB(240, 55, 75));
                    draw_border(fb, exPos[ei] + 8, eyPos[ei] + 64, 48, 5, isTgt ? RGB(255, 235, 90) : RGB(200, 180, 190));
                    if (isTgt) {
                        draw_text(fb, exPos[ei] + 29, eyPos[ei] - 8 + ((animFrame & 8) ? 2 : 0), "v", RGB(255, 235, 80));
                    }
                }
                if (judgeT > 0 && inFFMenu) {
                    fill_rect(fb, 150, 54, 180, 16, RGB(18, 24, 58));
                    draw_border(fb, 150, 54, 180, 16, RGB(255, 225, 85));
                    draw_text(fb, 158, 58, judge, RGB(255, 240, 100));
                }

                if (inFFMenu) {
                    /* Classic Final Fantasy Per-Character Turn Menu + Partitura Preview */
                    draw_ff_window(fb, 8, 170, 224, 96);
                    fill_rect(fb, 12, 174, 216, 12, RGB(38, 52, 128));
                    const char *turnHeaders[3] = {
                        "TURNO 1/3: KAEL (GUITARRA)",
                        "TURNO 2/3: LYRA (LAUD)",
                        "TURNO 3/3: THORIN (BAJO)"
                    };
                    draw_text(fb, 16, 176, turnHeaders[turnMember], RGB(255, 220, 80));

                    fill_rect(fb, 14, 189 + ffCursor * 18, 212, 15, RGB(65, 85, 185));
                    draw_border(fb, 14, 189 + ffCursor * 18, 212, 15, RGB(255, 215, 85));

                    const char *optsByMember[3][4] = {
                        { "SOLO GUITARRA (Objetivo)", "CAMBIAR ESTILO MUSICAL", "ROMPE-ARMADURA (1.5x)", "HUIR DEL COMBATE" },
                        { "ARPEGIO GLOBAL (Todos)",   "HIMNO CURATIVO (+45HP)", "CANTO DE MANA (+20MP)",  "ESCUDO ARMONICO" },
                        { "BAJO SISMICO (Objetivo)",  "GUARDIA FERREA (-50%)",  "GOLPE ESCUDO (Todos)",   "CONCENTRACION (+10MP)" }
                    };
                    const char *costsByMember[3][4] = {
                        { "ATK", "EST", "12MP", "ESC" },
                        { "10MP", "15MP", "+MP", "8MP" },
                        { "ATK", "DEF", "AOE", "+MP" }
                    };
                    for (int i = 0; i < 4; i++) {
                        int ry = 193 + i * 18;
                        draw_text(fb, 20, ry, optsByMember[turnMember][i], ffCursor == i ? RGB(255, 240, 90) : RGB(255, 255, 255));
                        draw_text(fb, 194, ry, costsByMember[turnMember][i], RGB(130, 235, 215));
                    }

                    /* Right Tactical & Partitura Window with Real 20x20 PSP Button Sprites */
                    draw_ff_window(fb, 238, 170, 234, 96);
                    fill_rect(fb, 242, 174, 226, 12, RGB(38, 52, 128));
                    draw_text(fb, 248, 176, "TACTICA Y PARTITURA (</>:OBJ)", RGB(100, 235, 255));

                    int tgtWeak = eType[targetEnemy];
                    fill_rect(fb, 246, 190, 218, 16, curStyle == tgtWeak ? RGB(18, 52, 36) : RGB(44, 34, 18));
                    draw_border(fb, 246, 190, 218, 16, curStyle == tgtWeak ? RGB(80, 235, 135) : RGB(240, 190, 75));
                    sprintf(buf, curStyle == tgtWeak ? "ESTILO:%s -> CRIT x2!" : "ESTILO:%s (DEBIL:%s)", styleNames[curStyle], styleNames[tgtWeak]);
                    draw_text(fb, 252, 194, buf, curStyle == tgtWeak ? RGB(120, 255, 150) : RGB(255, 225, 95));

                    draw_text(fb, 246, 211, guardActive ? "GUARDIA ACTIVA (-50% DMG)" : "SECUENCIA RITMICA:", guardActive ? RGB(110, 245, 255) : RGB(180, 225, 255));
                    for (int n = 0; n < 6; n++) {
                        int nl = (chapter + n * 3) & 3;
                        int nx = 248 + n * 36;
                        fill_rect(fb, nx, 221, 26, 22, RGB(18, 22, 54));
                        draw_border(fb, nx, 221, 26, 22, RGB(130, 150, 220));
                        draw_spr20(fb, btns20_arr[nl], nx + 3, 222);
                    }
                    sprintf(buf, "RELIQUIAS:+%dDMG +%dVAMP %s", relicDmg, relicVamp, relicShield ? "ESC" : "-");
                    draw_text(fb, 246, 248, buf, RGB(255, 215, 85));
                } else if (inDefense) {
                    /* Enemy Turn: 32-Bit Runic Dodge Arena with Active Party Member Avatar */
                    draw_ff_window(fb, 132, 110, 216, 156);
                    fill_rect(fb, 136, 114, 208, 13, RGB(88, 22, 42));
                    sprintf(buf, "ESQUIVA: %s!", partyNames[dodgeMember]);
                    draw_text(fb, 140, 117, buf, RGB(255, 235, 80));
                    fill_rect(fb, 266, 117, 72, 7, RGB(25, 10, 15));
                    fill_rect(fb, 266, 117, ((240 - phaseTimer) * 72) / 240, 7, RGB(255, 185, 50));
                    draw_border(fb, 266, 117, 72, 7, RGB(255, 255, 255));

                    /* Inner Runic Grid Arena (flashes crimson when hit!) */
                    fill_rect(fb, 138, 130, 204, 130, (judgeT > 18) ? RGB(72, 14, 24) : RGB(14, 11, 28));
                    for (int gx = 158; gx < 340; gx += 22) fill_rect(fb, gx, 131, 1, 128, RGB(34, 28, 64));
                    for (int gy = 148; gy < 258; gy += 18) fill_rect(fb, 139, gy, 202, 1, RGB(34, 28, 64));
                    draw_border(fb, 138, 130, 204, 130, (judgeT > 18) ? RGB(255, 80, 90) : RGB(140, 110, 225));

                    /* Glowing Cyan Aegis Shield Ring around Active Character Avatar */
                    draw_ellipse_ring(fb, (int)bardX, (int)bardY, 15, 14, (judgeT > 16) ? RGB(255, 80, 90) : (guardActive ? RGB(255, 225, 85) : RGB(70, 235, 255)));
                    draw_spr24(fb, partyAvatars[dodgeMember], (int)bardX - 12, (int)bardY - 12);
                    if (judgeT > 0) draw_text(fb, (int)bardX - 18, (int)bardY - 24 - (26 - judgeT) / 2, judge, RGB(255, 90, 90));

                    /* 32-Bit Layered Fiery Shadow-Flame Orbs with Comet Trails */
                    for (int p = 0; p < 16; p++) if (projAlive[p]) {
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
