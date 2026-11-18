#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("PixelFighter", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define FLOOR_Y 228
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

static unsigned int __attribute__((aligned(16))) fb0[BUF_W * SCR_H];
static int running = 1;
static unsigned int rng = 0x1122AABB;
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
    const float bass[4] = { 82.41f, 98.00f, 110.00f, 123.47f };
    while (arun) {
        short *o = abuf[cur];
        for (int i = 0; i < 1024; i++) {
            float m = 0.0f;
            if (sfx_on) {
                int st = (int)(bgm_t * 5.0f);
                float bf = bass[st & 3];
                float bt = bgm_t * bf - floorf(bgm_t * bf);
                m += (2.0f * bt - 1.0f) * 0.10f;
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

typedef struct { float x, y, vy; int hp, atkT, block, super, facing, cd; } Fighter;
typedef struct { float x, y, vx; int fromP, isSuper, alive; } Fireball;

static Fighter p1, cpu;
static Fireball fb[8];
static int wins = 0, best = 0, inTitle = 1, roundOver = 0, p1Won = 0;

static void load_best(void) { FILE *f = fopen("pf_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (wins > best) { best = wins; FILE *f = fopen("pf_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static void start_round(int resetWins) {
    if (resetWins) wins = 0;
    p1 = (Fighter){ 120, FLOOR_Y, 0, 100, 0, 0, 0, 1, 0 };
    cpu = (Fighter){ 360, FLOOR_Y, 0, 100, 0, 0, 0, -1, 0 };
    memset(fb, 0, sizeof(fb));
    inTitle = 0; roundOver = 0;
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
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("pf_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

    SceCtrlData pad; unsigned int prev = 0;
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;
        if (pressed & PSP_CTRL_SELECT) sfx_on = !sfx_on;

        if (inTitle || roundOver) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_round(inTitle || !p1Won);
        } else {
            p1.facing = (cpu.x >= p1.x) ? 1 : -1;
            cpu.facing = -p1.facing;

            float ax = ((float)pad.Lx - 128.0f) / 128.0f;
            if (fabsf(ax) < 0.2f) ax = 0;
            if (pad.Buttons & PSP_CTRL_LEFT) ax = -1;
            if (pad.Buttons & PSP_CTRL_RIGHT) ax = 1;
            p1.block = (pad.Buttons & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) ? 1 : 0;
            if (!p1.block) p1.x += ax * 3.0f;
            if (p1.x < 30) p1.x = 30; if (p1.x > SCR_W - 30) p1.x = SCR_W - 30;

            if ((pressed & (PSP_CTRL_UP | PSP_CTRL_CROSS)) && p1.y >= FLOOR_Y) { p1.vy = -8.5f; play_sfx(420.0f, 800); }
            p1.vy += 0.42f; p1.y += p1.vy;
            if (p1.y >= FLOOR_Y) { p1.y = FLOOR_Y; p1.vy = 0; }

            if (p1.atkT > 0) p1.atkT--;
            if (p1.cd > 0) p1.cd--;
            if ((pressed & (PSP_CTRL_SQUARE | PSP_CTRL_TRIANGLE)) && p1.cd <= 0) {
                int heavy = (pressed & PSP_CTRL_TRIANGLE) ? 1 : 0;
                p1.atkT = 12; p1.cd = heavy ? 22 : 14;
                play_sfx(heavy ? 320.0f : 540.0f, 900);
                if (fabsf(p1.x - cpu.x) < 42 && fabsf(p1.y - cpu.y) < 36) {
                    int dmg = heavy ? 12 : 7;
                    if (cpu.block) dmg /= 3;
                    cpu.hp -= dmg;
                    p1.super = (p1.super + 20 > 100) ? 100 : p1.super + 20;
                    play_sfx(180.0f, 1200);
                }
            }
            if ((pressed & PSP_CTRL_CIRCLE) && p1.cd <= 0) {
                int isSup = (p1.super >= 100);
                if (isSup) p1.super = 0;
                p1.cd = 28;
                for (int i = 0; i < 8; i++) if (!fb[i].alive) {
                    fb[i].alive = 1; fb[i].fromP = 1; fb[i].isSuper = isSup;
                    fb[i].x = p1.x + p1.facing * 16; fb[i].y = p1.y - 24;
                    fb[i].vx = p1.facing * (isSup ? 7.5f : 5.5f); break;
                }
                play_sfx(isSup ? 880.0f : 620.0f, 1500);
            }

            /* CPU AI */
            float dist = fabsf(cpu.x - p1.x);
            if (cpu.cd > 0) cpu.cd--;
            if (cpu.atkT > 0) cpu.atkT--;
            cpu.block = (dist < 45 && (xr() % 5) == 0);
            if (!cpu.block && dist > 34) cpu.x += cpu.facing * 1.8f;
            if (dist < 38 && cpu.cd <= 0) {
                cpu.atkT = 12; cpu.cd = 32;
                int dmg = p1.block ? 3 : 8;
                p1.hp -= dmg; play_sfx(160.0f, 1000);
            } else if (dist > 140 && cpu.cd <= 0 && (xr() % 45) == 0) {
                cpu.cd = 45;
                for (int i = 0; i < 8; i++) if (!fb[i].alive) {
                    fb[i].alive = 1; fb[i].fromP = 0; fb[i].isSuper = 0;
                    fb[i].x = cpu.x + cpu.facing * 16; fb[i].y = cpu.y - 24;
                    fb[i].vx = cpu.facing * 5.0f; break;
                }
            }

            for (int i = 0; i < 8; i++) if (fb[i].alive) {
                fb[i].x += fb[i].vx;
                if (fb[i].x < 0 || fb[i].x > SCR_W) { fb[i].alive = 0; continue; }
                Fighter *t = fb[i].fromP ? &cpu : &p1;
                if (fabsf(fb[i].x - t->x) < 16 && fabsf(fb[i].y - (t->y - 20)) < 24) {
                    int dmg = fb[i].isSuper ? 26 : 10;
                    if (t->block) dmg /= 3;
                    t->hp -= dmg; fb[i].alive = 0;
                    play_sfx(180.0f, 1400);
                }
            }

            if (cpu.hp <= 0) { cpu.hp = 0; roundOver = 1; p1Won = 1; wins++; save_best(); }
            else if (p1.hp <= 0) { p1.hp = 0; roundOver = 1; p1Won = 0; save_best(); }
        }

        fill_rect(0, 0, SCR_W, SCR_H, RGB(22, 16, 36));
        fill_rect(0, FLOOR_Y, SCR_W, SCR_H - FLOOR_Y, RGB(60, 45, 35));

        char buf[64];
        if (inTitle) {
            draw_text(165, 80, "P I X E L   F I G H T E R", RGB(255, 225, 80));
            sprintf(buf, "RACHA MAXIMA: %d VICTORIAS", best);
            draw_text(165, 120, buf, RGB(140, 255, 160));
            draw_text(148, 160, "Pulsa [X] o [START] para Luchar", RGB(255, 255, 255));
            draw_text(75, 204, "[]: Puno | Tri: Patada | O: Hadouken | L/R: Bloquear", RGB(190, 200, 230));
        } else if (roundOver) {
            draw_text(185, 95, p1Won ? "K.O. - VICTORIA!" : "K.O. - DERROTA", p1Won ? RGB(255,230,80) : RGB(255,70,90));
            sprintf(buf, "RACHA: %d  RECORD: %d", wins, best);
            draw_text(168, 130, buf, RGB(255, 255, 255));
            draw_text(155, 165, "Pulsa [X] para Siguiente Combate", RGB(160, 225, 255));
        } else {
            /* HP Bars */
            fill_rect(20, 14, 180, 12, RGB(40, 40, 55));
            fill_rect(20, 14, (p1.hp * 180) / 100, 12, RGB(60, 180, 255));
            fill_rect(280, 14, 180, 12, RGB(40, 40, 55));
            fill_rect(280, 14, (cpu.hp * 180) / 100, 12, RGB(255, 60, 80));
            fill_rect(20, 30, (p1.super * 120) / 100, 5, RGB(255, 220, 60));

            /* Fighters */
            fill_rect((int)p1.x - 10, (int)p1.y - 42, 20, 42, p1.block ? RGB(140, 220, 255) : RGB(50, 140, 255));
            if (p1.atkT > 0) fill_rect((int)p1.x + p1.facing * 10, (int)p1.y - 32, p1.facing * 18, 8, RGB(255, 240, 100));
            fill_rect((int)cpu.x - 10, (int)cpu.y - 42, 20, 42, cpu.block ? RGB(255, 160, 160) : RGB(230, 50, 70));
            if (cpu.atkT > 0) fill_rect((int)cpu.x + cpu.facing * 10, (int)cpu.y - 32, cpu.facing * 18, 8, RGB(255, 200, 80));

            for (int i = 0; i < 8; i++) if (fb[i].alive) {
                int s = fb[i].isSuper ? 10 : 6;
                fill_rect((int)fb[i].x - s, (int)fb[i].y - s, s * 2, s * 2, fb[i].isSuper ? RGB(255, 230, 60) : RGB(0, 235, 255));
            }
            sprintf(buf, "RACHA:%d", wins);
            draw_text(215, 16, buf, RGB(255, 255, 255));
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
