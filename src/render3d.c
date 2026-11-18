#include "game.h"
#include "font5x7.h"
#include <pspgu.h>
#include <pspgum.h>
#include <pspdisplay.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define BUF_W 512
#define SCR_SIZE (BUF_W * SCR_H * 4)
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))
#define RGBA(r,g,b,a) ((unsigned int)(((a)<<24) | ((b)<<16) | ((g)<<8) | (r)))

typedef struct {
    unsigned int color;
    float x, y, z;
} VertexC;

static unsigned int __attribute__((aligned(16))) gu_list[262144];
static void *fbp0 = (void *)0;
static void *fbp1 = (void *)(BUF_W * SCR_H * 4);
static void *zbp  = (void *)(BUF_W * SCR_H * 8);
static int cur_fb = 0;

/* 3D Nova Wing Starfighter (8 triangles = sleek swept-wing Arwing-style craft) */
static const VertexC __attribute__((aligned(16))) ship_verts[24] = {
    /* Nose top-left */
    {RGB(235,245,255),  0.0f,  0.18f, -1.3f}, {RGB(180,205,235), -0.35f, 0.0f,  0.5f}, {RGB(245,250,255),  0.0f,  0.32f,  0.3f},
    /* Nose top-right */
    {RGB(235,245,255),  0.0f,  0.18f, -1.3f}, {RGB(245,250,255),  0.0f,  0.32f,  0.3f}, {RGB(160,185,220),  0.35f, 0.0f,  0.5f},
    /* Left Swept Wing */
    {RGB(40,120,235),  -0.30f, 0.02f, -0.2f}, {RGB(20,75,180),   -1.35f,-0.15f, 0.8f}, {RGB(70,165,255),  -0.35f, 0.0f,  0.7f},
    /* Right Swept Wing */
    {RGB(40,120,235),   0.30f, 0.02f, -0.2f}, {RGB(70,165,255),   0.35f, 0.0f,  0.7f}, {RGB(20,75,180),    1.35f,-0.15f, 0.8f},
    /* Cockpit Canopy */
    {RGB(40,220,255),   0.0f,  0.34f, -0.3f}, {RGB(15,110,180),  -0.18f, 0.22f, 0.3f}, {RGB(15,110,180),   0.18f, 0.22f, 0.3f},
    /* Rear Hull */
    {RGB(140,160,190), -0.35f, 0.0f,   0.6f}, {RGB(140,160,190),  0.35f, 0.0f,  0.6f}, {RGB(200,215,235),  0.0f,  0.32f, 0.3f},
    /* Thruster Glow */
    {RGB(255,190,60),  -0.16f, 0.04f,  0.65f},{RGB(255,90,20),    0.0f, -0.02f, 1.15f},{RGB(255,190,60),   0.16f, 0.04f, 0.65f},
    /* Bottom Belly */
    {RGB(100,115,140),  0.0f, -0.10f, -1.1f}, {RGB(75,90,115),    0.35f, 0.0f,  0.5f}, {RGB(75,90,115),   -0.35f, 0.0f,  0.5f}
};

/* 3D Enemy Fighter */
static const VertexC __attribute__((aligned(16))) enemy_verts[12] = {
    {RGB(255,70,70),    0.0f,  0.0f,  1.1f},  {RGB(170,25,35),  -1.1f,  0.25f,-0.5f}, {RGB(220,45,55),   0.0f,  0.35f,-0.3f},
    {RGB(255,70,70),    0.0f,  0.0f,  1.1f},  {RGB(220,45,55),   0.0f,  0.35f,-0.3f}, {RGB(140,20,30),   1.1f,  0.25f,-0.5f},
    {RGB(255,210,70),   0.0f,  0.25f, 0.2f},  {RGB(200,60,20),  -0.25f, 0.10f,-0.2f}, {RGB(200,60,20),   0.25f, 0.10f,-0.2f},
    {RGB(120,20,25),    0.0f, -0.25f, 0.0f},  {RGB(170,25,35),   1.1f,  0.25f,-0.5f}, {RGB(170,25,35),  -1.1f,  0.25f,-0.5f}
};

/* 3D Octahedron (Asteroids & Explosions) */
static const VertexC __attribute__((aligned(16))) oct_verts[24] = {
    {RGB(160,150,175), 0, 1, 0}, {RGB(110,100,125), 1, 0, 0}, {RGB(130,120,145), 0, 0, 1},
    {RGB(160,150,175), 0, 1, 0}, {RGB(130,120,145), 0, 0, 1}, {RGB(95, 85,110),-1, 0, 0},
    {RGB(160,150,175), 0, 1, 0}, {RGB(95, 85,110),-1, 0, 0}, {RGB(115,105,130), 0, 0,-1},
    {RGB(160,150,175), 0, 1, 0}, {RGB(115,105,130), 0, 0,-1}, {RGB(110,100,125), 1, 0, 0},
    {RGB(70, 65, 85),  0,-1, 0}, {RGB(130,120,145), 0, 0, 1}, {RGB(110,100,125), 1, 0, 0},
    {RGB(70, 65, 85),  0,-1, 0}, {RGB(95, 85,110),-1, 0, 0}, {RGB(130,120,145), 0, 0, 1},
    {RGB(70, 65, 85),  0,-1, 0}, {RGB(115,105,130), 0, 0,-1}, {RGB(95, 85,110),-1, 0, 0},
    {RGB(70, 65, 85),  0,-1, 0}, {RGB(110,100,125), 1, 0, 0}, {RGB(115,105,130), 0, 0,-1}
};

static void draw_box_3d(float sx, float sy, float sz, unsigned int col) {
    VertexC *v = (VertexC *)sceGuGetMemory(36 * sizeof(VertexC));
    float x0 = -sx, x1 = sx, y0 = -sy, y1 = sy, z0 = -sz, z1 = sz;
    unsigned int cDark = RGB((col&0xFF)/2, ((col>>8)&0xFF)/2, ((col>>16)&0xFF)/2);
    VertexC cube[36] = {
        {col,x0,y1,z1},{col,x0,y0,z1},{col,x1,y0,z1}, {col,x0,y1,z1},{col,x1,y0,z1},{col,x1,y1,z1},
        {cDark,x1,y1,z0},{cDark,x1,y0,z0},{cDark,x0,y0,z0}, {cDark,x1,y1,z0},{cDark,x0,y0,z0},{cDark,x0,y1,z0},
        {col,x0,y1,z0},{col,x0,y1,z1},{col,x1,y1,z1}, {col,x0,y1,z0},{col,x1,y1,z1},{col,x1,y1,z0},
        {cDark,x0,y0,z1},{cDark,x0,y0,z0},{cDark,x1,y0,z0}, {cDark,x0,y0,z1},{cDark,x1,y0,z0},{cDark,x1,y0,z1},
        {cDark,x0,y1,z0},{cDark,x0,y0,z0},{cDark,x0,y0,z1}, {cDark,x0,y1,z0},{cDark,x0,y0,z1},{cDark,x0,y1,z1},
        {col,x1,y1,z1},{col,x1,y0,z1},{col,x1,y0,z0}, {col,x1,y1,z1},{col,x1,y0,z0},{col,x1,y1,z0}
    };
    memcpy(v, cube, sizeof(cube));
    sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 36, 0, v);
}

/* 2D Overlay helpers directly on the active backbuffer */
static unsigned int *get_draw_vram(void) {
    return (unsigned int *)(0x44000000 + (cur_fb ? (BUF_W * SCR_H * 4) : 0));
}

static void put_px(unsigned int *fb, int x, int y, unsigned int col) {
    if (x >= 0 && x < SCR_W && y >= 0 && y < SCR_H) fb[y * BUF_W + x] = col;
}

static void fill_rect(unsigned int *fb, int x, int y, int w, int h, unsigned int col) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > SCR_W ? SCR_W : x + w, y1 = y + h > SCR_H ? SCR_H : y + h;
    for (int yy = y0; yy < y1; yy++) {
        unsigned int *row = &fb[yy * BUF_W];
        for (int xx = x0; xx < x1; xx++) row[xx] = col;
    }
}

static void draw_outline(unsigned int *fb, int x, int y, int w, int h, unsigned int col) {
    fill_rect(fb, x, y, w, 1, col);
    fill_rect(fb, x, y + h - 1, w, 1, col);
    fill_rect(fb, x, y, 1, h, col);
    fill_rect(fb, x + w - 1, y, 1, h, col);
}

static void draw_text(unsigned int *fb, int x, int y, const char *s, unsigned int col) {
    while (*s) {
        char c = *s;
        if (c < 32 || c > 126) c = '?';
        const unsigned char *g = font5x7[c - 32];
        for (int cx = 0; cx < 5; cx++) {
            for (int cy = 0; cy < 7; cy++) {
                if (g[cx] & (1 << cy)) {
                    put_px(fb, x + cx + 1, y + cy + 1, RGB(10, 12, 18));
                    put_px(fb, x + cx, y + cy, col);
                }
            }
        }
        x += 6;
        s++;
    }
}

static void project_3d(float x, float y, float z, int *sx, int *sy) {
    float dz = 6.0f - z;
    if (dz < 1.0f) dz = 1.0f;
    float f = 260.0f / dz;
    *sx = SCR_W / 2 + (int)(x * f);
    *sy = SCR_H / 2 - (int)(y * f);
}

void render3d_init(void) {
    sceGuInit();
    sceGuStart(GU_DIRECT, gu_list);
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
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

void render3d_shutdown(void) {
    sceGuTerm();
}

void render3d_frame(void) {
    static float scrollZ = 0.0f;
    static int rf = 0;
    rf++;
    scrollZ += (player.boost > 0 && !ui.inTitle) ? 0.55f : 0.35f;
    if (scrollZ >= 10.0f) scrollZ -= 10.0f;

    sceGuStart(GU_DIRECT, gu_list);

    /* Stage sky colors */
    unsigned int bgCol = (player.stage == 1) ? RGB(8, 12, 28) :
                         ((player.stage == 2) ? RGB(22, 12, 32) : RGB(28, 8, 14));
    if (ui.bombFlash > 0) bgCol = RGB(220, 235, 255);
    sceGuClearColor(bgCol);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(58.0f, 480.0f / 272.0f, 0.5f, 180.0f);

    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    float camShake = (ui.shakeTimer > 0) ? ((ui.shakeTimer & 1) ? 0.25f : -0.25f) : 0.0f;
    ScePspFVector3 camPos = { player.x * 0.35f + camShake, player.y * 0.35f + 1.2f, 6.0f };
    ScePspFVector3 camLook = { player.x * 0.2f, player.y * 0.2f, -20.0f };
    ScePspFVector3 camUp = { 0.0f, 1.0f, 0.0f };
    sceGumLookAt(&camPos, &camLook, &camUp);

    sceGumMatrixMode(GU_MODEL);

    /* 1. 3D Starfield & Neon Scrolling Cyber-Grid Floor */
    {
        VertexC *sv = (VertexC *)sceGuGetMemory(MAX_STARS * sizeof(VertexC));
        for (int i = 0; i < MAX_STARS; i++) {
            sv[i].color = RGB(220, 235, 255);
            sv[i].x = stars[i].x; sv[i].y = stars[i].y; sv[i].z = stars[i].z;
        }
        sceGumLoadIdentity();
        sceGumDrawArray(GU_POINTS, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, MAX_STARS, 0, sv);

        /* Neon Grid Lines */
        int lines = 18;
        VertexC *gv = (VertexC *)sceGuGetMemory((lines * 2 + 12) * sizeof(VertexC));
        int gi = 0;
        unsigned int gridCol = (player.stage == 1) ? RGB(20, 180, 220) :
                               ((player.stage == 2) ? RGB(190, 60, 220) : RGB(220, 50, 70));
        for (int i = 0; i < lines; i++) {
            float gz = 8.0f - i * 8.0f + scrollZ;
            gv[gi++] = (VertexC){gridCol, -36.0f, -6.5f, gz};
            gv[gi++] = (VertexC){gridCol,  36.0f, -6.5f, gz};
        }
        for (int x = -30; x <= 30; x += 12) {
            gv[gi++] = (VertexC){gridCol, (float)x, -6.5f, 8.0f};
            gv[gi++] = (VertexC){RGB(10,20,40), (float)x, -6.5f, -135.0f};
        }
        sceGumDrawArray(GU_LINES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, gi, 0, gv);
    }

    if (ui.inTitle) {
        /* Showcase rotating 3D Nova Wing on Title Screen */
        sceGumLoadIdentity();
        ScePspFVector3 pos = { 0.0f, 0.3f, 0.5f };
        sceGumTranslate(&pos);
        sceGumRotateY(rf * 0.03f);
        sceGumRotateX(0.25f);
        sceGumScale(&(ScePspFVector3){1.6f, 1.6f, 1.6f});
        sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 24, 0, (void *)ship_verts);
    } else {
        /* 2. 3D Enemies, Asteroids, Rings & Dreadnought Bosses */
        for (int i = 0; i < MAX_ENEMIES; i++) {
            Enemy *e = &enemies[i];
            if (!e->alive) continue;
            sceGumLoadIdentity();
            sceGumTranslate(&(ScePspFVector3){e->x, e->y, e->z});
            if (e->type == EN_FIGHTER) {
                sceGumRotateZ(sinf(rf * 0.08f + i) * 0.4f);
                sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 12, 0, (void *)enemy_verts);
            } else if (e->type == EN_ASTEROID) {
                sceGumRotateY(e->rot);
                sceGumRotateX(e->rot * 0.7f);
                sceGumScale(&(ScePspFVector3){1.4f, 1.4f, 1.4f});
                sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 24, 0, (void *)oct_verts);
            } else if (e->type == EN_RING) {
                sceGumRotateZ(rf * 0.06f);
                draw_box_3d(1.6f, 0.14f, 0.2f, RGB(255, 220, 60));
                sceGumRotateZ(1.57f);
                draw_box_3d(1.6f, 0.14f, 0.2f, RGB(255, 220, 60));
            } else if (e->type == EN_BOSS) {
                unsigned int bCol = e->hitFlash > 0 ? RGB(255,255,255) : RGB(150, 30, 50);
                draw_box_3d(4.2f, 1.4f, 3.0f, bCol);
                sceGumTranslate(&(ScePspFVector3){0.0f, 0.0f, 3.2f});
                sceGumRotateZ(rf * 0.08f);
                draw_box_3d(1.1f, 1.1f, 0.8f, RGB(255, 200, 40)); /* Glowing Core */
            }
        }

        /* 3. Player 3D Nova Wing Starfighter */
        if ((player.iframes & 2) == 0) {
            sceGumLoadIdentity();
            sceGumTranslate(&(ScePspFVector3){player.x, player.y, 0.0f});
            sceGumRotateY(-player.vx * 0.45f);
            sceGumRotateX(player.vy * 0.40f);
            sceGumRotateZ(player.roll);
            sceGumDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, 24, 0, (void *)ship_verts);

            /* Barrel Roll Shield Halo */
            if (player.rollTimer > 0) {
                sceGumRotateZ(rf * 0.35f);
                draw_box_3d(1.5f, 0.05f, 0.05f, RGB(80, 230, 255));
            }
        }

        /* 4. Player Lasers & Homing Missiles */
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!bullets[i].alive) continue;
            sceGumLoadIdentity();
            sceGumTranslate(&(ScePspFVector3){bullets[i].x, bullets[i].y, bullets[i].z});
            if (bullets[i].targetIdx >= 0) {
                draw_box_3d(0.22f, 0.22f, 0.95f, RGB(80, 255, 130));
            } else {
                draw_box_3d(0.10f, 0.10f, 1.4f, player.laserLv > 0 ? RGB(80, 255, 180) : RGB(80, 210, 255));
            }
        }

        /* Enemy Plasma Bolts */
        for (int i = 0; i < MAX_EBULLETS; i++) {
            if (!ebullets[i].alive) continue;
            sceGumLoadIdentity();
            sceGumTranslate(&(ScePspFVector3){ebullets[i].x, ebullets[i].y, ebullets[i].z});
            draw_box_3d(0.24f, 0.24f, 0.24f, RGB(255, 80, 60));
        }

        /* 5. 3D Explosion Particles */
        for (int i = 0; i < MAX_PARTS; i++) {
            if (parts[i].life <= 0) continue;
            sceGumLoadIdentity();
            sceGumTranslate(&(ScePspFVector3){parts[i].x, parts[i].y, parts[i].z});
            float s = parts[i].size * (parts[i].life / 24.0f);
            draw_box_3d(s, s, s, parts[i].color);
        }
    }

    sceGuFinish();
    sceGuSync(0, 0);

    /* 6. Crisp 2D HUD Overlay directly on the rendered frame */
    unsigned int *fb = get_draw_vram();
    char buf[64];

    if (ui.inTitle) {
        fill_rect(fb, 110, 18, 260, 44, RGB(14, 18, 32));
        draw_outline(fb, 110, 18, 260, 44, RGB(70, 200, 255));
        draw_text(154, 28, "N O V A   W I N G   6 4", RGB(255, 235, 120));
        draw_text(150, 44, "3D HARDWARE SPACE COMBAT", RGB(100, 225, 255));

        sprintf(buf, "RECORD MAXIMO: %d PTS", player.bestScore);
        draw_text(165, 198, buf, RGB(140, 255, 160));
        draw_text(152, 222, "Pulsa [X] o [START] para Despegar!", RGB(255, 255, 255));
        draw_text(80, 248, "L/R: Barrel Roll | X: Laser | Tri: Misil Guiado | O: Bomba", RGB(170, 190, 220));
    } else if (ui.gameOver || ui.victory) {
        fill_rect(fb, 90, 70, 300, 120, RGB(14, 18, 28));
        draw_outline(fb, 90, 70, 300, 120, ui.victory ? RGB(255,220,80) : RGB(255,60,60));
        draw_text(155, 90, ui.victory ? "GALAXIA LIBERADA!" : "NAVE DESTRUIDA", ui.victory ? RGB(255,230,90) : RGB(255,90,90));
        sprintf(buf, "PUNTUACION: %d   RECORD: %d", player.score, player.bestScore);
        draw_text(135, 122, buf, RGB(230, 240, 255));
        draw_text(145, 158, "Pulsa [X] para volver al Menu", RGB(180, 220, 255));
    } else {
        /* 3D Crosshair Reticle */
        int rx, ry;
        project_3d(player.x * 0.9f, player.y * 0.9f, -14.0f, &rx, &ry);
        draw_outline(fb, rx - 8, ry - 8, 16, 16, RGB(80, 235, 255));
        put_px(fb, rx, ry, RGB(255, 255, 255));

        /* Green Homing Lock-On Boxes */
        for (int k = 0; k < player.lockCount; k++) {
            int ei = player.lockTargets[k];
            if (ei >= 0 && ei < MAX_ENEMIES && enemies[ei].alive) {
                int lx, ly;
                project_3d(enemies[ei].x, enemies[ei].y, enemies[ei].z, &lx, &ly);
                draw_outline(fb, lx - 12, ly - 12, 24, 24, RGB(80, 255, 110));
                draw_text(lx - 10, ly - 20, "LOCK", RGB(80, 255, 110));
            }
        }

        /* Shield & Boost Bars (Top-Left) */
        fill_rect(fb, 6, 6, 146, 26, RGB(12, 16, 26));
        draw_outline(fb, 6, 6, 146, 26, RGB(60, 140, 210));
        draw_text(10, 10, "SHD", RGB(180, 225, 255));
        int hpW = (player.hp * 108) / (player.maxHp > 0 ? player.maxHp : 100);
        if (hpW < 0) hpW = 0; if (hpW > 108) hpW = 108;
        fill_rect(fb, 34, 10, 110, 7, RGB(30, 38, 52));
        fill_rect(fb, 35, 11, hpW, 5, player.hp > 30 ? RGB(60, 210, 255) : RGB(255, 60, 60));

        draw_text(10, 21, "BST", RGB(255, 220, 120));
        int bW = (player.boost * 108) / 100;
        fill_rect(fb, 34, 21, 110, 7, RGB(30, 38, 52));
        fill_rect(fb, 35, 22, bW, 5, RGB(255, 195, 50));

        /* Score, Stage & Bombs (Top-Right) */
        sprintf(buf, "ZONA %d | PTS:%d", player.stage, player.score);
        draw_text(320, 8, buf, RGB(255, 235, 120));
        sprintf(buf, "BOMBAS:%d  COMBO:x%d", player.bombs, player.combo > 1 ? player.combo : 1);
        draw_text(320, 20, buf, RGB(140, 255, 170));

        /* Boss HP Bar */
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (enemies[i].alive && enemies[i].type == EN_BOSS) {
                fill_rect(fb, 120, 36, 240, 14, RGB(24, 12, 18));
                draw_outline(fb, 120, 36, 240, 14, RGB(255, 70, 90));
                int bw = (enemies[i].hp * 232) / (enemies[i].maxHp > 0 ? enemies[i].maxHp : 1);
                if (bw < 0) bw = 0; if (bw > 232) bw = 232;
                fill_rect(fb, 124, 41, bw, 5, RGB(255, 55, 85));
                draw_text(175, 39, "DREADNOUGHT BOSS", RGB(255, 220, 180));
                break;
            }
        }

        if (ui.msgTimer > 0) {
            draw_text(10, 256, ui.msg, RGB(140, 245, 255));
        }
        draw_text(310, 256, "[X]Laser [^]Misil [O]Bomba", RGB(160, 180, 210));
    }

    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    cur_fb ^= 1;
}
