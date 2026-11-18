#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("ZombieOutbreak2D", 0, 1, 0);
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
static unsigned int rng = 0x66554433u;
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
    const float bass[4] = { 55.00f, 58.27f, 65.41f, 55.00f };
    while (arun) {
        short *o = abuf[cur];
        for (int i = 0; i < 1024; i++) {
            float m = 0.0f;
            if (sfx_on) {
                int st = (int)(bgm_t * 3.0f);
                float bf = bass[st & 3];
                float bt = bgm_t * bf - floorf(bgm_t * bf);
                m += (2.0f * bt - 1.0f) * 0.08f;
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

typedef struct { float x, y, vx, vy; int alive; } Bullet;
typedef struct { float x, y, spd; int hp, alive; } Zombie;
typedef struct { float x, y; int active; } Crate;

static Bullet bullets[24];
static Zombie zombies[24];
static Crate crate;
static float px = 240, py = 136, dirX = 1, dirY = 0;
static int hp = 100, ammo = 24, wave = 1, score = 0, best = 0, fireCd = 0, iframes = 0, spawnT = 0;
static int inTitle = 1, gameOver = 0;

static void load_best(void) { FILE *f = fopen("zo_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("zo_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static void start_game(void) {
    px = 240; py = 136; dirX = 1; dirY = 0;
    hp = 100; ammo = 24; wave = 1; score = 0; fireCd = 0; iframes = 0; spawnT = 0;
    memset(bullets, 0, sizeof(bullets));
    memset(zombies, 0, sizeof(zombies));
    crate.active = 1; crate.x = 120; crate.y = 100;
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
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("zo_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

    SceCtrlData pad; unsigned int prev = 0;
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;
        if (pressed & PSP_CTRL_SELECT) sfx_on = !sfx_on;

        if (inTitle || gameOver) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        } else {
            float ax = ((float)pad.Lx - 128.0f) / 128.0f, ay = ((float)pad.Ly - 128.0f) / 128.0f;
            if (fabsf(ax) < 0.18f) ax = 0; if (fabsf(ay) < 0.18f) ay = 0;
            if (pad.Buttons & PSP_CTRL_LEFT) ax = -1; if (pad.Buttons & PSP_CTRL_RIGHT) ax = 1;
            if (pad.Buttons & PSP_CTRL_UP) ay = -1; if (pad.Buttons & PSP_CTRL_DOWN) ay = 1;

            if (ax != 0 || ay != 0) {
                float len = sqrtf(ax * ax + ay * ay);
                dirX = ax / len; dirY = ay / len;
                px += dirX * 2.5f; py += dirY * 2.5f;
            }
            if (px < 12) px = 12; if (px > SCR_W - 12) px = SCR_W - 12;
            if (py < 24) py = 24; if (py > SCR_H - 12) py = SCR_H - 12;
            if (iframes > 0) iframes--;

            if (crate.active && fabsf(px - crate.x) < 16 && fabsf(py - crate.y) < 16) {
                ammo += 16; hp = (hp + 15 > 100) ? 100 : hp + 15;
                crate.x = 40 + (float)(xr() % (SCR_W - 80));
                crate.y = 40 + (float)(xr() % (SCR_H - 80));
                play_sfx(880.0f, 1200);
            }

            if (fireCd > 0) fireCd--;
            if ((pad.Buttons & PSP_CTRL_CROSS) && fireCd <= 0 && ammo > 0) {
                fireCd = 10; ammo--;
                for (int i = 0; i < 24; i++) if (!bullets[i].alive) {
                    bullets[i].alive = 1; bullets[i].x = px; bullets[i].y = py;
                    bullets[i].vx = dirX * 7.5f; bullets[i].vy = dirY * 7.5f; break;
                }
                play_sfx(680.0f, 800);
            }

            spawnT++;
            if ((spawnT % 48) == 0) {
                for (int i = 0; i < 24; i++) if (!zombies[i].alive) {
                    zombies[i].alive = 1;
                    int s = xr() & 3;
                    zombies[i].x = (s == 0) ? 0 : (s == 1 ? SCR_W : (float)(xr() % SCR_W));
                    zombies[i].y = (s == 2) ? 20 : (s == 3 ? SCR_H : (float)(xr() % SCR_H));
                    zombies[i].hp = 1 + wave / 2;
                    zombies[i].spd = 0.85f + wave * 0.08f;
                    break;
                }
                if ((spawnT % 480) == 0) wave++;
            }

            for (int i = 0; i < 24; i++) if (bullets[i].alive) {
                bullets[i].x += bullets[i].vx; bullets[i].y += bullets[i].vy;
                if (bullets[i].x < 0 || bullets[i].x > SCR_W || bullets[i].y < 0 || bullets[i].y > SCR_H) { bullets[i].alive = 0; continue; }
                for (int j = 0; j < 24; j++) if (zombies[j].alive) {
                    if (fabsf(bullets[i].x - zombies[j].x) < 10 && fabsf(bullets[i].y - zombies[j].y) < 10) {
                        bullets[i].alive = 0; zombies[j].hp--;
                        if (zombies[j].hp <= 0) { zombies[j].alive = 0; score += 100; play_sfx(200.0f, 1000); }
                        break;
                    }
                }
            }

            for (int i = 0; i < 24; i++) if (zombies[i].alive) {
                float dx = px - zombies[i].x, dy = py - zombies[i].y;
                float d = sqrtf(dx * dx + dy * dy) + 0.001f;
                zombies[i].x += (dx / d) * zombies[i].spd;
                zombies[i].y += (dy / d) * zombies[i].spd;
                if (d < 12.0f && iframes <= 0) {
                    hp -= 20; iframes = 40; play_sfx(120.0f, 2500);
                    if (hp <= 0) { hp = 0; gameOver = 1; save_best(); }
                }
            }
        }

        fill_rect(0, 0, SCR_W, SCR_H, RGB(14, 18, 16));
        char buf[64];
        if (inTitle) {
            for (int _y = 0; _y < SCR_H; _y++) memcpy(&fb0[_y * BUF_W], &bg_synth[_y * SCR_W], SCR_W * 4);
            draw_text(145, 80, "Z O M B I E   O U T B R E A K   2 D", RGB(120, 255, 120));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(188, 120, buf, RGB(255, 235, 100));
            draw_text(148, 160, "Pulsa [X] o [START] para Sobrevivir", RGB(255, 255, 255));
            draw_text(105, 204, "Stick: Mover | X: Disparar | Cajas: +Municion/+HP", RGB(190, 210, 200));
        } else if (gameOver) {
            draw_text(190, 95, "INFECTADO!", RGB(255, 70, 90));
            sprintf(buf, "PUNTOS: %d  RECORD: %d", score, best);
            draw_text(148, 130, buf, RGB(255, 255, 255));
            draw_text(155, 165, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else {
            if (crate.active) fill_rect((int)crate.x - 6, (int)crate.y - 6, 12, 12, RGB(255, 210, 60));
            for (int i = 0; i < 24; i++) if (bullets[i].alive) fill_rect((int)bullets[i].x - 2, (int)bullets[i].y - 2, 4, 4, RGB(255, 240, 100));
            for (int i = 0; i < 24; i++) if (zombies[i].alive) fill_rect((int)zombies[i].x - 6, (int)zombies[i].y - 6, 12, 12, RGB(80, 190, 80));
            if ((iframes & 2) == 0) fill_rect((int)px - 6, (int)py - 6, 12, 12, RGB(70, 180, 255));
            sprintf(buf, "HP:%d  BALAS:%d  OLEADA:%d  PTS:%d", hp, ammo, wave, score);
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
