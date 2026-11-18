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

static void draw_car_3d(float x, float y, float z, float steer, unsigned int bodyCol, int isPlayer) {
    sceGumPushMatrix();
    ScePspFVector3 pos = { x, y, z };
    sceGumTranslate(&pos);
    sceGumRotateY(steer);

    unsigned int dark = RGB(25, 30, 45);
    unsigned int glass = RGB(90, 220, 255);
    unsigned int tail = (isPlayer && player.nitroActive) ? RGB(80, 220, 255) : RGB(255, 50, 70);

    /* Main chassis */
    draw_quad(-0.85f, 0.25f, -1.4f,  0.85f, 0.25f, -1.4f,  0.95f, 0.35f, 1.3f, -0.95f, 0.35f, 1.3f, bodyCol);
    /* Canopy */
    draw_quad(-0.55f, 0.35f, -0.4f,  0.55f, 0.35f, -0.4f,  0.65f, 0.65f, 0.7f, -0.65f, 0.65f, 0.7f, glass);
    /* Rear bumper & tail lights */
    draw_quad(-0.95f, 0.05f, 1.3f,   0.95f, 0.05f, 1.3f,   0.95f, 0.42f, 1.3f, -0.95f, 0.42f, 1.3f, dark);
    draw_quad(-0.80f, 0.18f, 1.32f, -0.35f, 0.18f, 1.32f, -0.35f, 0.32f, 1.32f, -0.80f, 0.32f, 1.32f, tail);
    draw_quad( 0.35f, 0.18f, 1.32f,  0.80f, 0.18f, 1.32f,  0.80f, 0.32f, 1.32f,  0.35f, 0.32f, 1.32f, tail);

    sceGumPopMatrix();
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
    sceGuClearColor(RGB(10, 6, 26));
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(65.0f, (float)SCR_W / (float)SCR_H, 0.5f, 140.0f);

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    float shakeX = (ui.shakeTimer > 0) ? ((ui.shakeTimer & 1) ? 0.15f : -0.15f) : 0.0f;
    ScePspFVector3 eye = { player.x * 3.8f + shakeX, 2.3f, 4.2f };
    ScePspFVector3 center = { player.x * 2.2f, 0.7f, -18.0f };
    ScePspFVector3 up = { 0.0f, 1.0f, 0.0f };
    sceGumLookAt(&eye, &center, &up);

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    /* Synthwave Horizon Sun */
    draw_quad(-14.0f, 2.0f, -85.0f, 14.0f, 2.0f, -85.0f, 14.0f, 18.0f, -85.0f, -14.0f, 18.0f, -85.0f, RGB(255, 70, 140));

    /* 3D Road & Curbs */
    float baseZ = fmodf(player.z, 3.0f);
    for (int i = 0; i < 32; i++) {
        float z0 = -i * 3.0f + baseZ;
        float z1 = z0 - 3.0f;
        float curve0 = sinf((player.z + i * 3.0f) * 0.035f) * 2.4f;
        float curve1 = sinf((player.z + (i + 1) * 3.0f) * 0.035f) * 2.4f;
        unsigned int roadCol = (i & 1) ? RGB(24, 24, 38) : RGB(30, 30, 48);
        unsigned int curbCol = (i & 1) ? RGB(0, 230, 255) : RGB(255, 40, 180);

        draw_quad(-6.0f + curve0, 0.0f, z0,  6.0f + curve0, 0.0f, z0,  6.0f + curve1, 0.0f, z1, -6.0f + curve1, 0.0f, z1, roadCol);
        draw_quad(-6.6f + curve0, 0.0f, z0, -6.0f + curve0, 0.0f, z0, -6.0f + curve1, 0.0f, z1, -6.6f + curve1, 0.0f, z1, curbCol);
        draw_quad( 6.0f + curve0, 0.0f, z0,  6.6f + curve0, 0.0f, z0,  6.6f + curve1, 0.0f, z1,  6.0f + curve1, 0.0f, z1, curbCol);

        if ((i % 2) == 0) {
            draw_quad(-0.18f + curve0, 0.02f, z0, 0.18f + curve0, 0.02f, z0, 0.18f + curve1, 0.02f, z1, -0.18f + curve1, 0.02f, z1, RGB(220, 230, 255));
        }
        if ((i % 4) == 0) {
            draw_quad(-8.0f + curve0, 0.0f, z0, -7.7f + curve0, 0.0f, z0, -7.7f + curve0, 3.8f, z0, -8.0f + curve0, 3.8f, z0, RGB(0, 200, 255));
            draw_quad( 7.7f + curve0, 0.0f, z0,  8.0f + curve0, 0.0f, z0,  8.0f + curve0, 3.8f, z0,  7.7f + curve0, 3.8f, z0, RGB(255, 50, 190));
        }
    }

    /* Traffic */
    for (int i = 0; i < MAX_TRAFFIC; i++) {
        if (!traffic[i].alive) continue;
        float relZ = -(traffic[i].z - player.z);
        if (relZ < -90.0f || relZ > 4.0f) continue;
        float curve = sinf(traffic[i].z * 0.035f) * 2.4f;
        draw_car_3d(traffic[i].x * 4.5f + curve, 0.0f, relZ, 0.0f, traffic[i].color, 0);
    }

    /* Player Car */
    draw_car_3d(player.x * 4.5f, 0.0f, -1.5f, player.steer, RGB(0, 210, 255), 1);

    /* Sparks */
    for (int i = 0; i < MAX_SPARKS; i++) {
        if (sparks[i].life > 0) {
            float s = 0.12f;
            draw_quad(sparks[i].x - s, sparks[i].y, sparks[i].z,
                      sparks[i].x + s, sparks[i].y, sparks[i].z,
                      sparks[i].x + s, sparks[i].y + s, sparks[i].z,
                      sparks[i].x - s, sparks[i].y + s, sparks[i].z, sparks[i].color);
        }
    }

    sceGuFinish();
    sceGuSync(0, 0);

    unsigned int *fb = (unsigned int*)(0x44000000 + (cur_fb ? (BUF_W * SCR_H * 4) : 0));
    char buf[64];

    if (ui.inTitle) {
        fill_rect(fb, 100, 24, 280, 46, RGB(14, 10, 32));
        draw_outline(fb, 100, 24, 280, 46, RGB(0, 230, 255));
        draw_text(fb, 148, 34, "C Y B E R   D R I F T   P S P", RGB(255, 220, 90));
        draw_text(fb, 144, 50, "3D HARDWARE NIGHT RACING", RGB(0, 230, 255));
        sprintf(buf, "RECORD: %d PTS", player.bestScore);
        draw_text(fb, 185, 198, buf, RGB(140, 255, 160));
        draw_text(fb, 148, 222, "Pulsa [X] o [START] para Correr!", RGB(255, 255, 255));
        draw_text(fb, 86, 248, "X: Acelerar | O: Freno/Drift | []: Nitro | L/R: Drift", RGB(180, 200, 230));
    } else if (ui.finished) {
        fill_rect(fb, 95, 76, 290, 110, RGB(14, 10, 32));
        draw_outline(fb, 95, 76, 290, 110, RGB(0, 230, 255));
        draw_text(fb, 175, 96, "CARRERA COMPLETADA!", RGB(255, 230, 90));
        sprintf(buf, "PUNTOS: %d   RECORD: %d", player.score, player.bestScore);
        draw_text(fb, 138, 126, buf, RGB(230, 240, 255));
        draw_text(fb, 145, 156, "Pulsa [X] para volver al Menu", RGB(160, 225, 255));
    } else {
        fill_rect(fb, 6, 6, 138, 28, RGB(12, 12, 28));
        draw_outline(fb, 6, 6, 138, 28, RGB(0, 210, 255));
        sprintf(buf, "%3d KM/H", (int)player.speed);
        draw_text(fb, 12, 11, buf, RGB(255, 255, 255));
        draw_text(fb, 12, 22, "N2O", RGB(0, 230, 255));
        int nw = (int)player.nitro;
        if (nw < 0) nw = 0; if (nw > 100) nw = 100;
        fill_rect(fb, 36, 22, 100, 6, RGB(30, 34, 52));
        fill_rect(fb, 36, 22, nw, 6, player.nitroActive ? RGB(255, 80, 220) : RGB(0, 220, 255));

        sprintf(buf, "VUELTA %d/3 | PTS:%d", player.lap, player.score);
        draw_text(fb, 320, 10, buf, RGB(255, 230, 100));
        if (ui.msgTimer > 0) draw_text(fb, 12, 254, ui.msg, RGB(120, 255, 180));
    }

    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    cur_fb ^= 1;
}
