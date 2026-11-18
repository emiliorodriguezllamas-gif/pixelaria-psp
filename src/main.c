#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("RogueCrypt", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define MW 18
#define MH 10
#define TILE 24
#define OX 24
#define OY 24
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

static unsigned int __attribute__((aligned(16))) fb0[BUF_W * SCR_H];
static int running = 1;
static unsigned int rng = 0x99118822u;
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
    const float bass[4] = { 65.41f, 69.30f, 73.42f, 65.41f };
    while (arun) {
        short *o = abuf[cur];
        for (int i = 0; i < 1024; i++) {
            float m = 0.0f;
            if (sfx_on) {
                int st = (int)(bgm_t * 2.5f);
                float bf = bass[st & 3];
                float bt = bgm_t * bf - floorf(bgm_t * bf);
                m += (2.0f * bt - 1.0f) * 0.07f;
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

typedef struct { int x, y, hp, atk, alive; } Mob;
static int map[MH][MW]; /* 0=floor, 1=wall, 2=stairs, 3=potion */
static Mob mobs[10];
static int px = 2, py = 2, hp = 30, maxHp = 30, atk = 4, floorLv = 1, score = 0, best = 0;
static int inTitle = 1, gameOver = 0;

static void load_best(void) { FILE *f = fopen("rc_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("rc_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static void gen_floor(int fl) {
    floorLv = fl;
    for (int y = 0; y < MH; y++)
        for (int x = 0; x < MW; x++)
            map[y][x] = (x == 0 || y == 0 || x == MW - 1 || y == MH - 1 || ((xr() % 7) == 0)) ? 1 : 0;
    px = 2; py = 2; map[py][px] = 0;
    map[MH - 3][MW - 3] = 2; /* Stairs */
    map[2 + (xr() % (MH - 4))][2 + (xr() % (MW - 4))] = 3; /* Potion */
    memset(mobs, 0, sizeof(mobs));
    int cnt = 3 + fl; if (cnt > 10) cnt = 10;
    for (int i = 0; i < cnt; i++) {
        int mx = 3 + (xr() % (MW - 5)), my = 2 + (xr() % (MH - 4));
        if (map[my][mx] == 0 && (mx != px || my != py)) {
            mobs[i].alive = 1; mobs[i].x = mx; mobs[i].y = my;
            mobs[i].hp = 4 + fl * 2; mobs[i].atk = 2 + fl / 2;
        }
    }
}

static void start_game(void) {
    hp = 30; maxHp = 30; atk = 4; score = 0;
    gen_floor(1);
    inTitle = 0; gameOver = 0;
    play_sfx(660.0f, 2000);
}

static void step_mobs(void) {
    for (int i = 0; i < 10; i++) if (mobs[i].alive) {
        int dx = px - mobs[i].x, dy = py - mobs[i].y;
        if (abs(dx) + abs(dy) == 1) {
            hp -= mobs[i].atk;
            play_sfx(160.0f, 1500);
            if (hp <= 0) { hp = 0; gameOver = 1; save_best(); }
        } else if (abs(dx) + abs(dy) <= 6) {
            int nx = mobs[i].x + (dx > 0 ? 1 : (dx < 0 ? -1 : 0));
            int ny = mobs[i].y;
            if (nx == mobs[i].x || map[ny][nx] == 1) { nx = mobs[i].x; ny = mobs[i].y + (dy > 0 ? 1 : (dy < 0 ? -1 : 0)); }
            if (map[ny][nx] != 1 && (nx != px || ny != py)) { mobs[i].x = nx; mobs[i].y = ny; }
        }
    }
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
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("rc_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

    SceCtrlData pad; unsigned int prev = 0;
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;
        if (pressed & PSP_CTRL_SELECT) sfx_on = !sfx_on;

        if (inTitle || gameOver) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        } else {
            int dx = 0, dy = 0;
            if (pressed & PSP_CTRL_LEFT) dx = -1;
            else if (pressed & PSP_CTRL_RIGHT) dx = 1;
            else if (pressed & PSP_CTRL_UP) dy = -1;
            else if (pressed & PSP_CTRL_DOWN) dy = 1;

            if (dx != 0 || dy != 0) {
                int nx = px + dx, ny = py + dy;
                int hitMob = -1;
                for (int i = 0; i < 10; i++) if (mobs[i].alive && mobs[i].x == nx && mobs[i].y == ny) { hitMob = i; break; }
                if (hitMob >= 0) {
                    mobs[hitMob].hp -= atk;
                    play_sfx(640.0f, 1000);
                    if (mobs[hitMob].hp <= 0) { mobs[hitMob].alive = 0; score += 100; }
                    step_mobs();
                } else if (map[ny][nx] != 1) {
                    px = nx; py = ny;
                    if (map[py][px] == 3) { map[py][px] = 0; hp = (hp + 12 > maxHp) ? maxHp : hp + 12; play_sfx(880.0f, 1200); }
                    else if (map[py][px] == 2) { score += 200 * floorLv; atk++; gen_floor(floorLv + 1); play_sfx(780.0f, 1800); }
                    else step_mobs();
                }
            } else if (pressed & PSP_CTRL_CROSS) {
                step_mobs();
            }
        }

        fill_rect(0, 0, SCR_W, SCR_H, RGB(14, 12, 18));
        char buf[64];
        if (inTitle) {
            draw_text(175, 80, "R O G U E   C R Y P T", RGB(255, 220, 80));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(188, 120, buf, RGB(140, 255, 160));
            draw_text(148, 160, "Pulsa [X] o [START] para Entrar", RGB(255, 255, 255));
            draw_text(115, 204, "Cruceta: Mover/Atacar | X: Esperar Turno", RGB(190, 200, 230));
        } else if (gameOver) {
            draw_text(190, 95, "HAS MUERTO!", RGB(255, 70, 90));
            sprintf(buf, "PUNTOS: %d  RECORD: %d", score, best);
            draw_text(148, 130, buf, RGB(255, 255, 255));
            draw_text(155, 165, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else {
            for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++) {
                unsigned int tc = RGB(28, 26, 36);
                if (map[y][x] == 1) tc = RGB(70, 65, 85);
                else if (map[y][x] == 2) tc = RGB(80, 240, 120);
                else if (map[y][x] == 3) tc = RGB(255, 80, 180);
                fill_rect(OX + x * TILE, OY + y * TILE, TILE - 1, TILE - 1, tc);
            }
            for (int i = 0; i < 10; i++) if (mobs[i].alive) fill_rect(OX + mobs[i].x * TILE + 4, OY + mobs[i].y * TILE + 4, 16, 16, RGB(230, 60, 70));
            fill_rect(OX + px * TILE + 4, OY + py * TILE + 4, 16, 16, RGB(60, 210, 255));
            sprintf(buf, "HP:%d/%d  PISO:%d  PTS:%d", hp, maxHp, floorLv, score);
            draw_text(10, 6, buf, RGB(255, 235, 100));
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
