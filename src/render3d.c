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

static void draw_kart(float x, float z, float steer, unsigned int col, int shield) {
    sceGumPushMatrix();
    ScePspFVector3 pos = { x, 0.0f, z };
    sceGumTranslate(&pos);
    sceGumRotateY(steer);

    /* Kart body */
    draw_quad(-0.7f, 0.25f, -0.9f,  0.7f, 0.25f, -0.9f,  0.85f, 0.35f, 0.9f, -0.85f, 0.35f, 0.9f, col);
    /* Driver helmet */
    draw_quad(-0.28f, 0.35f, -0.1f, 0.28f, 0.35f, -0.1f, 0.28f, 0.78f, 0.2f, -0.28f, 0.78f, 0.2f, RGB(255, 240, 220));
    /* Rear spoiler */
    draw_quad(-0.75f, 0.45f, 0.85f, 0.75f, 0.45f, 0.85f, 0.75f, 0.62f, 0.95f, -0.75f, 0.62f, 0.95f, RGB(255, 220, 60));
    if (shield) {
        draw_quad(-1.1f, 0.1f, -1.1f, 1.1f, 0.1f, -1.1f, 1.1f, 1.1f, -1.1f, -1.1f, 1.1f, -1.1f, RGB(80, 240, 255));
    }
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
    sceGuClearColor(RGB(65, 145, 245));
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(65.0f, (float)SCR_W / (float)SCR_H, 0.5f, 130.0f);

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    float shake = (ui.shakeTimer > 0) ? ((ui.shakeTimer & 1) ? 0.18f : -0.18f) : 0.0f;
    ScePspFVector3 eye = { player.x * 3.6f + shake, 2.5f, 4.0f };
    ScePspFVector3 center = { player.x * 2.0f, 0.6f, -18.0f };
    ScePspFVector3 up = { 0.0f, 1.0f, 0.0f };
    sceGumLookAt(&eye, &center, &up);

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    /* Track & Grass */
    float baseZ = fmodf(player.z, 3.0f);
    for (int i = 0; i < 30; i++) {
        float z0 = -i * 3.0f + baseZ;
        float z1 = z0 - 3.0f;
        float c0 = sinf((player.z + i * 3.0f) * 0.04f) * 2.2f;
        float c1 = sinf((player.z + (i + 1) * 3.0f) * 0.04f) * 2.2f;
        unsigned int grass = (i & 1) ? RGB(45, 155, 55) : RGB(55, 175, 65);
        unsigned int road  = (i & 1) ? RGB(70, 74, 84) : RGB(80, 84, 94);
        unsigned int curb  = (i & 1) ? RGB(230, 40, 40) : RGB(245, 245, 245);

        draw_quad(-24.0f, -0.05f, z0, 24.0f, -0.05f, z0, 24.0f, -0.05f, z1, -24.0f, -0.05f, z1, grass);
        draw_quad(-5.5f + c0, 0.0f, z0, 5.5f + c0, 0.0f, z0, 5.5f + c1, 0.0f, z1, -5.5f + c1, 0.0f, z1, road);
        draw_quad(-6.1f + c0, 0.0f, z0, -5.5f + c0, 0.0f, z0, -5.5f + c1, 0.0f, z1, -6.1f + c1, 0.0f, z1, curb);
        draw_quad( 5.5f + c0, 0.0f, z0,  6.1f + c0, 0.0f, z0,  6.1f + c1, 0.0f, z1,  5.5f + c1, 0.0f, z1, curb);
    }

    /* Item Boxes */
    for (int i = 0; i < MAX_BOXES; i++) {
        if (!boxes[i].active) continue;
        float rz = -(boxes[i].z - player.z);
        if (rz < -85.0f || rz > 3.0f) continue;
        float c = sinf(boxes[i].z * 0.04f) * 2.2f;
        float bx = boxes[i].x * 4.2f + c;
        draw_quad(bx - 0.55f, 0.25f, rz, bx + 0.55f, 0.25f, rz, bx + 0.55f, 1.35f, rz, bx - 0.55f, 1.35f, rz, RGB(255, 210, 40));
    }

    /* Rivals */
    for (int i = 0; i < MAX_RIVALS; i++) {
        float rz = -(rivals[i].z - player.z);
        if (rz < -85.0f || rz > 3.0f) continue;
        float c = sinf(rivals[i].z * 0.04f) * 2.2f;
        draw_kart(rivals[i].x * 4.2f + c, rz, rivals[i].spinTimer ? (rivals[i].spinTimer * 0.4f) : 0.0f, rivals[i].color, 0);
    }

    /* Player Kart */
    draw_kart(player.x * 4.2f, -1.5f, player.steer, RGB(235, 50, 50), player.shieldTimer > 0);

    sceGuFinish();
    sceGuSync(0, 0);

    unsigned int *fb = (unsigned int*)(0x44000000 + (cur_fb ? (BUF_W * SCR_H * 4) : 0));
    char buf[64];

    if (ui.inTitle) {
        fill_rect(fb, 100, 24, 280, 46, RGB(18, 28, 52));
        draw_outline(fb, 100, 24, 280, 46, RGB(255, 220, 60));
        draw_text(fb, 156, 34, "P O L Y - K A R T   6 4", RGB(255, 230, 80));
        draw_text(fb, 146, 50, "3D HARDWARE KART RACING", RGB(100, 230, 255));
        sprintf(buf, "RECORD: %d PTS", player.bestScore);
        draw_text(fb, 185, 198, buf, RGB(255, 255, 160));
        draw_text(fb, 148, 222, "Pulsa [X] o [START] para Correr!", RGB(255, 255, 255));
        draw_text(fb, 80, 248, "X: Acelerar | []: Usar Objeto | L/R: Derrapar", RGB(220, 240, 255));
    } else if (ui.finished) {
        fill_rect(fb, 95, 76, 290, 110, RGB(18, 28, 52));
        draw_outline(fb, 95, 76, 290, 110, RGB(255, 220, 60));
        draw_text(fb, 185, 96, "META ALCANZADA!", RGB(255, 230, 80));
        sprintf(buf, "PUNTOS: %d   RECORD: %d", player.score, player.bestScore);
        draw_text(fb, 138, 126, buf, RGB(255, 255, 255));
        draw_text(fb, 145, 156, "Pulsa [X] para volver al Menu", RGB(160, 225, 255));
    } else {
        int pos = 1;
        for (int i = 0; i < MAX_RIVALS; i++) if (rivals[i].z > player.z) pos++;
        fill_rect(fb, 6, 6, 136, 28, RGB(18, 24, 44));
        draw_outline(fb, 6, 6, 136, 28, RGB(255, 220, 60));
        sprintf(buf, "POS %d/4  %3d KM/H", pos, (int)player.speed);
        draw_text(fb, 12, 11, buf, RGB(255, 255, 255));
        const char *iname = (player.item == ITEM_TURBO) ? "TURBO" : (player.item == ITEM_ROCKET ? "COHETE" : (player.item == ITEM_SHIELD ? "ESCUDO" : "NINGUNO"));
        sprintf(buf, "OBJ: %s", iname);
        draw_text(fb, 12, 22, buf, RGB(255, 230, 90));

        sprintf(buf, "VUELTA %d/3 | PTS:%d", player.lap, player.score);
        draw_text(fb, 320, 10, buf, RGB(255, 255, 255));
        if (ui.msgTimer > 0) draw_text(fb, 12, 254, ui.msg, RGB(255, 240, 120));
    }

    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    cur_fb ^= 1;
}
