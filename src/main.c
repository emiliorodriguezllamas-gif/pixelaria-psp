#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("TowerSiege", 0, 1, 0);
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
    const float bass[4] = { 98.00f, 110.00f, 123.47f, 98.00f };
    while (arun) {
        short *o = abuf[cur];
        for (int i = 0; i < 1024; i++) {
            float m = 0.0f;
            if (sfx_on) {
                int st = (int)(bgm_t * 3.5f);
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

typedef struct { float x, y; int wp, hp, slowT, alive; } Enemy;
typedef struct { float x, y; int type, cd, alive; } Tower; /* 0=fast, 1=cannon, 2=ice */
typedef struct { float x, y, vx, vy; int dmg, slow, alive; } Shot;

static const float wpx[6] = { 0, 140, 140, 320, 320, 480 };
static const float wpy[6] = { 70, 70, 200, 200, 90, 90 };

static Enemy enemies[20];
static Tower towers[20];
static Shot shots[24];
static float cx = 200, cy = 136;
static int lives = 10, gold = 120, wave = 1, score = 0, best = 0, tick = 0;
static int inTitle = 1, gameOver = 0;

static void load_best(void) { FILE *f = fopen("ts_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("ts_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static void start_game(void) {
    cx = 200; cy = 136; lives = 10; gold = 140; wave = 1; score = 0; tick = 0;
    memset(enemies, 0, sizeof(enemies));
    memset(towers, 0, sizeof(towers));
    memset(shots, 0, sizeof(shots));
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
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("ts_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

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
            cx += ax * 3.5f; cy += ay * 3.5f;
            if (cx < 20) cx = 20; if (cx > SCR_W - 20) cx = SCR_W - 20;
            if (cy < 28) cy = 28; if (cy > SCR_H - 20) cy = SCR_H - 20;

            int buildType = -1, cost = 0;
            if (pressed & PSP_CTRL_CROSS) { buildType = 0; cost = 40; }
            else if (pressed & PSP_CTRL_SQUARE) { buildType = 1; cost = 70; }
            else if (pressed & PSP_CTRL_TRIANGLE) { buildType = 2; cost = 60; }

            if (buildType >= 0 && gold >= cost) {
                for (int i = 0; i < 20; i++) if (!towers[i].alive) {
                    towers[i].alive = 1; towers[i].type = buildType;
                    towers[i].x = cx; towers[i].y = cy; towers[i].cd = 0;
                    gold -= cost; play_sfx(660.0f, 1000); break;
                }
            }

            tick++;
            if ((tick % 55) == 0) {
                for (int i = 0; i < 20; i++) if (!enemies[i].alive) {
                    enemies[i].alive = 1; enemies[i].x = wpx[0]; enemies[i].y = wpy[0];
                    enemies[i].wp = 1; enemies[i].hp = 3 + wave * 2; enemies[i].slowT = 0; break;
                }
                if ((tick % 550) == 0) wave++;
            }

            for (int i = 0; i < 20; i++) if (towers[i].alive) {
                if (towers[i].cd > 0) towers[i].cd--;
                if (towers[i].cd <= 0) {
                    int tgt = -1; float bd = 120.0f * 120.0f;
                    for (int e = 0; e < 20; e++) if (enemies[e].alive) {
                        float dx = enemies[e].x - towers[i].x, dy = enemies[e].y - towers[i].y;
                        float d = dx * dx + dy * dy;
                        if (d < bd) { bd = d; tgt = e; }
                    }
                    if (tgt >= 0) {
                        towers[i].cd = (towers[i].type == 0) ? 18 : (towers[i].type == 1 ? 42 : 30);
                        float dx = enemies[tgt].x - towers[i].x, dy = enemies[tgt].y - towers[i].y;
                        float d = sqrtf(dx * dx + dy * dy) + 0.001f;
                        for (int s = 0; s < 24; s++) if (!shots[s].alive) {
                            shots[s].alive = 1; shots[s].x = towers[i].x; shots[s].y = towers[i].y;
                            shots[s].vx = (dx / d) * 6.0f; shots[s].vy = (dy / d) * 6.0f;
                            shots[s].dmg = (towers[i].type == 1) ? 4 : 1;
                            shots[s].slow = (towers[i].type == 2) ? 1 : 0;
                            break;
                        }
                    }
                }
            }

            for (int s = 0; s < 24; s++) if (shots[s].alive) {
                shots[s].x += shots[s].vx; shots[s].y += shots[s].vy;
                if (shots[s].x < 0 || shots[s].x > SCR_W || shots[s].y < 0 || shots[s].y > SCR_H) { shots[s].alive = 0; continue; }
                for (int e = 0; e < 20; e++) if (enemies[e].alive) {
                    if (fabsf(shots[s].x - enemies[e].x) < 10 && fabsf(shots[s].y - enemies[e].y) < 10) {
                        shots[s].alive = 0; enemies[e].hp -= shots[s].dmg;
                        if (shots[s].slow) enemies[e].slowT = 90;
                        if (enemies[e].hp <= 0) { enemies[e].alive = 0; gold += 15; score += 100; play_sfx(240.0f, 800); }
                        break;
                    }
                }
            }

            for (int e = 0; e < 20; e++) if (enemies[e].alive) {
                if (enemies[e].slowT > 0) enemies[e].slowT--;
                float spd = (enemies[e].slowT > 0) ? 0.55f : (1.1f + wave * 0.06f);
                float tx = wpx[enemies[e].wp], ty = wpy[enemies[e].wp];
                float dx = tx - enemies[e].x, dy = ty - enemies[e].y;
                float d = sqrtf(dx * dx + dy * dy);
                if (d < 4.0f) {
                    enemies[e].wp++;
                    if (enemies[e].wp >= 6) {
                        enemies[e].alive = 0; lives--; play_sfx(120.0f, 2000);
                        if (lives <= 0) { lives = 0; gameOver = 1; save_best(); }
                    }
                } else {
                    enemies[e].x += (dx / d) * spd;
                    enemies[e].y += (dy / d) * spd;
                }
            }
        }

        fill_rect(0, 0, SCR_W, SCR_H, RGB(24, 48, 28));
        /* Path */
        fill_rect(0, 58, 152, 24, RGB(120, 100, 75));
        fill_rect(128, 58, 24, 154, RGB(120, 100, 75));
        fill_rect(128, 188, 204, 24, RGB(120, 100, 75));
        fill_rect(308, 78, 24, 134, RGB(120, 100, 75));
        fill_rect(308, 78, 172, 24, RGB(120, 100, 75));

        char buf[64];
        if (inTitle) {
            for (int _y = 0; _y < SCR_H; _y++) memcpy(&fb0[_y * BUF_W], &bg_synth[_y * SCR_W], SCR_W * 4);
            draw_text(170, 80, "T O W E R   S I E G E", RGB(255, 225, 80));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(188, 120, buf, RGB(140, 255, 160));
            draw_text(148, 160, "Pulsa [X] o [START] para Jugar", RGB(255, 255, 255));
            draw_text(85, 204, "X: Rapida(40) | []: Canon(70) | Tri: Hielo(60)", RGB(200, 220, 240));
        } else if (gameOver) {
            draw_text(190, 95, "CASTILLO CAIDO!", RGB(255, 70, 90));
            sprintf(buf, "PUNTOS: %d  RECORD: %d", score, best);
            draw_text(148, 130, buf, RGB(255, 255, 255));
            draw_text(155, 165, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else {
            for (int i = 0; i < 20; i++) if (towers[i].alive) {
                unsigned int tc = (towers[i].type == 0) ? RGB(0, 220, 255) : (towers[i].type == 1 ? RGB(255, 150, 40) : RGB(150, 220, 255));
                fill_rect((int)towers[i].x - 7, (int)towers[i].y - 7, 14, 14, tc);
            }
            for (int e = 0; e < 20; e++) if (enemies[e].alive) fill_rect((int)enemies[e].x - 5, (int)enemies[e].y - 5, 10, 10, enemies[e].slowT ? RGB(100, 180, 255) : RGB(240, 60, 70));
            for (int s = 0; s < 24; s++) if (shots[s].alive) fill_rect((int)shots[s].x - 2, (int)shots[s].y - 2, 4, 4, RGB(255, 240, 100));
            draw_outline((int)cx - 8, (int)cy - 8, 16, 16, RGB(255, 255, 255));
            sprintf(buf, "VIDAS:%d  ORO:%d  OLEADA:%d  PTS:%d", lives, gold, wave, score);
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
