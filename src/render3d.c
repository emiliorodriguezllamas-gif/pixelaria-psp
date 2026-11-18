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

static void draw_box(float cx, float cy, float cz, float sx, float sy, float sz, unsigned int col) {
    if (vidx + 36 > 4096) return;
    float x0 = cx - sx, x1 = cx + sx;
    float y0 = cy - sy, y1 = cy + sy;
    float z0 = cz - sz, z1 = cz + sz;
    Vtx3D *v = &vpool[vidx];
    int k = 0;
    /* Front */
    v[k++] = (Vtx3D){col, x0,y0,z1}; v[k++] = (Vtx3D){col, x1,y0,z1}; v[k++] = (Vtx3D){col, x1,y1,z1};
    v[k++] = (Vtx3D){col, x0,y0,z1}; v[k++] = (Vtx3D){col, x1,y1,z1}; v[k++] = (Vtx3D){col, x0,y1,z1};
    /* Back */
    v[k++] = (Vtx3D){col, x1,y0,z0}; v[k++] = (Vtx3D){col, x0,y0,z0}; v[k++] = (Vtx3D){col, x0,y1,z0};
    v[k++] = (Vtx3D){col, x1,y0,z0}; v[k++] = (Vtx3D){col, x0,y1,z0}; v[k++] = (Vtx3D){col, x1,y1,z0};
    /* Top */
    v[k++] = (Vtx3D){col, x0,y1,z1}; v[k++] = (Vtx3D){col, x1,y1,z1}; v[k++] = (Vtx3D){col, x1,y1,z0};
    v[k++] = (Vtx3D){col, x0,y1,z1}; v[k++] = (Vtx3D){col, x1,y1,z0}; v[k++] = (Vtx3D){col, x0,y1,z0};
    /* Left */
    v[k++] = (Vtx3D){col, x0,y0,z0}; v[k++] = (Vtx3D){col, x0,y0,z1}; v[k++] = (Vtx3D){col, x0,y1,z1};
    v[k++] = (Vtx3D){col, x0,y0,z0}; v[k++] = (Vtx3D){col, x0,y1,z1}; v[k++] = (Vtx3D){col, x0,y1,z0};
    /* Right */
    v[k++] = (Vtx3D){col, x1,y0,z1}; v[k++] = (Vtx3D){col, x1,y0,z0}; v[k++] = (Vtx3D){col, x1,y1,z0};
    v[k++] = (Vtx3D){col, x1,y0,z1}; v[k++] = (Vtx3D){col, x1,y1,z0}; v[k++] = (Vtx3D){col, x1,y1,z1};

    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, k, 0, v);
    vidx += k;
}

static void draw_mech(float x, float y, float z, float yaw, float scale, unsigned int bodyCol, unsigned int visorCol, int shield) {
    sceGumPushMatrix();
    ScePspFVector3 pos = { x, y, z };
    sceGumTranslate(&pos);
    sceGumRotateY(yaw);

    /* Legs */
    draw_box(-0.45f * scale, 0.55f * scale, 0.0f, 0.25f * scale, 0.55f * scale, 0.30f * scale, RGB(45, 55, 75));
    draw_box( 0.45f * scale, 0.55f * scale, 0.0f, 0.25f * scale, 0.55f * scale, 0.30f * scale, RGB(45, 55, 75));
    /* Torso */
    draw_box(0.0f, 1.55f * scale, 0.0f, 0.72f * scale, 0.65f * scale, 0.55f * scale, bodyCol);
    /* Head / Visor */
    draw_box(0.0f, 2.38f * scale, 0.05f * scale, 0.36f * scale, 0.26f * scale, 0.32f * scale, visorCol);
    /* Cannon Arms */
    draw_box(-1.05f * scale, 1.55f * scale, -0.35f * scale, 0.22f * scale, 0.24f * scale, 0.75f * scale, RGB(90, 110, 140));
    draw_box( 1.05f * scale, 1.55f * scale, -0.35f * scale, 0.22f * scale, 0.24f * scale, 0.75f * scale, RGB(90, 110, 140));

    if (shield) {
        draw_box(0.0f, 1.4f * scale, -0.9f * scale, 1.25f * scale, 1.15f * scale, 0.08f * scale, RGB(60, 235, 255));
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
    sceGuClearColor(RGB(10, 14, 28));
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(60.0f, (float)SCR_W / (float)SCR_H, 0.5f, 140.0f);

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    float camDist = 8.5f;
    float shake = (ui.shakeTimer > 0) ? ((ui.shakeTimer & 1) ? 0.2f : -0.2f) : 0.0f;
    ScePspFVector3 eye = {
        player.x + sinf(player.yaw) * camDist + shake,
        4.2f,
        player.z + cosf(player.yaw) * camDist
    };
    ScePspFVector3 center = {
        player.x - sinf(player.yaw) * 8.0f,
        1.6f,
        player.z - cosf(player.yaw) * 8.0f
    };
    ScePspFVector3 up = { 0.0f, 1.0f, 0.0f };
    sceGumLookAt(&eye, &center, &up);

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();

    /* Arena Floor & Perimeter Walls */
    for (int gx = -3; gx <= 3; gx++) {
        for (int gz = -3; gz <= 3; gz++) {
            unsigned int col = ((gx + gz) & 1) ? RGB(22, 28, 46) : RGB(28, 36, 58);
            draw_box(gx * 8.0f, -0.1f, gz * 8.0f, 4.0f, 0.1f, 4.0f, col);
        }
    }
    draw_box(0.0f, 1.5f, -28.0f, 28.0f, 1.5f, 0.6f, RGB(0, 180, 255));
    draw_box(0.0f, 1.5f,  28.0f, 28.0f, 1.5f, 0.6f, RGB(0, 180, 255));
    draw_box(-28.0f, 1.5f, 0.0f, 0.6f, 1.5f, 28.0f, RGB(255, 60, 160));
    draw_box( 28.0f, 1.5f, 0.0f, 0.6f, 1.5f, 28.0f, RGB(255, 60, 160));

    /* Player Mech */
    if (!ui.inTitle && !ui.gameOver) {
        if ((player.iframes & 2) == 0) {
            draw_mech(player.x, 0.0f, player.z, player.yaw, 1.0f, RGB(50, 160, 255), RGB(255, 230, 80), player.shieldOn);
        }
    }

    /* Enemy Mechs */
    for (int i = 0; i < MAX_ENEMIES; i++) {
        EnemyMech *e = &enemies[i];
        if (!e->alive) continue;
        unsigned int bcol = e->hitFlash ? RGB(255, 255, 255) : (e->isBoss ? RGB(255, 50, 80) : RGB(235, 90, 50));
        draw_mech(e->x, 0.0f, e->z, e->yaw, e->isBoss ? 1.65f : 1.0f, bcol, RGB(120, 255, 120), 0);
    }

    /* Bullets */
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].alive) continue;
        unsigned int col = bullets[i].fromPlayer ? (bullets[i].homing ? RGB(100, 255, 120) : RGB(0, 235, 255)) : RGB(255, 70, 70);
        draw_box(bullets[i].x, bullets[i].y, bullets[i].z, 0.22f, 0.22f, 0.22f, col);
    }

    /* Particles */
    for (int i = 0; i < MAX_PARTS; i++) {
        if (parts[i].life > 0) {
            draw_box(parts[i].x, parts[i].y, parts[i].z, 0.2f, 0.2f, 0.2f, parts[i].color);
        }
    }

    sceGuFinish();
    sceGuSync(0, 0);

    unsigned int *fb = (unsigned int*)(0x04000000 + (cur_fb ? (BUF_W * SCR_H * 4) : 0));
    char buf[64];

    if (ui.inTitle) {
        fill_rect(fb, 95, 24, 290, 46, RGB(12, 16, 32));
        draw_outline(fb, 95, 24, 290, 46, RGB(0, 220, 255));
        draw_text(fb, 140, 34, "M E C H   A R E N A   Z E R O", RGB(255, 225, 90));
        draw_text(fb, 142, 50, "3D HARDWARE MECHA COMBAT", RGB(0, 220, 255));
        sprintf(buf, "RECORD: %d PTS", player.bestScore);
        draw_text(fb, 185, 198, buf, RGB(140, 255, 160));
        draw_text(fb, 145, 222, "Pulsa [X] o [START] para Combatir!", RGB(255, 255, 255));
        draw_text(fb, 76, 248, "L/R: Girar | X: Plasma | Tri: Misiles | []: Dash | O: Escudo", RGB(180, 200, 230));
    } else if (ui.gameOver) {
        fill_rect(fb, 95, 76, 290, 110, RGB(14, 16, 28));
        draw_outline(fb, 95, 76, 290, 110, RGB(255, 60, 70));
        draw_text(fb, 175, 96, "MECHA DESTRUIDO", RGB(255, 80, 80));
        sprintf(buf, "PUNTOS: %d   RECORD: %d", player.score, player.bestScore);
        draw_text(fb, 138, 126, buf, RGB(230, 240, 255));
        draw_text(fb, 145, 156, "Pulsa [X] para volver al Menu", RGB(160, 225, 255));
    } else {
        fill_rect(fb, 6, 6, 144, 28, RGB(12, 16, 28));
        draw_outline(fb, 6, 6, 144, 28, RGB(0, 200, 255));
        draw_text(fb, 10, 11, "HP", RGB(180, 225, 255));
        int hpw = (player.hp * 108) / (player.maxHp > 0 ? player.maxHp : 100);
        if (hpw < 0) hpw = 0; if (hpw > 108) hpw = 108;
        fill_rect(fb, 34, 11, 110, 6, RGB(30, 36, 50));
        fill_rect(fb, 34, 11, hpw, 6, player.hp > 30 ? RGB(60, 220, 255) : RGB(255, 60, 60));

        draw_text(fb, 10, 22, "EN", RGB(255, 220, 120));
        int enw = (player.energy * 108) / 100;
        if (enw < 0) enw = 0; if (enw > 108) enw = 108;
        fill_rect(fb, 34, 22, 110, 6, RGB(30, 36, 50));
        fill_rect(fb, 34, 22, enw, 6, RGB(255, 200, 60));

        sprintf(buf, "OLEADA %d | PTS:%d", player.wave, player.score);
        draw_text(fb, 325, 10, buf, RGB(255, 230, 100));
        if (ui.msgTimer > 0) draw_text(fb, 12, 254, ui.msg, RGB(120, 255, 180));
    }

    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    cur_fb ^= 1;
}
