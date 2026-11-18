#include "game.h"
#include "font5x7.h"
#include "sprites.h"
#include "sprites_extra.h"
#include "sprites_boat.h"
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

static int is_wall_at(int wx, int wy) {
    if (wx < 0 || wy < 0 || wx >= WORLD_SIZE || wy >= WORLD_SIZE) return 0;
    return world[wy * WORLD_SIZE + wx] == T_WALL;
}

static void draw_wood_wall(int sx, int sy, int wx, int wy, int hr) {
    int u = is_wall_at(wx, wy - 1);
    int d = is_wall_at(wx, wy + 1);
    int l = is_wall_at(wx - 1, wy);
    int r = is_wall_at(wx + 1, wy);
    if (l || r) blit_sprite(sx, sy, spr_wall_h, hr, 0, 0);
    if (u || d) blit_sprite(sx, sy, spr_wall_v, hr, 0, 0);
    if ((!l && !r && !u && !d) || ((l || r) && (u || d)) || (l != r && !u && !d)) {
        blit_sprite(sx, sy, spr_wall_s, hr, 0, 0);
    }
}

static void draw_tile_art(int sx, int sy, int wx, int wy, int t, int hr) {
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
        case T_WALL:   draw_wood_wall(sx, sy, wx, wy, hr); break;
        case T_FIRE:   blit_sprite(sx, sy, spr_fire, hr, flick, -flick); break;
        case T_DUNGEON_DOOR: /* Surface Crypt */
            fill_rect(sx + 2, sy + 1, 16, 18, tint_night(RGB(95, 92, 106), hr));
            fill_rect(sx + 4, sy + 1, 12, 3, tint_night(RGB(125, 122, 138), hr));
            fill_rect(sx + 6, sy + 8, 8, 11, RGB(14, 12, 20));
            draw_rect_outline(sx + 2, sy + 1, 16, 18, tint_night(RGB(52, 50, 62), hr));
            fill_rect(sx + 9, sy + 3, 2, 4, tint_night(RGB(205, 195, 160), hr));
            fill_rect(sx + 8, sy + 4, 4, 1, tint_night(RGB(205, 195, 160), hr));
            break;
        case T_STAIRS_DOWN:
            fill_rect(sx + 2, sy + 3, 16, 14, RGB(22, 20, 28));
            fill_rect(sx + 4, sy + 5, 12, 3, RGB(70, 65, 82));
            fill_rect(sx + 6, sy + 9, 10, 3, RGB(50, 46, 60));
            fill_rect(sx + 8, sy + 13, 8, 3, RGB(34, 30, 42));
            draw_rect_outline(sx + 2, sy + 3, 16, 14, RGB(210, 170, 60));
            break;
        case T_DWALL:
            fill_rect(sx, sy, 20, 20, RGB(58, 56, 68));
            fill_rect(sx + 1, sy + 1, 8, 8, RGB(78, 76, 90));
            fill_rect(sx + 11, sy + 1, 8, 8, RGB(72, 70, 84));
            fill_rect(sx + 5, sy + 11, 10, 8, RGB(76, 74, 88));
            draw_rect_outline(sx, sy, 20, 20, RGB(34, 32, 42));
            break;
        case T_EXIT:
            fill_rect(sx + 2, sy + 3, 16, 14, RGB(40, 38, 48));
            fill_rect(sx + 4, sy + 5, 12, 3, RGB(140, 120, 85));
            fill_rect(sx + 4, sy + 9, 12, 3, RGB(115, 98, 70));
            fill_rect(sx + 4, sy + 13, 12, 3, RGB(90, 76, 54));
            break;
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

    /* TITLE / START MENU */
    if (ui.inTitle) {
        static int tf = 0;
        tf++;
        /* Night twilight sky & stars */
        fill_rect(0, 0, SCR_W, 190, RGB(18, 26, 48));
        fill_rect(0, 110, SCR_W, 80, RGB(28, 42, 72));
        for (int s = 0; s < 24; s++) {
            int sx = (s * 73 + 19) % 470;
            int sy = (s * 41 + 11) % 100;
            if (((tf / 15 + s) % 4) != 0) put_px(sx, sy, RGB(255, 245, 200));
        }
        /* Distant mountains & grassy meadow */
        fill_rect(0, 190, SCR_W, 82, RGB(42, 92, 46));
        fill_rect(0, 190, SCR_W, 3, RGB(70, 140, 68));
        /* Decorative sprites on meadow */
        blit_sprite(40, 174, spr_pine, 12, 0, 0);
        blit_sprite(70, 178, spr_tree, 12, 0, 0);
        blit_sprite(380, 176, spr_ruin, 12, 0, 0);
        blit_sprite(415, 178, spr_pine, 12, 0, 0);
        blit_sprite(216, 182, spr_p_down, 12, 0, 0);
        blit_sprite(244, 184, spr_fire, 12, (tf / 8) & 1, 0);

        /* Title Banner */
        fill_rect(100, 26, 280, 44, RGB(19, 22, 32));
        draw_rect_outline(100, 26, 280, 44, RGB(255, 226, 122));
        draw_text(154, 36, "P I X E L A R I A", RGB(255, 226, 122));
        draw_text(176, 52, "POCKET - PSP EDITION", RGB(166, 216, 255));

        if (ui.showHelp) {
            fill_rect(60, 82, 360, 152, RGB(20, 24, 34));
            draw_rect_outline(60, 82, 360, 152, RGB(142, 232, 164));
            draw_text(74, 92, "CONTROLES PSP:", RGB(255, 226, 122));
            draw_text(74, 108, "Joystick: Mover directo  | Cruceta: Apuntar y andar", RGB(230, 235, 245));
            draw_text(74, 124, "[X]: Usar / Espada Zelda | [Cuad / L / R]: Slot", RGB(230, 235, 245));
            draw_text(74, 140, "[Triangulo]: Craftear    | [Circulo]: Habilidades", RGB(230, 235, 245));
            draw_text(74, 156, "[START]: Guardar         | [SELECT]: Musica/SFX", RGB(230, 235, 245));
            draw_text(74, 178, "Explora Criptas (10 pisos), forja armas y navega!", RGB(166, 216, 255));
            draw_text(154, 212, "Pulsa [X] o [O] para volver", RGB(142, 232, 164));
        } else {
            const char *opts[3] = {"Continuar Partida", "Nueva Partida", "Controles y Guia"};
            for (int i = 0; i < 3; i++) {
                int oy = 90 + i * 28;
                fill_rect(140, oy, 200, 22, i == ui.titleSel ? RGB(54, 65, 89) : RGB(24, 28, 38));
                draw_rect_outline(140, oy, 200, 22, i == ui.titleSel ? RGB(255, 226, 122) : RGB(75, 86, 112));
                draw_text(168, oy + 7, opts[i], i == ui.titleSel ? RGB(255, 226, 122) : RGB(230, 235, 245));
            }
            draw_text(146, 252, "Cruceta: Elegir   [X]: Aceptar", RGB(220, 235, 220));
        }

        sceDisplayWaitVblankStart();
        for (int y = 0; y < SCR_H; y++) {
            memcpy(&vram_base[y * VRAM_STRIDE], &backbuf[y * SCR_W], SCR_W * 4);
        }
        return;
    }

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

    int mapW = player.inDungeon ? DUNG_W : WORLD_SIZE;
    int mapH = player.inDungeon ? DUNG_H : WORLD_SIZE;
    unsigned char *curMap = player.inDungeon ? dungeon : world;
    Mob *curMobs = player.inDungeon ? dungMobs : mobs;
    int renderHr = player.inDungeon ? 12 : hr;

    int camX = player.x - VIEW_W / 2;
    int camY = player.y - VIEW_H / 2;
    if (camX < 0) camX = 0;
    if (camY < 0) camY = 0;
    if (camX > mapW - VIEW_W) camX = mapW - VIEW_W;
    if (camY > mapH - VIEW_H) camY = mapH - VIEW_H;

    for (int vy = 0; vy < VIEW_H; vy++) {
        for (int vx = 0; vx < VIEW_W; vx++) {
            int wx = camX + vx;
            int wy = camY + vy;
            int idx = wy * mapW + wx;
            int sx = ox + vx * TILE_PX;
            int sy = oy + vy * TILE_PX;
            if (player.inDungeon) {
                unsigned int dcol = ((wx + wy) & 1) ? RGB(36, 34, 44) : RGB(42, 40, 52);
                fill_rect(sx, sy, TILE_PX, TILE_PX, dcol);
            } else {
                draw_textured_ground(sx, sy, wx, wy, curMap[idx], biomes[idx], renderHr);
            }
            draw_tile_art(sx, sy, wx, wy, curMap[idx], renderHr);
        }
    }

    /* Enemies (Slimes & Dungeon Skeletons) */
    int mobBob = (gameTime.ticks & 1) ? -1 : 0;
    for (int i = 0; i < MAX_MOBS; i++) {
        if (!curMobs[i].alive) continue;
        int sx = ox + (curMobs[i].x - camX) * TILE_PX;
        int sy = oy + (curMobs[i].y - camY) * TILE_PX;
        if (sx >= ox && sx < ox + 320 && sy >= oy && sy < oy + 200) {
            const unsigned int *mspr = (curMobs[i].type == MOB_SKELETON) ? spr_skeleton : spr_slime;
            blit_sprite(sx, sy, mspr, renderHr, (i & 1), mobBob);
        }
    }

    /* Player (4-directional sprites + walk bob + step dust + Zelda slash) */
    int walkBob = (ui.walkTimer > 3) ? -2 : 0;
    int px = ox + (player.x - camX) * TILE_PX + (ui.lungeTimer > 0 ? player.fx * 4 : 0);
    int py = oy + (player.y - camY) * TILE_PX + (ui.lungeTimer > 0 ? player.fy * 4 : 0) + walkBob;
    if (ui.walkTimer > 0 && !player.inBoat) {
        int dustX = ox + (player.x - camX) * TILE_PX + (ui.walkFrame ? 6 : 11);
        int dustY = oy + (player.y - camY) * TILE_PX + 17;
        fill_rect(dustX, dustY, 3, 2, tint_night(RGB(210, 210, 190), renderHr));
    }
    if (player.inBoat) {
        int bx = ox + (player.x - camX) * TILE_PX;
        int by = oy + (player.y - camY) * TILE_PX + ((gameTime.ticks & 1) ? 1 : 0);
        if (player.fx != 0) {
            blit_sprite(bx, by, spr_boat_h, renderHr, player.fx > 0, 0);
        } else {
            blit_sprite(bx, by, spr_boat_v, renderHr, 0, 0);
        }
        fill_rect(bx - player.fx * 4 + 7, by - player.fy * 4 + 10, 5, 2, RGB(210, 238, 255));
        py -= 2;
    }
    const unsigned int *pspr = spr_p_down;
    if (player.fy < 0) pspr = spr_p_up;
    else if (player.fx < 0) pspr = spr_p_left;
    else if (player.fx > 0) pspr = spr_p_right;
    if ((player.iframes & 2) == 0) {
        blit_sprite(px, py, pspr, ui.damageFlash > 0 ? 22 : renderHr, 0, 0);
    }

    /* Zelda Game Boy wide sweeping sword slash arc */
    if (ui.slashTimer > 0) {
        static const unsigned int swCols[4] = {
            RGB(210, 165, 95),  /* Madera */
            RGB(215, 232, 250), /* Hierro */
            RGB(130, 215, 255), /* Acero */
            RGB(255, 225, 70)   /* Real */
        };
        unsigned int sc = swCols[player.swordTier & 3];
        int cx = ox + (player.x - camX) * TILE_PX + 10;
        int cy = oy + (player.y - camY) * TILE_PX + 10;
        int phase = 8 - ui.slashTimer; /* 0..7 sweep */
        for (int a = -1; a <= 1; a++) {
            int sx = cx + player.fx * 18 + (player.fy != 0 ? a * (10 + phase) : 0);
            int sy = cy + player.fy * 18 + (player.fx != 0 ? a * (10 + phase) : 0);
            fill_rect(sx - 2, sy - 2, 5, 5, sc);
            fill_rect(sx - 1, sy - 1, 3, 3, RGB(255, 255, 255));
        }
    } else if (player.slot < 3) {
        int toolX = px + (player.fx < 0 ? 1 : 16);
        int toolY = py + (ui.lungeTimer > 0 ? 4 : 8);
        unsigned int toolCol = (player.slot == 0 && player.swordTier >= 2) ? RGB(130, 215, 255) :
                               (player.ironTools ? RGB(215, 236, 255) : RGB(205, 210, 220));
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

    /* Weather particles (Overworld only) */
    if (!player.inDungeon && gameTime.weather > 0) {
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

    /* Built-in Player Lantern Darkness Pass (Dungeon & Night) */
    if (player.inDungeon || hr >= 20 || hr < 5) {
        int lcx = px + 10;
        int lcy = py + 10;
        int r1 = player.inDungeon ? 2025 : 3600; /* 45px / 60px full light */
        int r2 = player.inDungeon ? 7225 : 10000; /* 85px / 100px outer fade */
        int minMul = player.inDungeon ? 14 : 45;
        for (int yy = 34; yy < 234; yy++) {
            unsigned int *row = &backbuf[yy * SCR_W];
            int dy = yy - lcy;
            int dy2 = dy * dy;
            for (int xx = 4; xx < 324; xx++) {
                int dx = xx - lcx;
                int d2 = dx * dx + dy2;
                if (d2 > r1) {
                    int mul = minMul;
                    if (d2 < r2) {
                        mul = minMul + ((100 - minMul) * (r2 - d2)) / (r2 - r1);
                    }
                    unsigned int c = row[xx];
                    int r = ((c & 0xFF) * mul) / 100;
                    int g = (((c >> 8) & 0xFF) * mul) / 100;
                    int b = (((c >> 16) & 0xFF) * mul) / 100;
                    row[xx] = RGB(r, g, b);
                }
            }
        }
    }

    /* Target cursor */
    int tx = player.x + player.fx;
    int ty = player.y + player.fy;
    if (tx >= 0 && tx < mapW && ty >= 0 && ty < mapH) {
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

    static const char *swNames[4] = {"Mad", "Hie", "Ace", "Real"};
    sprintf(buf, "Mad:%d Pie:%d", player.wood, player.stone);
    draw_text(332, 110, buf, RGB(230, 235, 245));
    sprintf(buf, "Hie:%d Hue:%d", player.iron, player.bones);
    draw_text(332, 122, buf, RGB(255, 210, 158));
    if (player.inDungeon) {
        sprintf(buf, "Esp:%s %d%% P:%d/10", swNames[player.swordTier & 3], player.swordPct, player.dungeonFloor);
    } else {
        sprintf(buf, "Esp:%s %d%%", swNames[player.swordTier & 3], player.swordPct);
    }
    draw_text(332, 134, buf, RGB(255, 226, 122));

    draw_text(332, 150, "[X] Usar   [L/R] Slot", RGB(160, 175, 200));
    draw_text(332, 163, "[^] Craft  [O] Stats", RGB(160, 175, 200));
    draw_text(332, 176, "[START] Guardar", RGB(160, 175, 200));
    draw_text(332, 189, "[SEL] SFX On/Off", RGB(160, 175, 200));

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
        fill_rect(44, 34, 392, 204, RGB(22, 26, 36));
        draw_rect_outline(44, 34, 392, 204, RGB(255, 226, 122));
        draw_text(54, 41, "CRAFTEO & FORJA ([X] Crear, [^] Cerrar)", RGB(255, 226, 122));
        const char *items[7] = {
            "Hoguera (3 Mad, 2 Pie) - Seca ropa y cura",
            "Muro x2 (2 Mad, 1 Pie) - Refugio conectable",
            "Racion (2 Bayas, 1 Mad) - +45 Hambre",
            "Herramientas Hierro (4 Hie, 2 Mad) - 2x",
            "Mejorar Espada (3 Hie, 3 Hue) - Sube Tier!",
            "Barca de Madera (5 Mad) - Navegar agua",
            "Descansar (Junto a Hoguera) - +Energia/Dia"
        };
        for (int i = 0; i < 7; i++) {
            int iy = 56 + i * 24;
            fill_rect(54, iy, 372, 20, i == ui.menuSel ? RGB(54, 65, 89) : RGB(35, 41, 56));
            draw_rect_outline(54, iy, 372, 20, i == ui.menuSel ? RGB(255, 226, 122) : RGB(75, 86, 112));
            draw_text(60, iy + 6, items[i], RGB(240, 240, 240));
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
