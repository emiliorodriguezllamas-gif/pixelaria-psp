#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("DungeonTactics", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define GW 10
#define GH 7
#define TILE 32
#define OX 80
#define OY 28
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

static unsigned int __attribute__((aligned(16))) fb0[BUF_W * SCR_H];
static int running = 1;
static unsigned int rng = 0x12349876u;
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
    const float bass[4] = { 110.00f, 130.81f, 146.83f, 98.00f };
    while (arun) {
        short *o = abuf[cur];
        for (int i = 0; i < 1024; i++) {
            float m = 0.0f;
            if (sfx_on) {
                int st = (int)(bgm_t * 2.5f);
                float bf = bass[st & 3];
                float bt = bgm_t * bf - floorf(bgm_t * bf);
                m += (bt < 0.5f ? 1.0f : -1.0f) * 0.07f;
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

typedef struct { int x, y, hp, maxHp, atk, range, team, acted, alive; } Unit;
static Unit units[10];
static int grid[GH][GW];
static int cx = 1, cy = 3, sel = -1, wave = 1, score = 0, best = 0;
static int inTitle = 1, gameOver = 0;

static void load_best(void) { FILE *f = fopen("dt_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("dt_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static int unit_at(int x, int y) {
    for (int i = 0; i < 10; i++) if (units[i].alive && units[i].x == x && units[i].y == y) return i;
    return -1;
}

static void spawn_wave(int w) {
    wave = w;
    for (int i = 2; i < 10; i++) units[i].alive = 0;
    int cnt = 2 + w; if (cnt > 6) cnt = 6;
    for (int i = 0; i < cnt; i++) {
        units[2 + i].alive = 1;
        units[2 + i].team = 1;
        units[2 + i].x = GW - 1 - (i / 3);
        units[2 + i].y = 1 + (i % 5);
        units[2 + i].hp = units[2 + i].maxHp = 6 + w * 2;
        units[2 + i].atk = 3 + w / 2;
        units[2 + i].range = (i & 1) ? 2 : 1;
    }
}

static void start_game(void) {
    memset(grid, 0, sizeof(grid));
    grid[2][4] = 1; grid[4][4] = 1; grid[3][6] = 1;
    memset(units, 0, sizeof(units));
    units[0] = (Unit){ 1, 2, 20, 20, 6, 1, 0, 0, 1 }; /* Knight */
    units[1] = (Unit){ 1, 4, 14, 14, 5, 3, 0, 0, 1 }; /* Mage */
    score = 0; sel = -1; cx = 1; cy = 2;
    spawn_wave(1);
    inTitle = 0; gameOver = 0;
    play_sfx(660.0f, 2000);
}

static void enemy_turn(void) {
    for (int i = 2; i < 10; i++) {
        if (!units[i].alive) continue;
        int tgt = -1, bestD = 999;
        for (int h = 0; h < 2; h++) if (units[h].alive) {
            int d = abs(units[h].x - units[i].x) + abs(units[h].y - units[i].y);
            if (d < bestD) { bestD = d; tgt = h; }
        }
        if (tgt < 0) break;
        if (bestD <= units[i].range) {
            units[tgt].hp -= units[i].atk;
            play_sfx(180.0f, 1500);
            if (units[tgt].hp <= 0) units[tgt].alive = 0;
        } else {
            int nx = units[i].x + (units[tgt].x > units[i].x ? 1 : (units[tgt].x < units[i].x ? -1 : 0));
            int ny = units[i].y;
            if (nx == units[i].x || grid[ny][nx] || unit_at(nx, ny) >= 0) {
                nx = units[i].x;
                ny = units[i].y + (units[tgt].y > units[i].y ? 1 : (units[tgt].y < units[i].y ? -1 : 0));
            }
            if (nx >= 0 && nx < GW && ny >= 0 && ny < GH && !grid[ny][nx] && unit_at(nx, ny) < 0) {
                units[i].x = nx; units[i].y = ny;
            }
        }
    }
    if (!units[0].alive && !units[1].alive) { gameOver = 1; save_best(); }
    for (int h = 0; h < 2; h++) units[h].acted = 0;
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
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("dt_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

    SceCtrlData pad; unsigned int prev = 0;
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;
        if (pressed & PSP_CTRL_SELECT) sfx_on = !sfx_on;

        if (inTitle || gameOver) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        } else {
            if (pressed & PSP_CTRL_LEFT && cx > 0) cx--;
            if (pressed & PSP_CTRL_RIGHT && cx < GW - 1) cx++;
            if (pressed & PSP_CTRL_UP && cy > 0) cy--;
            if (pressed & PSP_CTRL_DOWN && cy < GH - 1) cy++;

            if (pressed & PSP_CTRL_START) { enemy_turn(); sel = -1; }

            if (pressed & PSP_CTRL_CROSS) {
                int u = unit_at(cx, cy);
                if (sel < 0) {
                    if (u >= 0 && units[u].team == 0 && !units[u].acted) { sel = u; play_sfx(520.0f, 800); }
                } else {
                    int dist = abs(cx - units[sel].x) + abs(cy - units[sel].y);
                    if (u >= 0 && units[u].team == 1 && dist <= units[sel].range) {
                        units[u].hp -= units[sel].atk;
                        units[sel].acted = 1; sel = -1;
                        play_sfx(740.0f, 1200);
                        if (units[u].hp <= 0) { units[u].alive = 0; score += 150; }
                    } else if (u < 0 && !grid[cy][cx] && dist <= 3) {
                        units[sel].x = cx; units[sel].y = cy;
                        units[sel].acted = 1; sel = -1;
                        play_sfx(420.0f, 600);
                    } else {
                        sel = -1;
                    }
                    int allDone = 1;
                    for (int h = 0; h < 2; h++) if (units[h].alive && !units[h].acted) allDone = 0;
                    if (allDone) enemy_turn();
                }
            }

            int eAlive = 0;
            for (int i = 2; i < 10; i++) if (units[i].alive) eAlive++;
            if (eAlive == 0) {
                for (int h = 0; h < 2; h++) if (units[h].alive) { units[h].hp = units[h].maxHp; units[h].acted = 0; }
                spawn_wave(wave + 1);
            }
        }

        fill_rect(0, 0, SCR_W, SCR_H, RGB(18, 22, 34));
        char buf[64];
        if (inTitle) {
            draw_text(155, 80, "D U N G E O N   T A C T I C S", RGB(255, 225, 80));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(188, 120, buf, RGB(140, 255, 160));
            draw_text(148, 160, "Pulsa [X] o [START] para Jugar", RGB(255, 255, 255));
            draw_text(105, 204, "Cruceta: Cursor | X: Mover/Atacar | START: Fin Turno", RGB(190, 200, 230));
        } else if (gameOver) {
            draw_text(190, 95, "DERROTA TACTICA", RGB(255, 70, 90));
            sprintf(buf, "PUNTOS: %d  RECORD: %d", score, best);
            draw_text(148, 130, buf, RGB(255, 255, 255));
            draw_text(155, 165, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else {
            for (int y = 0; y < GH; y++) {
                for (int x = 0; x < GW; x++) {
                    unsigned int tc = grid[y][x] ? RGB(70, 75, 90) : (((x + y) & 1) ? RGB(34, 42, 62) : RGB(40, 50, 72));
                    fill_rect(OX + x * TILE, OY + y * TILE, TILE - 1, TILE - 1, tc);
                }
            }
            for (int i = 0; i < 10; i++) if (units[i].alive) {
                int ux = OX + units[i].x * TILE + 6, uy = OY + units[i].y * TILE + 6;
                unsigned int uc = (units[i].team == 0) ? (i == 0 ? RGB(60, 180, 255) : RGB(180, 90, 255)) : RGB(240, 70, 70);
                fill_rect(ux, uy, 20, 20, uc);
                if (units[i].acted) draw_outline(ux, uy, 20, 20, RGB(120, 120, 120));
            }
            draw_outline(OX + cx * TILE, OY + cy * TILE, TILE - 1, TILE - 1, sel >= 0 ? RGB(80, 255, 120) : RGB(255, 235, 80));
            sprintf(buf, "OLEADA:%d  PTS:%d  (START: Fin Turno)", wave, score);
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
