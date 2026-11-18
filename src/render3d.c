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

static void draw_plane(float x, float y, float z, float roll, float pitch, float scale, unsigned int bodyCol, unsigned int wingCol) {
    sceGumPushMatrix();
    ScePspFVector3 pos = { x, y, z };
    sceGumTranslate(&pos);
    sceGumRotateZ(roll);
    sceGumRotateX(pitch);

    /* Fuselage */
    draw_quad(-0.22f * scale, 0.0f, -1.2f * scale,  0.22f * scale, 0.0f, -1.2f * scale,
               0.28f * scale, 0.0f,  1.1f * scale, -0.28f * scale, 0.0f,  1.1f * scale, bodyCol);
    /* Wings */
    draw_quad(-1.6f * scale, 0.0f, -0.2f * scale,  1.6f * scale, 0.0f, -0.2f * scale,
               1.4f * scale, 0.0f,  0.4f * scale, -1.4f * scale, 0.0f,  0.4f * scale, wingCol);
    /* Tail fin */
    draw_quad(0.0f, 0.0f, 0.6f * scale, 0.0f, 0.0f, 1.1f * scale,
              0.0f, 0.55f * scale, 1.1f * scale, 0.0f, 0.35f * scale, 0.7f * scale, RGB(255, 220, 60));

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
    sceGuClearColor(RGB(75, 165, 245));
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(65.0f, (float)SCR_W / (float)SCR_H, 0.5f, 140.0f);

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    float shake = (ui.shakeTimer > 0) ? ((ui.shakeTimer & 1) ? 0.16f : -0.16f) : 0.0f;
    ScePspFVector3 eye = { player.x * 0.4f + shake, player.y * 0.35f + 2.2f, 5.2f };
    ScePspFVector3 center = { player.x * 0.25f, player.y * 0.25f, -20.0f };
    ScePspFVector3 up = { 0.0f, 1.0f, 0.0f };
    sceGumLookAt(&eye, &center, &up);

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    /* Ocean & Islands */
    float baseZ = fmodf(player.z, 6.0f);
    for (int i = 0; i < 18; i++) {
        float z0 = -i * 6.0f + baseZ;
        float z1 = z0 - 6.0f;
        unsigned int sea = (i & 1) ? RGB(20, 85, 185) : RGB(24, 98, 205);
        draw_quad(-32.0f, -6.0f, z0, 32.0f, -6.0f, z0, 32.0f, -6.0f, z1, -32.0f, -6.0f, z1, sea);
        if ((i % 5) == 0) {
            float ix = ((i & 2) ? -10.0f : 10.0f);
            draw_quad(ix - 3.5f, -5.9f, z0, ix + 3.5f, -5.9f, z0, ix + 3.5f, -5.9f, z1, ix - 3.5f, -5.9f, z1, RGB(50, 165, 65));
        }
    }

    /* Player Plane */
    if (!ui.inTitle && !ui.gameOver) {
        if ((player.iframes & 2) == 0) {
            draw_plane(player.x, player.y, 0.0f, player.roll, player.pitch, 1.0f, RGB(230, 235, 245), RGB(40, 120, 240));
        }
    }

    /* Enemy Planes */
    for (int i = 0; i < MAX_ENEMIES; i++) {
        EnemyPlane *e = &enemies[i];
        if (!e->alive) continue;
        unsigned int bc = e->hitFlash ? RGB(255, 255, 255) : (e->isBoss ? RGB(180, 40, 50) : RGB(70, 85, 70));
        unsigned int wc = e->hitFlash ? RGB(255, 255, 255) : (e->isBoss ? RGB(220, 70, 70) : RGB(110, 130, 110));
        draw_plane(e->x, e->y, e->z, 0.0f, 0.0f, e->isBoss ? 2.1f : 0.95f, bc, wc);
    }

    /* Bullets */
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].alive) continue;
        unsigned int col = bullets[i].fromPlayer ? (bullets[i].isMissile ? RGB(255, 230, 60) : RGB(0, 240, 255)) : RGB(255, 65, 65);
        float s = bullets[i].isMissile ? 0.22f : 0.14f;
        draw_quad(bullets[i].x - s, bullets[i].y - s, bullets[i].z,
                  bullets[i].x + s, bullets[i].y - s, bullets[i].z,
                  bullets[i].x + s, bullets[i].y + s, bullets[i].z,
                  bullets[i].x - s, bullets[i].y + s, bullets[i].z, col);
    }

    /* Particles */
    for (int i = 0; i < MAX_PARTS; i++) {
        if (parts[i].life > 0) {
            float s = 0.22f;
            draw_quad(parts[i].x - s, parts[i].y - s, parts[i].z,
                      parts[i].x + s, parts[i].y - s, parts[i].z,
                      parts[i].x + s, parts[i].y + s, parts[i].z,
                      parts[i].x - s, parts[i].y + s, parts[i].z, parts[i].color);
        }
    }

    sceGuFinish();
    sceGuSync(0, 0);

    unsigned int *fb = (unsigned int*)(0x44000000 + (cur_fb ? (BUF_W * SCR_H * 4) : 0));
    char buf[64];

    if (ui.inTitle) {
        fill_rect(fb, 100, 24, 280, 46, RGB(14, 24, 48));
        draw_outline(fb, 100, 24, 280, 46, RGB(255, 220, 60));
        draw_text(fb, 152, 34, "S K Y   A C E   1 9 4 5", RGB(255, 230, 90));
        draw_text(fb, 146, 50, "3D HARDWARE AIR COMBAT", RGB(100, 230, 255));
        sprintf(buf, "RECORD: %d PTS", player.bestScore);
        draw_text(fb, 185, 198, buf, RGB(255, 255, 160));
        draw_text(fb, 145, 222, "Pulsa [X] o [START] para Despegar!", RGB(255, 255, 255));
        draw_text(fb, 86, 248, "X: Ametralladora | Tri: Misil | L/R: Barrel Roll", RGB(220, 240, 255));
    } else if (ui.gameOver) {
        fill_rect(fb, 95, 76, 290, 110, RGB(14, 24, 48));
        draw_outline(fb, 95, 76, 290, 110, RGB(255, 60, 70));
        draw_text(fb, 175, 96, "CAZA DERRIBADO", RGB(255, 80, 80));
        sprintf(buf, "PUNTOS: %d   RECORD: %d", player.score, player.bestScore);
        draw_text(fb, 138, 126, buf, RGB(255, 255, 255));
        draw_text(fb, 145, 156, "Pulsa [X] para volver al Menu", RGB(160, 225, 255));
    } else {
        fill_rect(fb, 6, 6, 136, 18, RGB(14, 24, 44));
        draw_outline(fb, 6, 6, 136, 18, RGB(80, 200, 255));
        draw_text(fb, 10, 11, "HP", RGB(180, 225, 255));
        int hpw = (player.hp * 102) / (player.maxHp > 0 ? player.maxHp : 100);
        if (hpw < 0) hpw = 0; if (hpw > 102) hpw = 102;
        fill_rect(fb, 32, 11, 104, 7, RGB(30, 38, 55));
        fill_rect(fb, 32, 11, hpw, 7, player.hp > 30 ? RGB(60, 220, 255) : RGB(255, 60, 60));

        sprintf(buf, "OLEADA %d | PTS:%d", player.wave, player.score);
        draw_text(fb, 325, 10, buf, RGB(255, 235, 100));
        if (ui.msgTimer > 0) draw_text(fb, 12, 254, ui.msg, RGB(255, 240, 120));
    }

    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    cur_fb ^= 1;
}
