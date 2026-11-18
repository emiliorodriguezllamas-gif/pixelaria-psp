#include <pspgu.h>
#include <pspgum.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "game.h"
#include "font5x7.h"

#define BUF_W 512
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

static unsigned int __attribute__((aligned(16))) list[262144];
static void *fbp0 = (void*)0;
static void *fbp1 = (void*)(BUF_W * SCR_H * 4);
static void *zbp  = (void*)(BUF_W * SCR_H * 8);
static int cur_fb = 0;

typedef struct {
    unsigned int color;
    float x, y, z;
} Vtx3D;

static Vtx3D __attribute__((aligned(16))) vpool[4096];
static int vidx = 0;

static void put_px(unsigned int *fb, int x, int y, unsigned int col) {
    if ((unsigned)x < SCR_W && (unsigned)y < SCR_H) fb[y * BUF_W + x] = col;
}

static void fill_rect(unsigned int *fb, int x, int y, int w, int h, unsigned int col) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x;
    if (y + h > SCR_H) h = SCR_H - y;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            fb[(y + j) * BUF_W + (x + i)] = col;
}

static void draw_outline(unsigned int *fb, int x, int y, int w, int h, unsigned int col) {
    fill_rect(fb, x, y, w, 1, col);
    fill_rect(fb, x, y + h - 1, w, 1, col);
    fill_rect(fb, x, y, 1, h, col);
    fill_rect(fb, x + w - 1, y, 1, h, col);
}

static void draw_text(unsigned int *fb, int x, int y, const char *s, unsigned int col) {
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c < 32 || c > 127) c = '?';
        const unsigned char *g = font5x7[c - 32];
        for (int cx = 0; cx < 5; cx++) {
            unsigned char colb = g[cx];
            for (int cy = 0; cy < 7; cy++) {
                if (colb & (1 << cy)) put_px(fb, x + cx, y + cy, col);
            }
        }
        x += 6;
    }
}

static void draw_quad(float x0, float y0, float z0,
                      float x1, float y1, float z1,
                      float x2, float y2, float z2,
                      float x3, float y3, float z3, unsigned int col) {
    if (vidx + 6 > 4096) return;
    Vtx3D *v = &vpool[vidx];
    v[0] = (Vtx3D){ col, x0, y0, z0 };
    v[1] = (Vtx3D){ col, x1, y1, z1 };
    v[2] = (Vtx3D){ col, x2, y2, z2 };
    v[3] = (Vtx3D){ col, x0, y0, z0 };
    v[4] = (Vtx3D){ col, x2, y2, z2 };
    v[5] = (Vtx3D){ col, x3, y3, z3 };
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 6, 0, v);
    vidx += 6;
}

void render3d_init(void) {
    sceGuInit();
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBuffer(GU_PSM_8888, fbp0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, fbp1, BUF_W);
    sceGuDepthBuffer(zbp, BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_SMOOTH);
    sceGuDisable(GU_CULL_FACE);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

void render3d_shutdown(void) {
    sceGuTerm();
}

void render3d_frame(void) {
    vidx = 0;
    sceGuStart(GU_DIRECT, list);
    sceGuClearColor(RGB(6, 6, 18));
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(72.0f, (float)SCR_W / (float)SCR_H, 0.4f, 120.0f);

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    float shake = (ui.shakeTimer > 0) ? ((ui.shakeTimer & 1) ? 0.16f : -0.16f) : 0.0f;
    float camR = 2.8f;
    ScePspFVector3 eye = { cosf(player.angle) * camR + shake, sinf(player.angle) * camR, 2.0f };
    ScePspFVector3 center = { 0.0f, 0.0f, -25.0f };
    ScePspFVector3 up = { -cosf(player.angle), -sinf(player.angle), 0.0f };
    sceGumLookAt(&eye, &center, &up);

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    /* Octagonal Neon Tunnel */
    float baseZ = fmodf(player.z, 4.0f);
    float R = 4.6f;
    for (int s = 0; s < 22; s++) {
        float z0 = -s * 4.0f + baseZ;
        float z1 = z0 - 4.0f;
        for (int k = 0; k < 8; k++) {
            float a0 = k * (6.28318f / 8.0f);
            float a1 = (k + 1) * (6.28318f / 8.0f);
            unsigned int col = ((s + k) & 1) ? RGB(18, 22, 52) : RGB(28, 36, 82);
            draw_quad(cosf(a0)*R, sinf(a0)*R, z0, cosf(a1)*R, sinf(a1)*R, z0,
                      cosf(a1)*R, sinf(a1)*R, z1, cosf(a0)*R, sinf(a0)*R, z1, col);
        }
    }

    /* Obstacles */
    for (int i = 0; i < MAX_OBS; i++) {
        if (!obs[i].active) continue;
        float rz = -(obs[i].z - player.z);
        if (rz < -85.0f || rz > 3.0f) continue;
        float a0 = obs[i].angle - obs[i].arc;
        float a1 = obs[i].angle + obs[i].arc;
        float rIn = 1.8f, rOut = 4.5f;
        draw_quad(cosf(a0)*rIn, sinf(a0)*rIn, rz, cosf(a1)*rIn, sinf(a1)*rIn, rz,
                  cosf(a1)*rOut, sinf(a1)*rOut, rz, cosf(a0)*rOut, sinf(a0)*rOut, rz, obs[i].color);
    }

    sceGuFinish();
    sceGuSync(0, 0);

    unsigned int *fb = (unsigned int*)(0x44000000 + (cur_fb ? (BUF_W * SCR_H * 4) : 0));
    char buf[64];

    if (ui.inTitle) {
        fill_rect(fb, 100, 24, 280, 46, RGB(12, 12, 32));
        draw_outline(fb, 100, 24, 280, 46, RGB(0, 240, 255));
        draw_text(fb, 146, 34, "T U N N E L   R U S H   X", RGB(255, 225, 80));
        draw_text(fb, 142, 50, "3D HARDWARE HYPER TUNNEL", RGB(0, 240, 255));
        sprintf(buf, "RECORD: %d PTS", player.bestScore);
        draw_text(fb, 185, 198, buf, RGB(140, 255, 160));
        draw_text(fb, 148, 222, "Pulsa [X] o [START] para Entrar!", RGB(255, 255, 255));
        draw_text(fb, 86, 248, "Stick / L / R: Girar por el Tunel | X: Turbo", RGB(190, 210, 240));
    } else if (ui.gameOver) {
        fill_rect(fb, 95, 76, 290, 110, RGB(12, 12, 32));
        draw_outline(fb, 95, 76, 290, 110, RGB(255, 50, 80));
        draw_text(fb, 185, 96, "IMPACTO FATAL!", RGB(255, 70, 90));
        sprintf(buf, "PUNTOS: %d   RECORD: %d", player.score, player.bestScore);
        draw_text(fb, 138, 126, buf, RGB(255, 255, 255));
        draw_text(fb, 145, 156, "Pulsa [X] para volver al Menu", RGB(160, 225, 255));
    } else {
        sprintf(buf, "VEL: %d  |  PTS: %d", (int)(player.speed * 100.0f), player.score);
        draw_text(fb, 12, 10, buf, RGB(0, 240, 255));
        if (ui.msgTimer > 0) draw_text(fb, 12, 254, ui.msg, RGB(255, 240, 120));
    }

    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    cur_fb ^= 1;
}
