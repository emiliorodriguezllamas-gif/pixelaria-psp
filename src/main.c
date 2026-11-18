#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("NeonStriker", 0, 1, 0);
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
static unsigned int rng = 0x99887766u;

static unsigned int xr(void) {
    unsigned int x = rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng = x;
    return x;
}

static int exit_cb(int a, int b, void *c) { (void)a; (void)b; (void)c; running = 0; return 0; }
static int cb_th(SceSize a, void *p) { (void)a; (void)p; sceKernelRegisterExitCallback(sceKernelCreateCallback("Exit", exit_cb, NULL)); sceKernelSleepThreadCB(); return 0; }

static void put_px(int x, int y, unsigned int c) {
    if ((unsigned)x < SCR_W && (unsigned)y < SCR_H) fb0[y * BUF_W + x] = c;
}
static void fill_rect(int x, int y, int w, int h, unsigned int c) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x;
    if (y + h > SCR_H) h = SCR_H - y;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            fb0[(y + j) * BUF_W + (x + i)] = c;
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

/* Audio */
static int ach = -1, ath = -1;
static volatile int arun = 0, sfx_t = 0, sfx_on = 1;
static float sfx_f = 440.0f, sfx_p = 0.0f, bgm_t = 0.0f;
static short abuf[2][1024 * 2];

static void play_sfx(float f, int dur) {
    if (!sfx_on) return;
    sfx_f = f; sfx_t = dur; sfx_p = 0.0f;
}

static int audio_th(SceSize a, void *p) {
    (void)a; (void)p;
    int cur = 0;
    const float bass[4] = { 65.41f, 77.78f, 87.31f, 98.00f };
    while (arun) {
        short *o = abuf[cur];
        for (int i = 0; i < 1024; i++) {
            float m = 0.0f;
            if (sfx_on) {
                int st = (int)(bgm_t * 5.0f);
                float bf = bass[st & 3];
                float bt = bgm_t * bf - floorf(bgm_t * bf);
                m += (2.0f * bt - 1.0f) * 0.12f;
                if (sfx_t > 0) {
                    sfx_p += sfx_f / 44100.0f;
                    m += sinf(sfx_p * 6.283f) * 0.25f * ((float)sfx_t / 4000.0f);
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

typedef struct { float x, y, vx, vy; int alive; } Bullet;
typedef struct { float x, y, spd; int hp, type, alive; } Enemy;
typedef struct { float x, y, vx, vy; unsigned int col; int life; } Part;

static Bullet bullets[48];
static Enemy enemies[28];
static Part parts[48];

static float px = 240, py = 136;
static int hp = 100, wave = 1, score = 0, best = 0, fireCd = 0, dashT = 0, iframes = 0;
static int inTitle = 1, gameOver = 0;

static void load_best(void) {
    FILE *f = fopen("ns_save.dat", "rb");
    if (f) { int v = 0; if (fread(&v, sizeof(int), 1, f) == 1 && v > 0) best = v; fclose(f); }
}
static void save_best(void) {
    if (score > best) {
        best = score;
        FILE *f = fopen("ns_save.dat", "wb");
        if (f) { fwrite(&best, sizeof(int), 1, f); fclose(f); }
    }
}

static void spawn_parts(float x, float y, unsigned int col, int cnt) {
    int n = 0;
    for (int i = 0; i < 48 && n < cnt; i++) {
        if (parts[i].life <= 0) {
            parts[i].x = x; parts[i].y = y;
            parts[i].vx = ((int)(xr() % 40) - 20) * 0.14f;
            parts[i].vy = ((int)(xr() % 40) - 20) * 0.14f;
            parts[i].col = col;
            parts[i].life = 14 + (xr() % 10);
            n++;
        }
    }
}

static void spawn_wave(int w) {
    wave = w;
    int cnt = 5 + w * 2;
    if (cnt > 28) cnt = 28;
    for (int i = 0; i < cnt; i++) {
        enemies[i].alive = 1;
        int side = xr() & 3;
        if (side == 0) { enemies[i].x = (float)(xr() % SCR_W); enemies[i].y = 10.0f; }
        else if (side == 1) { enemies[i].x = (float)(xr() % SCR_W); enemies[i].y = SCR_H - 10.0f; }
        else if (side == 2) { enemies[i].x = 10.0f; enemies[i].y = (float)(xr() % SCR_H); }
        else { enemies[i].x = SCR_W - 10.0f; enemies[i].y = (float)(xr() % SCR_H); }
        enemies[i].type = xr() % 3;
        enemies[i].hp = (enemies[i].type == 2) ? (3 + w / 2) : 1;
        enemies[i].spd = (enemies[i].type == 1) ? 1.85f : (enemies[i].type == 2 ? 0.85f : 1.25f);
    }
}

static void start_game(void) {
    px = 240; py = 136; hp = 100; score = 0; fireCd = 0; dashT = 0; iframes = 0;
    memset(bullets, 0, sizeof(bullets));
    memset(enemies, 0, sizeof(enemies));
    memset(parts, 0, sizeof(parts));
    inTitle = 0; gameOver = 0;
    spawn_wave(1);
    play_sfx(660.0f, 2500);
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
    if (ach >= 0) {
        arun = 1;
        ath = sceKernelCreateThread("ns_aud", audio_th, 0x12, 0x10000, 0, NULL);
        if (ath >= 0) sceKernelStartThread(ath, 0, NULL);
    }

    SceCtrlData pad;
    unsigned int prev = 0;
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;

        if (pressed & PSP_CTRL_SELECT) sfx_on = !sfx_on;

        if (inTitle || gameOver) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        } else {
            float ax = ((float)pad.Lx - 128.0f) / 128.0f;
            float ay = ((float)pad.Ly - 128.0f) / 128.0f;
            if (fabsf(ax) < 0.18f) ax = 0.0f;
            if (fabsf(ay) < 0.18f) ay = 0.0f;
            if (pad.Buttons & PSP_CTRL_LEFT)  ax = -1.0f;
            if (pad.Buttons & PSP_CTRL_RIGHT) ax =  1.0f;
            if (pad.Buttons & PSP_CTRL_UP)    ay = -1.0f;
            if (pad.Buttons & PSP_CTRL_DOWN)  ay =  1.0f;

            if ((pressed & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) && dashT <= 0) {
                dashT = 12; iframes = 14; play_sfx(520.0f, 1800);
            }
            if (dashT > 0) dashT--;
            if (iframes > 0) iframes--;

            float spd = (dashT > 0) ? 5.6f : 2.8f;
            px += ax * spd; py += ay * spd;
            if (px < 12) px = 12; if (px > SCR_W - 12) px = SCR_W - 12;
            if (py < 22) py = 22; if (py > SCR_H - 12) py = SCR_H - 12;

            float sx = 0, sy = 0;
            if (pad.Buttons & PSP_CTRL_TRIANGLE) sy = -1;
            if (pad.Buttons & PSP_CTRL_CROSS)    sy =  1;
            if (pad.Buttons & PSP_CTRL_SQUARE)   sx = -1;
            if (pad.Buttons & PSP_CTRL_CIRCLE)   sx =  1;

            if (fireCd > 0) fireCd--;
            if ((sx != 0 || sy != 0) && fireCd <= 0) {
                fireCd = 6;
                float len = sqrtf(sx * sx + sy * sy);
                for (int i = 0; i < 48; i++) {
                    if (!bullets[i].alive) {
                        bullets[i].alive = 1;
                        bullets[i].x = px; bullets[i].y = py;
                        bullets[i].vx = (sx / len) * 7.2f;
                        bullets[i].vy = (sy / len) * 7.2f;
                        break;
                    }
                }
                play_sfx(780.0f, 900);
            }

            for (int i = 0; i < 48; i++) {
                if (!bullets[i].alive) continue;
                bullets[i].x += bullets[i].vx; bullets[i].y += bullets[i].vy;
                if (bullets[i].x < 0 || bullets[i].x > SCR_W || bullets[i].y < 0 || bullets[i].y > SCR_H) {
                    bullets[i].alive = 0; continue;
                }
                for (int j = 0; j < 28; j++) {
                    if (!enemies[j].alive) continue;
                    if (fabsf(bullets[i].x - enemies[j].x) < 10 && fabsf(bullets[i].y - enemies[j].y) < 10) {
                        bullets[i].alive = 0;
                        enemies[j].hp--;
                        if (enemies[j].hp <= 0) {
                            enemies[j].alive = 0;
                            score += 100;
                            spawn_parts(enemies[j].x, enemies[j].y, RGB(255, 80, 220), 10);
                            play_sfx(220.0f, 1600);
                        }
                        break;
                    }
                }
            }

            int alive = 0;
            for (int i = 0; i < 28; i++) {
                if (!enemies[i].alive) continue;
                alive++;
                float dx = px - enemies[i].x, dy = py - enemies[i].y;
                float d = sqrtf(dx * dx + dy * dy) + 0.001f;
                enemies[i].x += (dx / d) * enemies[i].spd;
                enemies[i].y += (dy / d) * enemies[i].spd;
                if (d < 12.0f && iframes <= 0) {
                    hp -= 20; iframes = 40;
                    spawn_parts(px, py, RGB(255, 60, 60), 12);
                    play_sfx(140.0f, 3000);
                    if (hp <= 0) { hp = 0; gameOver = 1; save_best(); }
                }
            }
            if (alive == 0) spawn_wave(wave + 1);

            for (int i = 0; i < 48; i++) {
                if (parts[i].life > 0) {
                    parts[i].x += parts[i].vx; parts[i].y += parts[i].vy; parts[i].life--;
                }
            }
        }

        /* Render */
        fill_rect(0, 0, SCR_W, SCR_H, RGB(8, 10, 22));
        for (int gx = 0; gx < SCR_W; gx += 32) fill_rect(gx, 0, 1, SCR_H, RGB(16, 22, 46));
        for (int gy = 0; gy < SCR_H; gy += 32) fill_rect(0, gy, SCR_W, 1, RGB(16, 22, 46));

        char buf[64];
        if (inTitle) {
            for (int _y = 0; _y < SCR_H; _y++) memcpy(&fb0[_y * BUF_W], &bg_synth[_y * SCR_W], SCR_W * 4);
            draw_text(165, 80, "N E O N   S T R I K E R", RGB(0, 240, 255));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(188, 120, buf, RGB(140, 255, 160));
            draw_text(145, 160, "Pulsa [X] o [START] para Jugar", RGB(255, 255, 255));
            draw_text(95, 210, "Stick: Mover | Tri/O/X/[]: Disparar | L/R: Dash", RGB(180, 200, 230));
        } else if (gameOver) {
            draw_text(190, 95, "GAME OVER", RGB(255, 70, 90));
            sprintf(buf, "PUNTOS: %d  RECORD: %d", score, best);
            draw_text(148, 130, buf, RGB(255, 255, 255));
            draw_text(155, 165, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else {
            for (int i = 0; i < 48; i++) {
                if (parts[i].life > 0) fill_rect((int)parts[i].x - 1, (int)parts[i].y - 1, 3, 3, parts[i].col);
            }
            for (int i = 0; i < 48; i++) {
                if (bullets[i].alive) fill_rect((int)bullets[i].x - 2, (int)bullets[i].y - 2, 4, 4, RGB(0, 240, 255));
            }
            for (int i = 0; i < 28; i++) {
                if (!enemies[i].alive) continue;
                unsigned int ec = (enemies[i].type == 2) ? RGB(255, 160, 40) : (enemies[i].type == 1 ? RGB(255, 50, 190) : RGB(255, 60, 80));
                fill_rect((int)enemies[i].x - 6, (int)enemies[i].y - 6, 12, 12, ec);
            }
            if ((iframes & 2) == 0) {
                fill_rect((int)px - 6, (int)py - 6, 12, 12, RGB(80, 235, 255));
                draw_outline((int)px - 6, (int)py - 6, 12, 12, RGB(255, 255, 255));
            }
            sprintf(buf, "HP:%d  OLEADA:%d  PTS:%d", hp, wave, score);
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
