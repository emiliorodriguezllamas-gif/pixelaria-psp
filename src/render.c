#include "game.h"
#include "font5x7.h"
#include "sprites_re4.h"
#include <pspdisplay.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define VRAM_STRIDE 512
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

static unsigned int *vram_base = (unsigned int *)0x44000000;
static unsigned int backbuf[SCR_W * SCR_H];
static float zbuf[SCR_W];

static void put_px(int x, int y, unsigned int col) {
    if (x >= 0 && x < SCR_W && y >= 0 && y < SCR_H) {
        backbuf[y * SCR_W + x] = col;
    }
}

static void fill_rect(int x, int y, int w, int h, unsigned int col) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > SCR_W ? SCR_W : x + w;
    int y1 = y + h > SCR_H ? SCR_H : y + h;
    for (int yy = y0; yy < y1; yy++) {
        unsigned int *row = &backbuf[yy * SCR_W];
        for (int xx = x0; xx < x1; xx++) row[xx] = col;
    }
}

static void draw_rect_outline(int x, int y, int w, int h, unsigned int col) {
    fill_rect(x, y, w, 1, col);
    fill_rect(x, y + h - 1, w, 1, col);
    fill_rect(x, y, 1, h, col);
    fill_rect(x + w - 1, y, 1, h, col);
}

static void draw_line(int x0, int y0, int x1, int y1, unsigned int col) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = -(y1 > y0 ? y1 - y0 : y0 - y1);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        put_px(x0, y0, col);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void draw_char(int x, int y, char c, unsigned int col) {
    if (c < 32 || c > 126) c = '?';
    const unsigned char *glyph = font5x7[c - 32];
    for (int cx = 0; cx < 5; cx++) {
        unsigned char bits = glyph[cx];
        for (int cy = 0; cy < 7; cy++) {
            if (bits & (1 << cy)) put_px(x + cx, y + cy, col);
        }
    }
}

static void draw_text(int x, int y, const char *s, unsigned int col) {
    while (*s) {
        draw_char(x, y, *s, col);
        x += 6;
        s++;
    }
}

static unsigned int fog_shade(unsigned int col, float dist, int hitFlash) {
    if (hitFlash > 0) return RGB(255, 90, 90);
    int mul = (int)(115.0f / (1.0f + dist * 0.22f));
    if (mul > 100) mul = 100;
    if (mul < 14) mul = 14;
    int r = ((col & 0xFF) * mul) / 100;
    int g = (((col >> 8) & 0xFF) * mul) / 100;
    int b = (((col >> 16) & 0xFF) * mul) / 100;
    return RGB(r, g, b);
}

/* Procedural 16x16 Wall Textures */
static unsigned int sample_wall_tex(int wtype, int tx, int ty, int side) {
    int r = 100, g = 90, b = 80;
    if (wtype == W_WOOD_WALL) {
        /* Timber cabin planks + dark beams */
        if (tx == 0 || tx == 15 || ty == 0 || ty == 7 || ty == 15) { r = 48; g = 34; b = 22; }
        else if (tx >= 5 && tx <= 10 && ty >= 3 && ty <= 6) { r = 170; g = 125; b = 60; } /* Warm window */
        else { r = 112 + ((tx * 7 + ty * 3) & 15); g = 78 + ((ty * 5) & 7); b = 48; }
    } else if (wtype == W_STONE_WALL) {
        int brickRow = ty / 4;
        int mortar = (ty % 4 == 0) || ((tx + (brickRow & 1) * 4) % 8 == 0);
        if (mortar) { r = 42; g = 46; b = 44; }
        else { r = 95 + ((tx ^ ty) & 15); g = 100 + ((tx * ty) & 7); b = 96; }
    } else if (wtype == W_CHURCH_WALL) {
        if (tx == 0 || tx == 15 || ty == 0 || ty == 15) { r = 55; g = 48; b = 64; }
        else if (tx >= 6 && tx <= 9 && ty >= 3 && ty <= 12) { r = 180; g = 60; b = 90; } /* Gothic glass */
        else { r = 110; g = 104; b = 125; }
    } else if (wtype == W_GATE_DOOR) {
        if (tx % 3 == 0 || ty == 0 || ty == 15) { r = 60; g = 65; b = 75; }
        else if (tx >= 6 && tx <= 9 && ty >= 6 && ty <= 9) { r = 220; g = 40; b = 40; } /* Red Cult Seal */
        else { r = 30; g = 24; b = 22; }
    } else {
        int mortar = (ty % 4 == 0) || (tx % 8 == 0);
        if (mortar) { r = 50; g = 35; b = 35; }
        else if (((tx * 13 + ty * 7) % 19) < 4) { r = 155; g = 25; b = 25; }
        else { r = 105; g = 85; b = 82; }
    }
    if (side) { r = (r * 78) / 100; g = (g * 78) / 100; b = (b * 78) / 100; }
    return RGB(r, g, b);
}

void render_init(void) {
    sceDisplaySetMode(0, SCR_W, SCR_H);
    sceDisplaySetFrameBuf(vram_base, VRAM_STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
}

void render_frame(void) {
    char buf[64];

    if (ui.inTitle) {
        fill_rect(0, 0, SCR_W, SCR_H, RGB(14, 16, 22));
        fill_rect(0, 0, SCR_W, 130, RGB(22, 26, 36));
        /* Moon & Foggy Village silhouette */
        fill_rect(370, 24, 26, 26, RGB(215, 225, 235));
        fill_rect(30, 80, 60, 110, RGB(18, 16, 20));
        fill_rect(38, 95, 12, 16, RGB(190, 130, 55));
        fill_rect(390, 65, 50, 125, RGB(18, 16, 20));
        fill_rect(408, 40, 14, 25, RGB(18, 16, 20));

        fill_rect(90, 20, 300, 48, RGB(24, 18, 20));
        draw_rect_outline(90, 20, 300, 48, RGB(195, 45, 45));
        draw_text(148, 30, "B I O - V I L L A G E   4", RGB(235, 215, 195));
        draw_text(136, 48, "LAS SOMBRAS DEL PUEBLO (PSP 3D)", RGB(210, 70, 70));

        if (ui.showHelp) {
            fill_rect(50, 78, 380, 162, RGB(18, 20, 26));
            draw_rect_outline(50, 78, 380, 162, RGB(210, 70, 70));
            draw_text(64, 88, "CONTROLES RESIDENT EVIL 4 PSP:", RGB(255, 215, 120));
            draw_text(64, 104, "Stick / Cruceta: Caminar y Girar (o Mover Laser)", RGB(230, 230, 235));
            draw_text(64, 118, "Mantener [L]: Apuntar con Mira Laser Roja", RGB(255, 110, 110));
            draw_text(64, 132, "[X] o [R]: Disparar / Patada Cuerpo a Cuerpo", RGB(230, 230, 235));
            draw_text(64, 146, "[Cuadrado]: Recargar / Cambiar Arma", RGB(230, 230, 235));
            draw_text(64, 160, "[Triangulo]: Hablar con el Buhonero (Tienda)", RGB(190, 140, 255));
            draw_text(64, 174, "[Circulo]: Maletin (Usar/Mezclar Hierbas y Armas)", RGB(130, 235, 150));
            draw_text(64, 192, "Dispara a la CABEZA (Critico) o RODILLA + [X] Patada!", RGB(255, 220, 120));
            draw_text(148, 218, "Pulsa [X] o [O] para volver", RGB(200, 200, 210));
        } else {
            const char *opts[3] = {"Continuar Partida", "Nueva Mision", "Controles RE4"};
            for (int i = 0; i < 3; i++) {
                int oy = 92 + i * 30;
                fill_rect(140, oy, 200, 22, i == ui.titleSel ? RGB(85, 28, 32) : RGB(24, 24, 30));
                draw_rect_outline(140, oy, 200, 22, i == ui.titleSel ? RGB(235, 90, 90) : RGB(80, 80, 95));
                draw_text(168, oy + 7, opts[i], i == ui.titleSel ? RGB(255, 230, 180) : RGB(215, 215, 225));
            }
            draw_text(146, 250, "Cruceta: Elegir   [X]: Aceptar", RGB(190, 195, 205));
        }

        sceDisplayWaitVblankStart();
        for (int y = 0; y < SCR_H; y++) {
            memcpy(&vram_base[y * VRAM_STRIDE], &backbuf[y * SCR_W], SCR_W * 4);
        }
        return;
    }

    int shakeX = (ui.shakeTimer > 0) ? ((ui.shakeTimer & 1) ? 3 : -3) : 0;
    int horizon = 132 + (ui.shakeTimer > 0 ? ((ui.shakeTimer & 2) ? 2 : -2) : 0);
    static int rf = 0;
    rf++;

    if (ui.gameWon) {
        fill_rect(0, 0, SCR_W, SCR_H, RGB(10, 12, 18));
        draw_rect_outline(40, 40, 400, 190, RGB(235, 200, 90));
        draw_text(150, 70, "MISION COMPLETADA", RGB(255, 225, 120));
        draw_text(110, 100, "Escapaste de las Sombras del Pueblo!", RGB(230, 235, 240));
        sprintf(buf, "Bajas: %d   Pesetas: %d", player.kills, player.pesetas);
        draw_text(130, 130, buf, RGB(150, 235, 170));
        draw_text(120, 170, "Pulsa [X] para volver al menu", RGB(200, 205, 215));
        sceDisplayWaitVblankStart();
        for (int y = 0; y < SCR_H; y++) {
            memcpy(&vram_base[y * VRAM_STRIDE], &backbuf[y * SCR_W], SCR_W * 4);
        }
        return;
    }

    /* 1. Ceiling (Foggy Night Sky) & 3D Perspective Cobblestone Floor */
    float dirX = cosf(player.ang);
    float dirY = sinf(player.ang);
    float fovScale = player.aiming ? ((player.weapon == WPN_RIFLE) ? 0.28f : 0.48f) : 0.66f;
    float planeX = -dirY * fovScale;
    float planeY =  dirX * fovScale;

    for (int y = 0; y < SCR_H; y++) {
        unsigned int *row = &backbuf[y * SCR_W];
        if (y < horizon) {
            int d = horizon - y;
            int r = 18 + (d * 10) / (horizon > 0 ? horizon : 1);
            int g = 22 + (d * 14) / (horizon > 0 ? horizon : 1);
            int b = 32 + (d * 24) / (horizon > 0 ? horizon : 1);
            unsigned int col = RGB(r, g, b);
            for (int x = 0; x < SCR_W; x++) row[x] = col;
        } else {
            int p = y - horizon;
            if (p < 1) p = 1;
            float rowDist = (0.5f * SCR_H) / (float)p;
            float stepX = rowDist * 2.0f * planeX / (float)SCR_W;
            float stepY = rowDist * 2.0f * planeY / (float)SCR_W;
            float fx = player.x + rowDist * (dirX - planeX);
            float fy = player.y + rowDist * (dirY - planeY);
            int shade = 18 + (p * 38) / (SCR_H - horizon);
            for (int x = 0; x < SCR_W; x += 2) {
                int cx = (int)(fx * 4.0f);
                int cy = (int)(fy * 4.0f);
                int chk = ((cx ^ cy) & 1) ? 6 : 0;
                unsigned int col = RGB(shade + chk + 2, shade + chk, shade + chk - 2);
                row[x] = col;
                row[x + 1] = col;
                fx += stepX * 2.0f;
                fy += stepY * 2.0f;
            }
        }
    }

    /* 2. 3D Raycasting Walls (240 rays x 2px columns) */

    for (int col = 0; col < SCR_W; col += 2) {
        float camX = 2.0f * col / (float)SCR_W - 1.0f;
        float rayDirX = dirX + planeX * camX;
        float rayDirY = dirY + planeY * camX;

        int mapX = (int)player.x;
        int mapY = (int)player.y;

        float deltaDistX = (fabsf(rayDirX) < 0.0001f) ? 1e30f : fabsf(1.0f / rayDirX);
        float deltaDistY = (fabsf(rayDirY) < 0.0001f) ? 1e30f : fabsf(1.0f / rayDirY);
        float sideDistX, sideDistY;
        int stepX, stepY;

        if (rayDirX < 0) { stepX = -1; sideDistX = (player.x - mapX) * deltaDistX; }
        else             { stepX =  1; sideDistX = (mapX + 1.0f - player.x) * deltaDistX; }
        if (rayDirY < 0) { stepY = -1; sideDistY = (player.y - mapY) * deltaDistY; }
        else             { stepY =  1; sideDistY = (mapY + 1.0f - player.y) * deltaDistY; }

        int hit = 0, side = 0, wtype = 1, steps = 0;
        while (!hit && steps < 36) {
            steps++;
            if (sideDistX < sideDistY) { sideDistX += deltaDistX; mapX += stepX; side = 0; }
            else                       { sideDistY += deltaDistY; mapY += stepY; side = 1; }
            if (mapX < 0 || mapY < 0 || mapX >= MAP_W || mapY >= MAP_H) { hit = 1; wtype = W_STONE_WALL; break; }
            int wt = worldMap[mapY * MAP_W + mapX];
            if (wt > 0) { hit = 1; wtype = wt; }
        }

        float perpDist = (side == 0) ? (sideDistX - deltaDistX) : (sideDistY - deltaDistY);
        if (perpDist < 0.12f) perpDist = 0.12f;
        zbuf[col] = perpDist;
        zbuf[col + 1] = perpDist;

        int lineH = (int)(SCR_H / perpDist);
        int drawStart = horizon - lineH / 2;
        int drawEnd = horizon + lineH / 2;
        if (drawStart < 0) drawStart = 0;
        if (drawEnd >= SCR_H) drawEnd = SCR_H - 1;

        float wallX = (side == 0) ? (player.y + perpDist * rayDirY) : (player.x + perpDist * rayDirX);
        wallX -= floorf(wallX);
        int texX = (int)(wallX * 16.0f) & 15;

        for (int y = drawStart; y <= drawEnd; y++) {
            int d = y - (horizon - lineH / 2);
            int texY = ((d * 16) / (lineH > 0 ? lineH : 1)) & 15;
            unsigned int wc = fog_shade(sample_wall_tex(wtype, texX, texY, side), perpDist, 0);
            backbuf[y * SCR_W + col] = wc;
            backbuf[y * SCR_W + col + 1] = wc;
        }
    }

    /* 3. Depth-Sorted 2D Sprites Projected in 3D Space */
    int order[MAX_ENTS];
    float dists[MAX_ENTS];
    int entCnt = 0;
    for (int i = 0; i < MAX_ENTS; i++) {
        if (!ents[i].alive) continue;
        float dx = ents[i].x - player.x;
        float dy = ents[i].y - player.y;
        order[entCnt] = i;
        dists[entCnt] = dx * dx + dy * dy;
        entCnt++;
    }
    /* Sort far to near */
    for (int i = 0; i < entCnt - 1; i++) {
        for (int j = i + 1; j < entCnt; j++) {
            if (dists[i] < dists[j]) {
                float td = dists[i]; dists[i] = dists[j]; dists[j] = td;
                int to = order[i]; order[i] = order[j]; order[j] = to;
            }
        }
    }

    float invDet = 1.0f / (planeX * dirY - dirX * planeY);
    for (int idx = 0; idx < entCnt; idx++) {
        Entity *e = &ents[order[idx]];
        float sx = e->x - player.x;
        float sy = e->y - player.y;
        float transX = invDet * (dirY * sx - dirX * sy);
        float transY = invDet * (-planeY * sx + planeX * sy);
        if (transY <= 0.25f) continue;

        int sprScreenX = (int)((SCR_W / 2) * (1.0f + transX / transY)) + shakeX;
        int fullH = (int)fabsf(SCR_H / transY);
        int sprH = fullH;
        int sprW = (fullH * 3) / 4;
        if (e->type == ENT_CRATE || e->type >= ENT_HERB_G) {
            sprH = (fullH * 3) / 5;
            sprW = (fullH * 3) / 5;
        }
        int crouchOff = (e->stunTimer > 0) ? (sprH / 5) : 0;
        int bobOff = 0;
        if (e->type == ENT_VILLAGER || e->type == ENT_CHAINSAW || e->type == ENT_BOSS) {
            if (e->stunTimer <= 0) bobOff = (int)(sinf(rf * 0.22f + e->x * 3.0f) * (float)sprH * 0.035f);
        }
        int drawStartY = horizon + fullH / 2 - sprH + crouchOff + bobOff;
        int drawEndY = horizon + fullH / 2 + bobOff;
        int drawStartX = sprScreenX - sprW / 2;
        int drawEndX = sprScreenX + sprW / 2;

        const unsigned int *sprData = spr_villager;
        if (e->type == ENT_CHAINSAW || e->type == ENT_BOSS) sprData = spr_chainsaw;
        else if (e->type == ENT_MERCHANT) sprData = spr_merchant;
        else if (e->type == ENT_CRATE || e->type == ENT_BARREL) sprData = spr_crate;
        else if (e->type >= ENT_HERB_G) sprData = spr_herb;

        int clipStartX = drawStartX < 0 ? 0 : drawStartX;
        int clipEndX = drawEndX >= SCR_W ? SCR_W - 1 : drawEndX;
        int clipStartY = drawStartY < 0 ? 0 : drawStartY;
        int clipEndY = drawEndY >= SCR_H ? SCR_H - 1 : drawEndY;

        for (int stripe = clipStartX; stripe <= clipEndX; stripe++) {
            if (transY >= zbuf[stripe]) continue;
            int texX = ((stripe - drawStartX) * SPR_W) / (sprW > 0 ? sprW : 1);
            if (texX < 0) texX = 0; if (texX >= SPR_W) texX = SPR_W - 1;
            for (int y = clipStartY; y <= clipEndY; y++) {
                int texY = ((y - drawStartY) * SPR_H) / (sprH > 0 ? sprH : 1);
                if (texY < 0) texY = 0; if (texY >= SPR_H) texY = SPR_H - 1;
                unsigned int c = sprData[texY * SPR_W + texX];
                if (c != 0) {
                    if (e->type == ENT_BARREL) {
                        /* Explosive Red Barrel with yellow hazard band */
                        int lum = (c & 0xFF);
                        if (texY >= 26 && texY <= 34) c = RGB(240, 200, 40);
                        else c = RGB(lum + 70 > 255 ? 255 : lum + 70, lum / 4, lum / 4);
                    } else if (e->type == ENT_HERB_R) {
                        /* Tint green herb leaves red */
                        int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
                        if (g > r + 20) c = RGB(g, r / 2, b / 2);
                    } else if (e->type == ENT_AMMO) {
                        c = RGB(90, 175, 240);
                    } else if (e->type == ENT_PESETAS) {
                        c = RGB(255, 215, 70);
                    }
                    backbuf[y * SCR_W + stripe] = fog_shade(c, transY, e->hitFlash);
                }
            }
        }

        /* Stunned / Melee Kick Prompt! */
        if (e->stunTimer > 0 && transY < 4.5f && sprScreenX > 40 && sprScreenX < SCR_W - 40) {
            fill_rect(sprScreenX - 34, clipStartY - 12, 68, 11, RGB(180, 35, 35));
            draw_text(sprScreenX - 30, clipStartY - 10, "[X] PATADA!", RGB(255, 240, 150));
        }
        /* Merchant Prompt */
        if (e->type == ENT_MERCHANT && transY < 3.5f && sprScreenX > 50 && sprScreenX < SCR_W - 50) {
            fill_rect(sprScreenX - 44, clipStartY - 12, 88, 11, RGB(65, 25, 95));
            draw_text(sprScreenX - 40, clipStartY - 10, "[^] BUHONERO", RGB(230, 180, 255));
        }
    }

    /* 3b. Explosion Fireball & Blood/Hit Sparks */
    if (ui.explosionTimer > 0) {
        int rad = (16 - ui.explosionTimer) * 7;
        fill_rect(SCR_W / 2 - rad, horizon - rad / 2, rad * 2, rad, RGB(255, 120, 20));
        fill_rect(SCR_W / 2 - rad / 2, horizon - rad / 3, rad, (rad * 2) / 3, RGB(255, 235, 90));
    }
    if (ui.hitSparkTimer > 0) {
        int sp = (8 - ui.hitSparkTimer) * 3;
        for (int k = -1; k <= 1; k += 2) {
            fill_rect(ui.hitSparkX + k * sp, ui.hitSparkY - sp, 3, 3, RGB(220, 25, 25));
            fill_rect(ui.hitSparkX - k * sp, ui.hitSparkY + sp, 2, 2, RGB(180, 15, 15));
        }
    }

    /* 4. Red Laser Sight / Sniper Scope & Distinct Weapon Viewmodels */
    int aimScrX = SCR_W / 2 + player.aimX;
    int aimScrY = horizon + player.aimY;
    int walkSwing = (int)(sinf(rf * 0.18f) * 3.0f);
    int gunBaseX = (player.aiming ? 260 : 310) + walkSwing;
    int gunBaseY = SCR_H - 110 + (player.fireTimer > 0 ? -12 : 0) + (player.reloadTimer > 0 ? 24 : 0);

    if (player.aiming && player.weapon == WPN_RIFLE) {
        /* Telescopic Sniper Scope Overlay */
        fill_rect(0, 0, 90, SCR_H, RGB(8, 10, 14));
        fill_rect(SCR_W - 90, 0, 90, SCR_H, RGB(8, 10, 14));
        draw_line(90, aimScrY, SCR_W - 90, aimScrY, RGB(40, 220, 90));
        draw_line(aimScrX, 20, aimScrX, SCR_H - 20, RGB(40, 220, 90));
        draw_rect_outline(90, 16, SCR_W - 180, SCR_H - 32, RGB(40, 220, 90));
        fill_rect(aimScrX - 2, aimScrY - 2, 5, 5, RGB(255, 40, 40));
    } else {
        if (player.aiming && player.reloadTimer <= 0 && player.weapon != WPN_KNIFE) {
            draw_line(gunBaseX + 26, gunBaseY + 34, aimScrX, aimScrY, RGB(220, 25, 25));
            fill_rect(aimScrX - 2, aimScrY - 2, 5, 5, RGB(255, 40, 40));
            put_px(aimScrX, aimScrY, RGB(255, 240, 240));
        }

        /* Muzzle Flash */
        if (player.fireTimer > 3 && player.weapon != WPN_KNIFE) {
            int mx = player.aiming ? aimScrX : (gunBaseX + 24);
            int my = player.aiming ? aimScrY : (gunBaseY + 24);
            int mfSz = (player.weapon == WPN_SHOTGUN) ? 14 : 8;
            fill_rect(mx - mfSz, my - mfSz, mfSz * 2, mfSz * 2, RGB(255, 190, 60));
            fill_rect(mx - mfSz / 2, my - mfSz / 2, mfSz, mfSz, RGB(255, 255, 220));
        }

        /* Roundhouse Kick Flash */
        if (player.kickTimer > 0) {
            int kx = SCR_W / 2 - 40 + (12 - player.kickTimer) * 8;
            fill_rect(kx, horizon + 10, 44, 18, RGB(85, 55, 35));
            fill_rect(kx + 34, horizon + 6, 18, 24, RGB(35, 30, 28));
        }

        /* Distinct Weapon Extensions (Knife blade, Shotgun pump barrel, Rifle barrel) */
        if (player.weapon == WPN_KNIFE) {
            int slashOff = (player.fireTimer > 0) ? (14 - player.fireTimer) * 7 : 0;
            int kx = gunBaseX + 18 - slashOff;
            int ky = gunBaseY + 26;
            fill_rect(kx, ky, 10, 34, RGB(195, 205, 220));
            fill_rect(kx + 2, ky - 14, 6, 14, RGB(230, 240, 255));
        } else if (player.weapon == WPN_SHOTGUN) {
            fill_rect(gunBaseX - 18, gunBaseY + 10, 44, 12, RGB(65, 70, 78));
            fill_rect(gunBaseX - 10, gunBaseY + 20, 36, 10, RGB(120, 72, 36));
        } else if (player.weapon == WPN_RIFLE) {
            fill_rect(gunBaseX - 32, gunBaseY + 12, 58, 8, RGB(50, 55, 62));
            fill_rect(gunBaseX - 10, gunBaseY + 4, 24, 7, RGB(30, 35, 42));
        }

        /* Draw Gloved Hands + Base Weapon 2D Sprite */
        for (int y = 0; y < SPR_H; y++) {
            for (int x = 0; x < SPR_W; x++) {
                if (player.weapon == WPN_KNIFE && y < 26) continue;
                unsigned int c = spr_gunhand[y * SPR_W + x];
                if (c != 0) {
                    int sx = gunBaseX + x * 2 + shakeX;
                    int sy = gunBaseY + y * 2;
                    put_px(sx, sy, c);
                    put_px(sx + 1, sy, c);
                    put_px(sx, sy + 1, c);
                    put_px(sx + 1, sy + 1, c);
                }
            }
        }
    }

    /* Damage Red Vignette */
    if (player.damageFlash > 0 || (player.hp * 100 < player.maxHp * 30 && ((rf / 18) & 1))) {
        fill_rect(0, 0, SCR_W, 8, RGB(200, 20, 20));
        fill_rect(0, SCR_H - 8, SCR_W, 8, RGB(200, 20, 20));
        fill_rect(0, 0, 8, SCR_H, RGB(200, 20, 20));
        fill_rect(SCR_W - 8, 0, 8, SCR_H, RGB(200, 20, 20));
    }

    /* 5. RESIDENT EVIL 4 HUD */
    static const char *wpnNames[4] = {"CUCHILLO", "PISTOLA 9MM", "ESCOPETA", "RIFLE"};
    static const char *stgNames[5] = {"", "PUEBLO", "IGLESIA", "CASTILLO", "LABORATORIO"};

    /* Top-left Stage & Pesetas */
    fill_rect(6, 6, 195, 22, RGB(18, 20, 26));
    draw_rect_outline(6, 6, 195, 22, RGB(95, 85, 70));
    sprintf(buf, "ZONA %d: %s | %d Pts", player.stage, stgNames[player.stage], player.pesetas);
    draw_text(12, 13, buf, RGB(240, 215, 130));

    /* Minimap (top-right, 2px per cell) */
    {
        int mmx = SCR_W - 58, mmy = 6;
        fill_rect(mmx - 2, mmy - 2, MAP_W * 2 + 4, MAP_H * 2 + 4, RGB(10, 12, 16));
        draw_rect_outline(mmx - 2, mmy - 2, MAP_W * 2 + 4, MAP_H * 2 + 4, RGB(95, 85, 70));
        for (int my = 0; my < MAP_H; my++) {
            for (int mx = 0; mx < MAP_W; mx++) {
                int wt = worldMap[my * MAP_W + mx];
                if (wt == W_EMPTY) continue;
                unsigned int mc = (wt == W_GATE_DOOR) ? RGB(220, 60, 60) : RGB(105, 100, 110);
                fill_rect(mmx + mx * 2, mmy + my * 2, 2, 2, mc);
            }
        }
        for (int i = 0; i < MAX_ENTS; i++) {
            if (!ents[i].alive) continue;
            unsigned int ec = 0;
            int sz = 2;
            if (ents[i].type == ENT_VILLAGER) ec = RGB(220, 70, 60);
            else if (ents[i].type == ENT_CHAINSAW || ents[i].type == ENT_BOSS) { ec = RGB(255, 140, 30); sz = 3; }
            else if (ents[i].type == ENT_MERCHANT) ec = RGB(190, 120, 255);
            if (ec) fill_rect(mmx + (int)(ents[i].x * 2.0f), mmy + (int)(ents[i].y * 2.0f), sz, sz, ec);
        }
        int pmx = mmx + (int)(player.x * 2.0f);
        int pmy = mmy + (int)(player.y * 2.0f);
        fill_rect(pmx - 1, pmy - 1, 3, 3, RGB(250, 235, 90));
        draw_line(pmx, pmy, pmx + (int)(cosf(player.ang) * 5.0f), pmy + (int)(sinf(player.ang) * 5.0f), RGB(250, 235, 90));
    }

    /* Objective line & Boss Health Bar */
    {
        int bossIdx = -1;
        for (int i = 0; i < MAX_ENTS; i++) {
            if (ents[i].alive && (ents[i].type == ENT_CHAINSAW || ents[i].type == ENT_BOSS)) {
                bossIdx = i;
                break;
            }
        }
        fill_rect(6, 31, 195, 13, RGB(18, 20, 26));
        draw_text(12, 34, bossIdx >= 0 ? "OBJETIVO: Derrota al Jefe" : "OBJETIVO: Cruza la puerta roja",
                  bossIdx >= 0 ? RGB(255, 150, 90) : RGB(150, 240, 170));

        if (bossIdx >= 0) {
            Entity *b = &ents[bossIdx];
            float dx = b->x - player.x, dy = b->y - player.y;
            if (dx * dx + dy * dy < 13.0f * 13.0f) {
                fill_rect(206, 6, 208, 22, RGB(24, 16, 20));
                draw_rect_outline(206, 6, 208, 22, RGB(220, 60, 60));
                draw_text(212, 9, b->type == ENT_BOSS ? "JEFE: LORD SADDLER" : "JEFE: DR. SALVADOR", RGB(255, 220, 130));
                int bw = (b->hp * 196) / (b->maxHp > 0 ? b->maxHp : 1);
                if (bw < 0) bw = 0; if (bw > 196) bw = 196;
                fill_rect(212, 19, 196, 5, RGB(45, 20, 24));
                fill_rect(212, 19, bw, 5, RGB(225, 50, 65));
            }
        }
    }

    /* Bottom-right RE4 Health Gauge & Ammo Counter */
    fill_rect(SCR_W - 156, SCR_H - 42, 150, 36, RGB(18, 22, 26));
    draw_rect_outline(SCR_W - 156, SCR_H - 42, 150, 36, RGB(80, 140, 95));
    int hpW = (player.hp * 138) / (player.maxHp > 0 ? player.maxHp : 100);
    if (hpW < 0) hpW = 0; if (hpW > 138) hpW = 138;
    fill_rect(SCR_W - 150, SCR_H - 36, 138, 9, RGB(35, 45, 38));
    fill_rect(SCR_W - 150, SCR_H - 36, hpW, 9, player.hp > 35 ? RGB(75, 210, 95) : RGB(220, 55, 55));

    if (player.weapon == WPN_KNIFE) {
        sprintf(buf, "%s (INF)", wpnNames[0]);
    } else {
        sprintf(buf, "%s %d/%d", wpnNames[player.weapon], player.mag[player.weapon], player.reserve[player.weapon]);
    }
    draw_text(SCR_W - 150, SCR_H - 21, buf, RGB(230, 235, 240));

    /* Bottom-left Message Toast */
    if (ui.msgTimer > 0) {
        fill_rect(6, SCR_H - 26, 260, 20, RGB(18, 20, 26));
        draw_rect_outline(6, SCR_H - 26, 260, 20, RGB(180, 60, 60));
        draw_text(12, SCR_H - 19, ui.msg, RGB(255, 225, 160));
    }

    /* BUHONERO (MERCHANT) SHOP MODAL */
    if (ui.shopOpen) {
        fill_rect(50, 36, 380, 196, RGB(20, 16, 28));
        draw_rect_outline(50, 36, 380, 196, RGB(190, 120, 255));
        sprintf(buf, "EL BUHONERO - Que vas a comprar? (%d Pts)", player.pesetas);
        draw_text(60, 44, buf, RGB(230, 180, 255));
        const char *sitems[6] = {
            "Escopeta Corredera (1500 Pts) - Gran Dispersion",
            "Rifle Francotirador (2500 Pts) - Alto Calibre",
            "Mejorar Potencia Arma Actual (800 Pts) - +Dano",
            "Caja Municion Mixta (400 Pts) - +15 Balas/+6 Cart",
            "Spray Primeros Auxilios (500 Pts) - Cura 100%",
            "Chaleco Tactico (1200 Pts) - Reduce Dano Recibido"
        };
        for (int i = 0; i < 6; i++) {
            int iy = 62 + i * 26;
            fill_rect(60, iy, 360, 22, i == ui.shopSel ? RGB(75, 40, 110) : RGB(32, 26, 44));
            draw_rect_outline(60, iy, 360, 22, i == ui.shopSel ? RGB(235, 190, 255) : RGB(85, 65, 110));
            draw_text(66, iy + 7, sitems[i], RGB(240, 235, 250));
        }
    }

    /* ATTACHE CASE / INVENTORY MODAL */
    if (ui.invOpen) {
        fill_rect(60, 44, 360, 180, RGB(20, 24, 28));
        draw_rect_outline(60, 44, 360, 180, RGB(120, 220, 140));
        draw_text(70, 52, "MALETIN TACTICO ([X] Usar/Equipar, [O] Cerrar)", RGB(140, 235, 160));
        char iitems[5][56];
        snprintf(iitems[0], 56, "Usar Hierba Verde (Tienes: %d) - +45 HP", player.herbsG);
        snprintf(iitems[1], 56, "Mezclar Hierba Verde+Roja (%dG/%dR) - 100%% +HP Max", player.herbsG, player.herbsR);
        snprintf(iitems[2], 56, "Usar Spray Auxilios (Tienes: %d) - Cura Total", player.sprays);
        snprintf(iitems[3], 56, "Cambiar Arma: %s (Pot.Nv.%d)", wpnNames[player.weapon], player.powLv[player.weapon]);
        snprintf(iitems[4], 56, "Guardar Partida (Bajas: %d | Blindaje Nv.%d)", player.kills, player.armorLv);
        for (int i = 0; i < 5; i++) {
            int iy = 72 + i * 28;
            fill_rect(70, iy, 340, 22, i == ui.invSel ? RGB(40, 75, 55) : RGB(28, 34, 38));
            draw_rect_outline(70, iy, 340, 22, i == ui.invSel ? RGB(150, 245, 170) : RGB(70, 90, 80));
            draw_text(76, iy + 7, iitems[i], RGB(235, 245, 235));
        }
    }

    if (player.deadTimer > 0) {
        int a = 160 - player.deadTimer;
        int shade = (a > 100) ? 100 : a;
        for (int y = 0; y < SCR_H; y++) {
            unsigned int *row = &backbuf[y * SCR_W];
            for (int x = 0; x < SCR_W; x++) {
                unsigned int c = row[x];
                int r = ((c & 0xFF) * (100 - shade)) / 100 + (shade * 90) / 100;
                int g = (((c >> 8) & 0xFF) * (100 - shade)) / 100;
                int b = (((c >> 16) & 0xFF) * (100 - shade)) / 100;
                row[x] = RGB(r, g, b);
            }
        }
        draw_text(176, 120, "H A S   M U E R T O", RGB(255, 235, 220));
        draw_text(150, 140, "Reapareceras en esta zona...", RGB(235, 200, 190));
    }

    sceDisplayWaitVblankStart();
    for (int y = 0; y < SCR_H; y++) {
        memcpy(&vram_base[y * VRAM_STRIDE], &backbuf[y * SCR_W], SCR_W * 4);
    }
}
