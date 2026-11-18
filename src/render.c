#include "game.h"
#include "font5x7.h"
#include "sprites.h"
#include <pspdisplay.h>
#include <stdio.h>
#include <string.h>

#define VRAM_STRIDE 512
/* PSP 32-bit ABGR color macro */
#define RGB(r,g,b) ((unsigned int)(0xFF000000 | ((b)<<16) | ((g)<<8) | (r)))

static unsigned int *vram_base = (unsigned int *)0x44000000;
static unsigned int backbuf[SCR_W * SCR_H];

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

static unsigned int ground_color(int t, int b) {
    if (t == T_WATER) return RGB(44, 115, 201);
    if (t == T_SAND || t == T_CACTUS || t == T_RUIN || t == T_CHEST) return RGB(223, 193, 123);
    if (t == T_SNOW) return RGB(220, 230, 242);
    if (t == T_FOREST) {
        if (gameTime.season == 2) return RGB(138, 112, 59);
        if (gameTime.season == 3) return RGB(184, 199, 217);
        return RGB(59, 110, 60);
    }
    if (b == B_TUNDRA) return RGB(220, 230, 242);
    if (b == B_DESERT) return RGB(223, 193, 123);
    if (gameTime.season == 2) return RGB(140, 156, 70);
    if (gameTime.season == 3) return RGB(199, 212, 226);
    return RGB(76, 166, 76);
}

static unsigned int tint_night(unsigned int col, int hr) {
    int r = col & 0xFF;
    int g = (col >> 8) & 0xFF;
    int b = (col >> 16) & 0xFF;
    if (hr >= 20 || hr < 5) {
        r = (r * 55) / 100;
        g = (g * 60) / 100;
        b = (b * 85) / 100;
    } else if (hr >= 18 || hr == 5) {
        r = (r * 92) / 100;
        g = (g * 78) / 100;
        b = (b * 72) / 100;
    }
    return RGB(r, g, b);
}

static void blit_sprite(int sx, int sy, const unsigned int *spr, int hr, int flipX, int bobY) {
    for (int y = 0; y < 20; y++) {
        int dy = sy + y + bobY;
        if (dy < 34 || dy >= 234) continue;
        for (int x = 0; x < 20; x++) {
            int dx = sx + x;
            if (dx < 4 || dx >= 324) continue;
            unsigned int c = spr[y * 20 + (flipX ? (19 - x) : x)];
            if (c != 0) {
                put_px(dx, dy, tint_night(c, hr));
            }
        }
    }
}

static void draw_textured_ground(int sx, int sy, int wx, int wy, int t, int b, int hr) {
    unsigned int base = ground_color(t, b);
    if (b == B_MEADOW && gameTime.season < 2 && t != T_WATER) {
        blit_sprite(sx, sy, spr_grass, hr, (wx ^ wy) & 1, 0);
        return;
    }
    fill_rect(sx, sy, TILE_PX, TILE_PX, tint_night(base, hr));
    unsigned int h = (unsigned int)(wx * 73856093 ^ wy * 19349663);
    if (t == T_WATER) {
        int flick = (gameTime.ticks & 1) ? 1 : 0;
        fill_rect(sx + 3, sy + 6 + flick, 6, 1, tint_night(RGB(140, 205, 255), hr));
        fill_rect(sx + 10, sy + 13 - flick, 6, 1, tint_night(RGB(120, 185, 245), hr));
    } else if (b == B_DESERT || t == T_SAND) {
        fill_rect(sx + (h % 13) + 2, sy + ((h >> 4) % 14) + 2, 4, 1, tint_night(RGB(201, 168, 98), hr));
        fill_rect(sx + ((h >> 8) % 14) + 2, sy + ((h >> 12) % 14) + 2, 2, 1, tint_night(RGB(238, 212, 148), hr));
    } else if (b == B_TUNDRA || t == T_SNOW) {
        fill_rect(sx + (h % 15) + 2, sy + ((h >> 4) % 15) + 2, 2, 2, tint_night(RGB(245, 250, 255), hr));
        fill_rect(sx + ((h >> 8) % 14) + 2, sy + ((h >> 12) % 14) + 2, 3, 1, tint_night(RGB(185, 205, 228), hr));
    } else {
        fill_rect(sx + (h % 14) + 2, sy + ((h >> 4) % 14) + 2, 2, 2, tint_night(RGB(42, 86, 44), hr));
        fill_rect(sx + ((h >> 8) % 13) + 2, sy + ((h >> 12) % 13) + 2, 3, 1, tint_night(RGB(82, 136, 78), hr));
    }
}

static void draw_tile_art(int sx, int sy, int t, int hr) {
    int flick = (gameTime.ticks & 1) ? 1 : 0;
    switch (t) {
        case T_TREE:   blit_sprite(sx, sy, spr_tree, hr, 0, 0); break;
        case T_PINE:   blit_sprite(sx, sy, spr_pine, hr, 0, 0); break;
        case T_ROCK:   blit_sprite(sx, sy, spr_rock, hr, 0, 0); break;
        case T_IRON:   blit_sprite(sx, sy, spr_iron, hr, 0, 0); break;
        case T_CACTUS: blit_sprite(sx, sy, spr_cactus, hr, 0, 0); break;
        case T_BERRY:  blit_sprite(sx, sy, spr_berry, hr, 0, 0); break;
        case T_RUIN:   blit_sprite(sx, sy, spr_ruin, hr, 0, 0); break;
        case T_CHEST:  blit_sprite(sx, sy, spr_chest, hr, 0, 0); break;
        case T_WALL:   blit_sprite(sx, sy, spr_wall, hr, 0, 0); break;
        case T_FIRE:   blit_sprite(sx, sy, spr_fire, hr, flick, -flick); break;
        default: break;
    }
}

void render_init(void) {
    sceDisplaySetMode(0, SCR_W, SCR_H);
    sceDisplaySetFrameBuf(vram_base, VRAM_STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
}

void render_frame(void) {
    static const char *bnames[4] = {"Pradera", "Bosque", "Desierto", "Tundra"};
    static const char *snames[4] = {"Primav", "Verano", "Otono", "Invierno"};
    static const char *wnames[3] = {"Sol", "Lluvia", "Nieve"};
    char buf[64];

    fill_rect(0, 0, SCR_W, SCR_H, RGB(22, 26, 35));

    /* TOP HUD */
    fill_rect(0, 0, SCR_W, 32, RGB(18, 22, 29));
    fill_rect(0, 31, SCR_W, 1, RGB(53, 59, 74));

    int mhp = maxHp();
    int hpW = (int)((player.hp / (float)mhp) * 76.0f);
    int hmW = (int)((player.hunger / 100.0f) * 76.0f);
    int sdW = (int)((player.thirst / 100.0f) * 76.0f);
    int enW = (int)((player.stamina / 100.0f) * 76.0f);

    draw_text(4, 5, "HP", RGB(210, 215, 230));
    fill_rect(20, 4, 78, 9, RGB(44, 49, 61));
    fill_rect(21, 5, hpW < 0 ? 0 : (hpW > 76 ? 76 : hpW), 7, RGB(217, 67, 67));

    draw_text(104, 5, "HM", RGB(210, 215, 230));
    fill_rect(120, 4, 78, 9, RGB(44, 49, 61));
    fill_rect(121, 5, hmW < 0 ? 0 : (hmW > 76 ? 76 : hmW), 7, RGB(224, 139, 54));

    draw_text(4, 19, "SD", RGB(210, 215, 230));
    fill_rect(20, 18, 78, 9, RGB(44, 49, 61));
    fill_rect(21, 19, sdW < 0 ? 0 : (sdW > 76 ? 76 : sdW), 7, RGB(59, 154, 225));

    draw_text(104, 19, "EN", RGB(210, 215, 230));
    fill_rect(120, 18, 78, 9, RGB(44, 49, 61));
    fill_rect(121, 19, enW < 0 ? 0 : (enW > 76 ? 76 : enW), 7, RGB(99, 199, 77));

    int hr = gameTime.minute / 60;
    int mn = gameTime.minute % 60;
    sprintf(buf, "Dia %d %02d:%02d Nv.%d", gameTime.day, hr, mn, totalLevel());
    draw_text(208, 5, buf, RGB(255, 226, 122));

    sprintf(buf, "%s | %s (%d,%d)", snames[gameTime.season], bnames[biomes[player.y * WORLD_SIZE + player.x]], player.x, player.y);
    draw_text(208, 19, buf, RGB(215, 222, 237));

    /* VIEWPORT (320x200 at 4,34) */
    int ox = 4 + (ui.shakeTimer > 0 ? ((ui.shakeTimer & 1) ? 2 : -2) : 0);
    int oy = 34 + (ui.shakeTimer > 0 ? ((ui.shakeTimer & 2) ? 1 : -1) : 0);

    int camX = player.x - VIEW_W / 2;
    int camY = player.y - VIEW_H / 2;
    if (camX < 0) camX = 0;
    if (camY < 0) camY = 0;
    if (camX > WORLD_SIZE - VIEW_W) camX = WORLD_SIZE - VIEW_W;
    if (camY > WORLD_SIZE - VIEW_H) camY = WORLD_SIZE - VIEW_H;

    for (int vy = 0; vy < VIEW_H; vy++) {
        for (int vx = 0; vx < VIEW_W; vx++) {
            int wx = camX + vx;
            int wy = camY + vy;
            int idx = wy * WORLD_SIZE + wx;
            int sx = ox + vx * TILE_PX;
            int sy = oy + vy * TILE_PX;
            draw_textured_ground(sx, sy, wx, wy, world[idx], biomes[idx], hr);
            draw_tile_art(sx, sy, world[idx], hr);
        }
    }

    /* Slimes */
    int slimeBob = (gameTime.ticks & 1) ? -1 : 0;
    for (int i = 0; i < MAX_MOBS; i++) {
        if (!mobs[i].alive) continue;
        int sx = ox + (mobs[i].x - camX) * TILE_PX;
        int sy = oy + (mobs[i].y - camY) * TILE_PX;
        if (sx >= ox && sx < ox + 320 && sy >= oy && sy < oy + 200) {
            blit_sprite(sx, sy, spr_slime, hr, (i & 1), slimeBob);
        }
    }

    /* Player with walk bob, step dust, and tool/build animation */
    int walkBob = (ui.walkTimer > 3) ? -2 : 0;
    int px = ox + (player.x - camX) * TILE_PX + (ui.lungeTimer > 0 ? player.fx * 4 : 0);
    int py = oy + (player.y - camY) * TILE_PX + (ui.lungeTimer > 0 ? player.fy * 4 : 0) + walkBob;
    if (ui.walkTimer > 0) {
        int dustX = ox + (player.x - camX) * TILE_PX + (ui.walkFrame ? 6 : 11);
        int dustY = oy + (player.y - camY) * TILE_PX + 17;
        fill_rect(dustX, dustY, 3, 2, tint_night(RGB(210, 210, 190), hr));
    }
    blit_sprite(px, py, spr_player, ui.damageFlash > 0 ? 22 : hr, player.fx < 0, 0);
    if (player.slot < 3) {
        int toolX = px + (player.fx < 0 ? 1 : 16);
        int toolY = py + (ui.lungeTimer > 0 ? 4 : 8);
        unsigned int toolCol = player.ironTools ? RGB(215, 236, 255) : RGB(205, 210, 220);
        fill_rect(toolX, toolY, 3, ui.lungeTimer > 0 ? 10 : 6, toolCol);
    }

    /* Break / Build impact particles on target tile */
    if (ui.lungeTimer > 0) {
        int ttx = ox + (player.x + player.fx - camX) * TILE_PX;
        int tty = oy + (player.y + player.fy - camY) * TILE_PX;
        int sp = 10 - ui.lungeTimer;
        unsigned int pcol = (ui.actionType == 2) ? RGB(255, 220, 90) : RGB(230, 185, 120);
        fill_rect(ttx + 8 - sp, tty + 8 - sp, 2, 2, pcol);
        fill_rect(ttx + 10 + sp, tty + 8 - sp, 2, 2, pcol);
        fill_rect(ttx + 8 - sp, tty + 11 + sp, 2, 2, pcol);
        fill_rect(ttx + 10 + sp, tty + 11 + sp, 2, 2, pcol);
    }

    /* Weather particles (Rain / Snow) */
    if (gameTime.weather > 0) {
        static int wf = 0;
        wf++;
        for (int p = 0; p < 24; p++) {
            int rx = ox + ((p * 53 + wf * 3) % 316);
            int ry = oy + ((p * 37 + wf * (gameTime.weather == 1 ? 6 : 2)) % 194);
            if (gameTime.weather == 1) {
                fill_rect(rx, ry, 1, 4, RGB(120, 180, 255));
            } else {
                fill_rect(rx, ry, 2, 2, RGB(240, 248, 255));
            }
        }
    }

    /* Target cursor */
    int tx = player.x + player.fx;
    int ty = player.y + player.fy;
    if (tx >= 0 && tx < WORLD_SIZE && ty >= 0 && ty < WORLD_SIZE) {
        draw_rect_outline(ox + (tx - camX) * TILE_PX, oy + (ty - camY) * TILE_PX, TILE_PX, TILE_PX, RGB(255, 226, 77));
    }

    draw_rect_outline(ox - 1, oy - 1, 322, 202, ui.damageFlash > 0 ? RGB(255, 60, 60) : RGB(64, 72, 89));

    /* Floating Popups */
    for (int i = 0; i < MAX_POPS; i++) {
        if (pops[i].timer > 0) {
            int sx = ox + (pops[i].wx - camX) * TILE_PX;
            int sy = oy + (pops[i].wy - camY) * TILE_PX - (30 - pops[i].timer) / 3;
            draw_text(sx, sy, pops[i].text, pops[i].color);
        }
    }

    /* RIGHT SIDEBAR (148x200 at 328,34) */
    fill_rect(328, 34, 148, 200, RGB(19, 22, 29));
    draw_rect_outline(328, 34, 148, 200, RGB(53, 59, 74));

    /* Minimap 16x16 (4x4px = 64x64 at 332,38) */
    int pCellX = player.x / 4;
    int pCellY = player.y / 4;
    for (int my = 0; my < 16; my++) {
        for (int mx = 0; mx < 16; mx++) {
            unsigned int mc = (mx == pCellX && my == pCellY) ? RGB(255, 50, 50)
                : ground_color(world[(my * 4 + 2) * WORLD_SIZE + (mx * 4 + 2)], biomes[(my * 4 + 2) * WORLD_SIZE + (mx * 4 + 2)]);
            fill_rect(332 + mx * 4, 38 + my * 4, 4, 4, mc);
        }
    }
    draw_rect_outline(331, 37, 66, 66, RGB(75, 84, 104));

    draw_text(402, 40, wnames[gameTime.weather], RGB(166, 216, 255));
    if (player.wet > 0) {
        fill_rect(400, 54, 72, 11, RGB(28, 78, 128));
        draw_text(402, 56, "Ropa mojada", RGB(255, 255, 255));
    }
    if (player.warm) {
        fill_rect(400, 68, 72, 11, RGB(128, 55, 18));
        draw_text(402, 70, "Hoguera OK", RGB(255, 255, 255));
    }

    sprintf(buf, "Mad:%d Pie:%d", player.wood, player.stone);
    draw_text(332, 110, buf, RGB(230, 235, 245));
    sprintf(buf, "Hie:%d", player.iron);
    draw_text(332, 122, buf, RGB(255, 210, 158));

    draw_text(332, 144, "[X] Usar   [L/R] Slot", RGB(160, 175, 200));
    draw_text(332, 158, "[^] Craft  [O] Stats", RGB(160, 175, 200));
    draw_text(332, 172, "[START] Guardar", RGB(160, 175, 200));
    draw_text(332, 186, "[SEL] SFX On/Off", RGB(160, 175, 200));

    /* BOTTOM HOTBAR */
    fill_rect(4, 237, 472, 32, RGB(19, 22, 29));
    draw_rect_outline(4, 237, 472, 32, RGB(53, 59, 74));

    const char *pfx = player.ironTools ? "H+" : "";
    char sbuf[6][16];
    sprintf(sbuf[0], "1:%sEsp", pfx);
    sprintf(sbuf[1], "2:%sHac", pfx);
    sprintf(sbuf[2], "3:%sPic", pfx);
    sprintf(sbuf[3], "4:Hog(%d)", player.campfires);
    sprintf(sbuf[4], "5:Mur(%d)", player.walls);
    sprintf(sbuf[5], "6:Bay(%d)", player.food);

    for (int s = 0; s < 6; s++) {
        int sx = 8 + s * 56;
        fill_rect(sx, 241, 54, 24, s == player.slot ? RGB(57, 70, 97) : RGB(34, 39, 51));
        draw_rect_outline(sx, 241, 54, 24, s == player.slot ? RGB(255, 226, 122) : RGB(69, 78, 99));
        draw_text(sx + 3, 249, sbuf[s], s == player.slot ? RGB(255, 226, 122) : RGB(230, 235, 245));
    }
    draw_text(348, 249, ui.msg, RGB(168, 230, 163));

    /* CRAFT MODAL */
    if (ui.craftOpen) {
        fill_rect(50, 42, 380, 180, RGB(22, 26, 36));
        draw_rect_outline(50, 42, 380, 180, RGB(255, 226, 122));
        draw_text(60, 50, "CRAFTEO & EQUIPO ([X] Crear, [^] Cerrar)", RGB(255, 226, 122));
        const char *items[5] = {
            "Hoguera (3 Mad, 2 Pie) - Seca ropa y cura",
            "Muro x2 (2 Mad, 1 Pie) - Refugio",
            "Racion (2 Bayas, 1 Mad) - +45 Hambre",
            "Herramientas Hierro (4 Hie, 2 Mad) - 2x",
            "Descansar (Junto a Hoguera) - +Energia/Dia"
        };
        for (int i = 0; i < 5; i++) {
            int iy = 72 + i * 26;
            fill_rect(60, iy, 360, 22, i == ui.menuSel ? RGB(54, 65, 89) : RGB(35, 41, 56));
            draw_rect_outline(60, iy, 360, 22, i == ui.menuSel ? RGB(255, 226, 122) : RGB(75, 86, 112));
            draw_text(66, iy + 7, items[i], RGB(240, 240, 240));
        }
    }

    /* STATS MODAL */
    if (ui.statsOpen) {
        fill_rect(50, 42, 380, 180, RGB(22, 26, 36));
        draw_rect_outline(50, 42, 380, 180, RGB(142, 232, 164));
        draw_text(60, 50, "HABILIDADES & ESTADISTICAS ([O] Cerrar)", RGB(142, 232, 164));
        Skill *sks[5] = {&skills.tala, &skills.mineria, &skills.combate, &skills.supervivencia, &skills.construccion};
        const char *skn[5] = {"Tala", "Mineria", "Combate", "Superv.", "Constr."};
        for (int i = 0; i < 5; i++) {
            int iy = 70 + i * 20;
            int req = nextXp(sks[i]->lv);
            int barW = (sks[i]->xp * 100) / (req > 0 ? req : 1);
            if (barW > 100) barW = 100;
            sprintf(buf, "%-8s Nv.%2d", skn[i], sks[i]->lv);
            draw_text(64, iy, buf, RGB(230, 235, 245));
            fill_rect(170, iy, 102, 8, RGB(19, 22, 29));
            fill_rect(171, iy + 1, barW, 6, RGB(99, 199, 77));
            sprintf(buf, "%d/%d XP", sks[i]->xp, req);
            draw_text(280, iy, buf, RGB(158, 203, 255));
        }
        sprintf(buf, "Pasos:%d  Arb:%d  Min:%d", stats.steps, stats.trees, stats.ores);
        draw_text(64, 178, buf, RGB(220, 220, 220));
        sprintf(buf, "Slimes:%d  Craft:%d  Cofres:%d", stats.slimes, stats.crafted, stats.chests);
        draw_text(64, 194, buf, RGB(220, 220, 220));
    }

    /* Blit backbuffer to PSP VRAM */
    sceDisplayWaitVblankStart();
    for (int y = 0; y < SCR_H; y++) {
        memcpy(&vram_base[y * VRAM_STRIDE], &backbuf[y * SCR_W], SCR_W * 4);
    }
}
