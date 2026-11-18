#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspaudio.h>
#include <pspgu.h>
#include <pspge.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"
#include "bg_arena.h"

PSP_MODULE_INFO("Chordbreaker", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define HIT_Y 236
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

static unsigned int __attribute__((aligned(16))) gulist[262144];
static void *fbp0 = (void*)0;
static void *fbp1 = (void*)(BUF_W * SCR_H * 4);
static int cur_fb = 0;

typedef struct { unsigned int color; short x, y, z; } Vtx2D;
static Vtx2D __attribute__((aligned(16))) vpool[4096];
static int vidx = 0;

static int running = 1;
static unsigned int rng = 0x77113355u;
static unsigned int xr(void) { unsigned int x = rng; x ^= x<<13; x ^= x>>17; x ^= x<<5; rng = x; return x; }
static int exit_cb(int a, int b, void *c) { (void)a; (void)b; (void)c; running = 0; return 0; }
static int cb_th(SceSize a, void *p) { (void)a; (void)p; sceKernelRegisterExitCallback(sceKernelCreateCallback("Exit", exit_cb, NULL)); sceKernelSleepThreadCB(); return 0; }

static void gu_rect(int x, int y, int w, int h, unsigned int col) {
    if (vidx + 6 > 4096) return;
    Vtx2D *v = &vpool[vidx];
    v[0] = (Vtx2D){ col, (short)x, (short)y, 0 };
    v[1] = (Vtx2D){ col, (short)(x + w), (short)y, 0 };
    v[2] = (Vtx2D){ col, (short)(x + w), (short)(y + h), 0 };
    v[3] = (Vtx2D){ col, (short)x, (short)y, 0 };
    v[4] = (Vtx2D){ col, (short)(x + w), (short)(y + h), 0 };
    v[5] = (Vtx2D){ col, (short)x, (short)(y + h), 0 };
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 6, 0, v);
    vidx += 6;
}

static void put_px(unsigned int *fb, int x, int y, unsigned int c) {
    if ((unsigned)x < SCR_W && (unsigned)y < SCR_H) fb[y * BUF_W + x] = c;
}
static void draw_text(unsigned int *fb, int x, int y, const char *s, unsigned int c) {
    while (*s) {
        unsigned char ch = (unsigned char)*s++;
        if (ch < 32 || ch > 127) ch = '?';
        const unsigned char *g = font5x7[ch - 32];
        for (int cx = 0; cx < 5; cx++) {
            unsigned char b = g[cx];
            for (int cy = 0; cy < 7; cy++) if (b & (1 << cy)) put_px(fb, x + cx, y + cy, c);
        }
        x += 6;
    }
}

/* Audio: 3 styles (0=Rock, 1=Synth, 2=Symphonic) */
static int ach = -1, ath = -1;
static volatile int arun = 0, sfx_t = 0, sfx_on = 1, curStyle = 0;
static float sfx_f = 440.0f, sfx_p = 0.0f, bgm_t = 0.0f;
static short abuf[2][1024 * 2];
static void play_sfx(float f, int dur) { if (sfx_on) { sfx_f = f; sfx_t = dur; sfx_p = 0.0f; } }

static int audio_th(SceSize a, void *p) {
    (void)a; (void)p;
    int cur = 0;
    const float notes[3][8] = {
        { 82.41f, 82.41f, 98.00f, 110.00f, 82.41f, 123.47f, 110.00f, 98.00f }, /* Rock */
        { 130.81f, 196.00f, 261.63f, 196.00f, 146.83f, 220.00f, 293.66f, 220.00f }, /* Synth */
        { 110.00f, 130.81f, 164.81f, 220.00f, 196.00f, 164.81f, 146.83f, 130.81f }  /* Symphonic */
    };
    while (arun) {
        short *o = abuf[cur];
        for (int i = 0; i < 1024; i++) {
            float m = 0.0f;
            if (sfx_on) {
                int st = (int)(bgm_t * 4.5f);
                float bf = notes[curStyle][st & 7];
                float sub = bgm_t * 4.5f - floorf(bgm_t * 4.5f);
                float bt = bgm_t * bf - floorf(bgm_t * bf);
                m += (curStyle == 2 ? sinf(bt * 6.283f) : (2.0f * bt - 1.0f)) * 0.11f * expf(-sub * 2.0f);
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

typedef struct { int lane; float y; int active; } Note;
static Note notes[24];
static const char *bossNames[3] = { "GOLEM DE HIERRO", "AUTOMATA ARCANO", "DRAGON SOMBRIO" };
static const char *styleNames[3] = { "ROCK", "SYNTH", "SINFONICO" };
static const int bossWeak[3] = { 0, 1, 2 };

static int chapter = 0, pHp = 100, pMaxHp = 100, bHp = 120, bMaxHp = 120;
static int combo = 0, score = 0, best = 0, tick = 0;
static int relicDmg = 0, relicVamp = 0, relicShield = 0;
static int inTitle = 1, inStory = 0, inRelic = 0, gameOver = 0, victory = 0;
static char judge[32] = "";
static int judgeT = 0;

static void load_best(void) { FILE *f = fopen("cb_save.dat", "rb"); if (f) { int v=0; if (fread(&v,4,1,f)==1 && v>0) best=v; fclose(f); } }
static void save_best(void) { if (score > best) { best = score; FILE *f = fopen("cb_save.dat", "wb"); if (f) { fwrite(&best,4,1,f); fclose(f); } } }

static void start_chapter(int ch) {
    chapter = ch;
    bHp = bMaxHp = 120 + ch * 60;
    combo = 0; tick = 0; judgeT = 0;
    memset(notes, 0, sizeof(notes));
    inStory = 1;
}

static void start_game(void) {
    pHp = pMaxHp = 100; score = 0;
    relicDmg = 0; relicVamp = 0; relicShield = 0;
    inTitle = 0; gameOver = 0; victory = 0; inRelic = 0;
    start_chapter(0);
    play_sfx(660.0f, 2000);
}

int main(void) {
    int th = sceKernelCreateThread("cb", cb_th, 0x11, 0xFA0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    sceGuInit();
    sceGuStart(GU_DIRECT, gulist);
    sceGuDrawBuffer(GU_PSM_8888, fbp0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, fbp1, BUF_W);
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
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;
        if (pressed & PSP_CTRL_SELECT) sfx_on = !sfx_on;

        if (inTitle || gameOver || victory) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        } else if (inStory) {
            if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) { inStory = 0; play_sfx(520.0f, 1200); }
        } else if (inRelic) {
            if (pressed & PSP_CTRL_SQUARE) { relicDmg += 4; inRelic = 0; start_chapter(chapter + 1); }
            else if (pressed & PSP_CTRL_TRIANGLE) { relicVamp += 3; inRelic = 0; start_chapter(chapter + 1); }
            else if (pressed & PSP_CTRL_CIRCLE) { relicShield = 1; pHp = pMaxHp; inRelic = 0; start_chapter(chapter + 1); }
        } else {
            if (pressed & PSP_CTRL_LTRIGGER) { curStyle = (curStyle + 2) % 3; play_sfx(440.0f, 600); }
            if (pressed & PSP_CTRL_RTRIGGER) { curStyle = (curStyle + 1) % 3; play_sfx(440.0f, 600); }
            if (judgeT > 0) judgeT--;

            tick++;
            int rate = 26 - chapter * 3;
            if ((tick % rate) == 0) {
                for (int i = 0; i < 24; i++) if (!notes[i].active) {
                    notes[i].active = 1; notes[i].lane = xr() & 3; notes[i].y = 120; break;
                }
            }

            for (int l = 0; l < 4; l++) if (pressed & laneBtns[l]) {
                int hit = -1; float bestD = 26.0f;
                for (int i = 0; i < 24; i++) if (notes[i].active && notes[i].lane == l) {
                    float d = fabsf(notes[i].y - HIT_Y);
                    if (d < bestD) { bestD = d; hit = i; }
                }
                if (hit >= 0) {
                    notes[hit].active = 0; combo++;
                    int isPerf = (bestD < 10.0f);
                    int base = (isPerf ? 10 : 5) + relicDmg + (combo / 4);
                    if (curStyle == bossWeak[chapter]) base *= 2;
                    bHp -= base;
                    score += base * 10;
                    if (isPerf && relicVamp > 0) { pHp += relicVamp; if (pHp > pMaxHp) pHp = pMaxHp; }
                    snprintf(judge, sizeof(judge), isPerf ? "PERFECT! -%d" : "GOOD -%d", base);
                    judgeT = 28;
                    play_sfx(523.25f + l * 75.0f, 800);
                    if (bHp <= 0) {
                        bHp = 0;
                        if (chapter < 2) inRelic = 1;
                        else { victory = 1; save_best(); }
                    }
                }
            }

            float spd = 2.1f + chapter * 0.35f;
            for (int i = 0; i < 24; i++) if (notes[i].active) {
                notes[i].y += spd;
                if (notes[i].y > SCR_H) {
                    notes[i].active = 0; combo = 0;
                    int edmg = relicShield ? 5 : 9;
                    pHp -= edmg;
                    snprintf(judge, sizeof(judge), "MISS! -%d HP", edmg);
                    judgeT = 28;
                    play_sfx(140.0f, 1200);
                    if (pHp <= 0) { pHp = 0; gameOver = 1; save_best(); }
                }
            }
        }

        unsigned int *fb = (unsigned int*)(0x44000000 + (cur_fb ? (BUF_W * SCR_H * 4) : 0));
        for (int y = 0; y < SCR_H; y++) memcpy(&fb[y * BUF_W], &bg_arena[y * SCR_W], SCR_W * 4);

        vidx = 0;
        sceGuStart(GU_DIRECT, gulist);

        if (inTitle || gameOver || victory || inStory || inRelic) {
            gu_rect(70, 58, 340, 156, RGB(14, 12, 28));
        } else {
            /* HP Bars */
            gu_rect(14, 12, 160, 12, RGB(30, 20, 30));
            gu_rect(14, 12, (pHp * 160) / pMaxHp, 12, RGB(60, 210, 255));
            gu_rect(306, 12, 160, 12, RGB(30, 20, 30));
            gu_rect(306, 12, (bHp * 160) / bMaxHp, 12, RGB(255, 50, 80));

            /* 4-Lane Rhythm Highway */
            gu_rect(144, 118, 192, 154, RGB(12, 10, 24));
            unsigned int lcols[4] = { RGB(255,80,180), RGB(80,255,140), RGB(255,70,70), RGB(0,220,255) };
            for (int l = 0; l < 4; l++) {
                gu_rect(148 + l * 46, HIT_Y - 4, 40, 8, lcols[l]);
            }
            for (int i = 0; i < 24; i++) if (notes[i].active) {
                gu_rect(152 + notes[i].lane * 46, (int)notes[i].y - 5, 32, 10, lcols[notes[i].lane]);
            }
        }

        sceGuFinish();
        sceGuSync(0, 0);

        char buf[64];
        if (inTitle) {
            draw_text(135, 76, "C H O R D B R E A K E R :  A R I A", RGB(255, 225, 80));
            draw_text(145, 96, "32-BIT GUITAR HERO + RPG ROGUELIKE", RGB(0, 235, 255));
            sprintf(buf, "RECORD: %d PTS", best);
            draw_text(190, 126, buf, RGB(140, 255, 160));
            draw_text(148, 160, "Pulsa [X] o [START] para Tocar!", RGB(255, 255, 255));
            draw_text(98, 186, "L/R: Cambiar Estilo | [] Tri O X: Notas", RGB(190, 200, 230));
        } else if (inStory) {
            sprintf(buf, "CAPITULO %d: %s", chapter + 1, bossNames[chapter]);
            draw_text(135, 84, buf, RGB(255, 225, 80));
            sprintf(buf, "DEBILIDAD MUSICAL: %s", styleNames[bossWeak[chapter]]);
            draw_text(145, 114, buf, RGB(0, 235, 255));
            draw_text(120, 144, "Usa L/R para elegir su estilo debil!", RGB(255, 255, 255));
            draw_text(155, 178, "Pulsa [X] para Comenzar", RGB(140, 255, 160));
        } else if (inRelic) {
            draw_text(165, 78, "BOSS DERROTADO! ELIGE RELIQUIA:", RGB(255, 225, 80));
            draw_text(105, 110, "[[]] Amplificador (+4 Dano por Nota)", RGB(255, 120, 200));
            draw_text(105, 136, "[Tri] Pua Vampirica (+3 HP por Perfect)", RGB(120, 255, 140));
            draw_text(105, 162, "[O] Escudo Ritmico (-40% Dano + Cura)", RGB(100, 220, 255));
        } else if (gameOver || victory) {
            draw_text(175, 92, victory ? "LEYENDA DEL ROCK!" : "RITMO ROTO...", victory ? RGB(255,230,80) : RGB(255,70,90));
            sprintf(buf, "PUNTOS: %d   RECORD: %d", score, best);
            draw_text(148, 128, buf, RGB(255, 255, 255));
            draw_text(155, 164, "Pulsa [X] para Reintentar", RGB(160, 225, 255));
        } else {
            sprintf(buf, "HP:%d", pHp); draw_text(16, 28, buf, RGB(140, 230, 255));
            draw_text(306, 28, bossNames[chapter], RGB(255, 140, 140));
            sprintf(buf, "ESTILO:%s  COMBO:x%d", styleNames[curStyle], combo);
            draw_text(165, 102, buf, curStyle == bossWeak[chapter] ? RGB(100, 255, 140) : RGB(255, 235, 100));
            if (judgeT > 0) draw_text(190, 122, judge, RGB(255, 255, 255));
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
