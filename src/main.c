#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("AsteroideX", 0, 1, 0);
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
static unsigned int rng = 0x778899AAu;
static unsigned int xr(void) { unsigned int x = rng; x ^= x<<13; x ^= x>>17; x ^= x<<5; rng = x; return x; }
static int exit_cb(int a, int b, void *c) { (void)a; (void)b; (void)c; running = 0; return 0; }
static int cb_th(SceSize a, void *p) { (void)a; (void)p; sceKernelRegisterExitCallback(sceKernelCreateCallback("Exit", exit_cb, NULL)); sceKernelSleepThreadCB(); return 0; }

static void put_px(int x, int y, unsigned int c) { if ((unsigned)x < SCR_W && (unsigned)y < SCR_H) fb0[y * BUF_W + x] = c; }
static void fill_rect(int x, int y, int w, int h, unsigned int c) {
    if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x; if (y + h > SCR_H) h = SCR_H - y;
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) fb0[(y + j) * BUF_W + (x + i)] = c;
}
static void draw_line(int x0, int y0, int x1, int y1, unsigned int c) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    for (;;) {
        put_px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
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
    const float bass[4] = { 55.00f, 65.41f, 73.42f, 65.41f };
    while (arun) {
        short *o = abuf[cur];
        for (int i = 0; i < 1024; i++) {
            float m = 0.0f;
            if (sfx_on) {
                int st = (int)(bgm_t * 3.5f);
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

typedef struct { float x, y, vx, vy; int life, alive; } Bullet;
typedef struct { float x, y, vx, vy, r; int size, alive; } Rock;

static Bullet bullets[24];
static Rock rocks[32];
static float px = 240, py = 136, pvx = 0, pvy = 0, pang = -1.57f;
static int lives = 3, wave = 1, score = 0, best = 0, fireCd = 0, iframes = 0;
static int inTitle = 1, gameOver = 0;

static void load_best(void) { FILE *f = fopen("ax_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("ax_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static void spawn_rock(float x, float y, int size) {
    for (int i = 0; i < 32; i++) if (!rocks[i].alive) {
        rocks[i].alive = 1; rocks[i].x = x; rocks[i].y = y;
        rocks[i].size = size;
        rocks[i].r = (size == 3) ? 20.0f : (size == 2 ? 12.0f : 7.0f);
        float a = ((float)(xr() % 360)) * 0.01745f;
        float s = (4 - size) * 0.65f + 0.4f;
        rocks[i].vx = cosf(a) * s; rocks[i].vy = sinf(a) * s;
        break;
    }
}

static void spawn_wave(int w) {
    wave = w;
    int cnt = 3 + w;
    if (cnt > 10) cnt = 10;
    for (int i = 0; i < cnt; i++) {
        float x = (xr() & 1) ? 30.0f : SCR_W - 30.0f;
        float y = (float)(xr() % SCR_H);
        spawn_rock(x, y, 3);
    }
}

static void start_game(void) {
    px = 240; py = 136; pvx = 0; pvy = 0; pang = -1.57f;
    lives = 3; score = 0; fireCd = 0; iframes = 60;
    memset(bullets, 0, sizeof(bullets));
    memset(rocks, 0, sizeof(rocks));
    spawn_wave(1);
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
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("ax_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

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
            pang += ax * 0.085f;

            if (pad.Buttons & (PSP_CTRL_UP | PSP_CTRL_SQUARE)) {
                pvx += cosf(pang) * 0.14f;
                pvy += sinf(pang) * 0.14f;
            }
            if (pressed & PSP_CTRL_TRIANGLE) {
                px = (float)(xr() % SCR_W); py = (float)(xr() % SCR_H);
                pvx = 0; pvy = 0; iframes = 30; play_sfx(880.0f, 1500);
            }
            pvx *= 0.99f; pvy *= 0.99f;
            px += pvx; py += pvy;
            if (px < 0) px += SCR_W; if (px >= SCR_W) px -= SCR_W;
            if (py < 0) py += SCR_H; if (py >= SCR_H) py -= SCR_H;
            if (iframes > 0) iframes--;

            if (fireCd > 0) fireCd--;
            if ((pad.Buttons & PSP_CTRL_CROSS) && fireCd <= 0) {
                fireCd = 10;
                for (int i = 0; i < 24; i++) if (!bullets[i].alive) {
                    bullets[i].alive = 1; bullets[i].life = 55;
                    bullets[i].x = px + cosf(pang) * 10; bullets[i].y = py + sinf(pang) * 10;
                    bullets[i].vx = cosf(pang) * 6.5f; bullets[i].vy = sinf(pang) * 6.5f;
                    break;
                }
                play_sfx(720.0f, 800);
            }

            for (int i = 0; i < 24; i++) if (bullets[i].alive) {
                bullets[i].x += bullets[i].vx; bullets[i].y += bullets[i].vy;
                if (bullets[i].x < 0) bullets[i].x += SCR_W; if (bullets[i].x >= SCR_W) bullets[i].x -= SCR_W;
                if (bullets[i].y < 0) bullets[i].y += SCR_H; if (bullets[i].y >= SCR_H) bullets[i].y -= SCR_H;
                if (--bullets[i].life <= 0) { bullets[i].alive = 0; continue; }
                for (int j = 0; j < 32; j++) if (rocks[j].alive) {
                    float dx = bullets[i].x - rocks[j].x, dy = bullets[i].y - rocks[j].y;
                    if (dx * dx + dy * dy < rocks[j].r * rocks[j].r) {
                        bullets[i].alive = 0;
                        int sz = rocks[j].size;
                        float rx = rocks[j].x, ry = rocks[j].y;
                        rocks[j].alive = 0;
                        score += (4 - sz) * 50;
                        play_sfx(200.0f, 1500);
                        if (sz > 1) { spawn_rock(rx, ry, sz - 1); spawn_rock(rx, ry, sz - 1); }
                        break;
                    }
                }
            }

            int rem = 0;
            for (int i = 0; i < 32; i++) if (rocks[i].alive) {
                rem++;
                rocks[i].x += rocks[i].vx; rocks[i].y += rocks[i].vy;
                if (rocks[i].x < 0) rocks[i].x += SCR_W; if (rocks[i].x >= SCR_W) rocks[i].x -= SCR_W;
                if (rocks[i].y < 0) rocks[i].y += SCR_H; if (rocks[i].y >= SCR_H) rocks[i].y -= SCR_H;
                float dx = px - rocks[i].x, dy = py - rocks[i].y;
                if (iframes <= 0 && dx * dx + dy * dy < (rocks[i].r + 6) * (rocks[i].r + 6)) {
                    lives--; iframes = 75; px = 240; py = 136; pvx = 0; pvy = 0;
                    play_sfx(120.0f, 3000);
                    if (lives <= 0) { gameOver = 1; save_best(); }
                }
            }
            if (rem == 0) spawn_wave(wave + 1);
        }

        fill_rect(0, 0, SCR_W, SCR_H, RGB(4, 6, 16));
        char buf[64];
        if (inTitle) {
            for (int _y = 0; _y < SCR_H; _y++) memcpy(&fb0[_y * BUF_W], &bg_synth[_y * SCR_W], SCR_W * 4);
            draw_text(170, 80, "A S T E R O I D E   X", RGB(0, 240, 255));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(188, 120, buf, RGB(140, 255, 160));
            draw_text(148, 160, "Pulsa [X] o [START] para Jugar", RGB(255, 255, 255));
            draw_text(95, 210, "Izq/Der: Rotar | []: Empuje | X: Laser | Tri: Warp", RGB(180, 200, 230));
        } else if (gameOver) {
            draw_text(190, 95, "GAME OVER", RGB(255, 70, 90));
            sprintf(buf, "PUNTOS: %d  RECORD: %d", score, best);
            draw_text(148, 130, buf, RGB(255, 255, 255));
            draw_text(155, 165, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else {
            for (int i = 0; i < 24; i++) if (bullets[i].alive) fill_rect((int)bullets[i].x - 1, (int)bullets[i].y - 1, 3, 3, RGB(255, 240, 100));
            for (int i = 0; i < 32; i++) if (rocks[i].alive) {
                int r = (int)rocks[i].r, rx = (int)rocks[i].x, ry = (int)rocks[i].y;
                draw_line(rx - r, ry, rx, ry - r, RGB(0, 230, 255));
                draw_line(rx, ry - r, rx + r, ry, RGB(0, 230, 255));
                draw_line(rx + r, ry, rx, ry + r, RGB(0, 230, 255));
                draw_line(rx, ry + r, rx - r, ry, RGB(0, 230, 255));
            }
            if ((iframes & 2) == 0) {
                int x0 = (int)(px + cosf(pang) * 10), y0 = (int)(py + sinf(pang) * 10);
                int x1 = (int)(px + cosf(pang + 2.4f) * 8), y1 = (int)(py + sinf(pang + 2.4f) * 8);
                int x2 = (int)(px + cosf(pang - 2.4f) * 8), y2 = (int)(py + sinf(pang - 2.4f) * 8);
                draw_line(x0, y0, x1, y1, RGB(255, 255, 255));
                draw_line(x1, y1, x2, y2, RGB(255, 255, 255));
                draw_line(x2, y2, x0, y0, RGB(255, 255, 255));
            }
            sprintf(buf, "VIDAS:%d  OLEADA:%d  PTS:%d", lives, wave, score);
            draw_text(10, 6, buf, RGB(255, 235, 100));
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
