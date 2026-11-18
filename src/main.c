#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"

PSP_MODULE_INFO("CaveExplorer", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define GROUND_Y 236
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

static unsigned int __attribute__((aligned(16))) fb0[BUF_W * SCR_H];
static int running = 1;
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
    const float bass[4] = { 65.41f, 73.42f, 82.41f, 65.41f };
    while (arun) {
        short *o = abuf[cur];
        for (int i = 0; i < 1024; i++) {
            float m = 0.0f;
            if (sfx_on) {
                int st = (int)(bgm_t * 3.0f);
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

typedef struct { float x, y, vx; int alive; } Shot;
typedef struct { float x, y, vx; int hp, alive; } Mob;

static Shot shots[16];
static Mob mobs[8];
static float px = 60, py = GROUND_Y, pvy = 0;
static int facing = 1, jumps = 1, hasDoubleJump = 0, hasPowerShot = 0;
static int room = 0, hp = 100, iframes = 0, score = 0, best = 0;
static int inTitle = 1, gameOver = 0, victory = 0;

static void load_best(void) { FILE *f = fopen("ce_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("ce_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static void enter_room(int r) {
    room = r;
    memset(shots, 0, sizeof(shots));
    memset(mobs, 0, sizeof(mobs));
    int cnt = (r == 4) ? 1 : (2 + r);
    for (int i = 0; i < cnt; i++) {
        mobs[i].alive = 1;
        mobs[i].x = 180.0f + i * 70.0f;
        mobs[i].y = GROUND_Y;
        mobs[i].vx = (i & 1) ? -1.2f : 1.2f;
        mobs[i].hp = (r == 4) ? 18 : (2 + r);
    }
}

static void start_game(void) {
    px = 60; py = GROUND_Y; pvy = 0; facing = 1; hasDoubleJump = 0; hasPowerShot = 0;
    hp = 100; iframes = 0; score = 0;
    inTitle = 0; gameOver = 0; victory = 0;
    enter_room(0);
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
    if (ach >= 0) { arun = 1; ath = sceKernelCreateThread("ce_aud", audio_th, 0x12, 0x10000, 0, NULL); if (ath >= 0) sceKernelStartThread(ath, 0, NULL); }

    SceCtrlData pad; unsigned int prev = 0;
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;
        if (pressed & PSP_CTRL_SELECT) sfx_on = !sfx_on;

        if (inTitle || gameOver || victory) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        } else {
            float ax = ((float)pad.Lx - 128.0f) / 128.0f;
            if (fabsf(ax) < 0.2f) ax = 0;
            if (pad.Buttons & PSP_CTRL_LEFT) ax = -1;
            if (pad.Buttons & PSP_CTRL_RIGHT) ax = 1;
            if (ax < -0.1f) facing = -1; else if (ax > 0.1f) facing = 1;
            px += ax * 2.8f;

            if (px < 8) {
                if (room > 0) { enter_room(room - 1); px = SCR_W - 20; }
                else px = 8;
            } else if (px > SCR_W - 8) {
                if (room < 4) { enter_room(room + 1); px = 20; }
                else px = SCR_W - 8;
            }

            if ((pressed & PSP_CTRL_CROSS) && jumps > 0) {
                pvy = -7.6f; jumps--; play_sfx(480.0f, 1000);
            }
            pvy += 0.36f; py += pvy;
            if (py >= GROUND_Y) { py = GROUND_Y; pvy = 0; jumps = hasDoubleJump ? 2 : 1; }
            if (pvy > 0 && py >= 166 && py <= 176 && px > 160 && px < 320) {
                py = 166; pvy = 0; jumps = hasDoubleJump ? 2 : 1;
            }
            if (iframes > 0) iframes--;

            /* Upgrades */
            if (room == 1 && !hasDoubleJump && fabsf(px - 240) < 16 && fabsf(py - 166) < 20) {
                hasDoubleJump = 1; score += 300; play_sfx(880.0f, 2000);
            }
            if (room == 3 && !hasPowerShot && fabsf(px - 240) < 16 && fabsf(py - 166) < 20) {
                hasPowerShot = 1; score += 300; play_sfx(880.0f, 2000);
            }

            if (pressed & PSP_CTRL_SQUARE) {
                for (int i = 0; i < 16; i++) if (!shots[i].alive) {
                    shots[i].alive = 1; shots[i].x = px; shots[i].y = py - 12;
                    shots[i].vx = facing * 6.8f; break;
                }
                play_sfx(700.0f, 800);
            }

            for (int i = 0; i < 16; i++) if (shots[i].alive) {
                shots[i].x += shots[i].vx;
                if (shots[i].x < 0 || shots[i].x > SCR_W) { shots[i].alive = 0; continue; }
                for (int j = 0; j < 8; j++) if (mobs[j].alive) {
                    int r = (room == 4) ? 20 : 12;
                    if (fabsf(shots[i].x - mobs[j].x) < r && fabsf(shots[i].y - (mobs[j].y - r)) < r + 8) {
                        shots[i].alive = 0;
                        mobs[j].hp -= (hasPowerShot ? 2 : 1);
                        if (mobs[j].hp <= 0) {
                            mobs[j].alive = 0;
                            score += (room == 4) ? 1500 : 150;
                            play_sfx(200.0f, 1600);
                            if (room == 4) { victory = 1; save_best(); }
                        }
                        break;
                    }
                }
            }

            for (int i = 0; i < 8; i++) if (mobs[i].alive) {
                mobs[i].x += mobs[i].vx;
                if (mobs[i].x < 24 || mobs[i].x > SCR_W - 24) mobs[i].vx = -mobs[i].vx;
                if (iframes <= 0 && fabsf(mobs[i].x - px) < 14 && fabsf(mobs[i].y - py) < 20) {
                    hp -= 20; iframes = 40; play_sfx(120.0f, 2800);
                    if (hp <= 0) { hp = 0; gameOver = 1; save_best(); }
                }
            }
        }

        fill_rect(0, 0, SCR_W, SCR_H, RGB(18, 16, 28));
        fill_rect(0, GROUND_Y, SCR_W, SCR_H - GROUND_Y, RGB(50, 42, 68));
        fill_rect(160, 166, 160, 8, RGB(80, 70, 110));

        char buf[64];
        if (inTitle) {
            draw_text(160, 80, "C A V E   E X P L O R E R", RGB(100, 235, 255));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(188, 120, buf, RGB(140, 255, 160));
            draw_text(148, 160, "Pulsa [X] o [START] para Explorar", RGB(255, 255, 255));
            draw_text(110, 210, "Stick: Mover | X: Saltar | []: Rayo de Energia", RGB(190, 200, 230));
        } else if (gameOver || victory) {
            draw_text(175, 95, victory ? "CUEVA CONQUISTADA!" : "DERROTADO", victory ? RGB(255,230,80) : RGB(255,70,90));
            sprintf(buf, "PUNTOS: %d  RECORD: %d", score, best);
            draw_text(148, 130, buf, RGB(255, 255, 255));
            draw_text(155, 165, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else {
            if ((room == 1 && !hasDoubleJump) || (room == 3 && !hasPowerShot)) {
                fill_rect(234, 150, 12, 12, RGB(255, 230, 60));
            }
            for (int i = 0; i < 16; i++) if (shots[i].alive) fill_rect((int)shots[i].x - 3, (int)shots[i].y - 2, 6, 4, hasPowerShot ? RGB(255, 120, 255) : RGB(0, 240, 255));
            for (int i = 0; i < 8; i++) if (mobs[i].alive) {
                int s = (room == 4) ? 22 : 10;
                fill_rect((int)mobs[i].x - s, (int)mobs[i].y - s * 2, s * 2, s * 2, room == 4 ? RGB(255, 50, 90) : RGB(120, 230, 90));
            }
            if ((iframes & 2) == 0) fill_rect((int)px - 6, (int)py - 20, 12, 20, RGB(70, 195, 255));
            sprintf(buf, "HP:%d  SALA:%d/5  PTS:%d", hp, room + 1, score);
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
