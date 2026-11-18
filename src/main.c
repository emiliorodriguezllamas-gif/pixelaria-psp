#include "game.h"
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

PSP_MODULE_INFO("BioVillage4", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

unsigned char worldMap[MAP_W * MAP_H];
Entity ents[MAX_ENTS];
PopText pops[MAX_POPS];
Player player;
UIState ui;

static int running = 1;
static unsigned int rng_seed = 40404;

static float rnd(void) {
    rng_seed = (rng_seed * 16807) % 2147483647;
    return (float)(rng_seed - 1) / 2147483646.0f;
}

static void set_msg(const char *s) {
    snprintf(ui.msg, sizeof(ui.msg), "%s", s);
    ui.msgTimer = 150;
}

static void spawn_pop(int sx, int sy, const char *txt, unsigned int col) {
    for (int i = 0; i < MAX_POPS; i++) {
        if (pops[i].timer <= 0) {
            pops[i].scrX = sx < 20 ? 20 : (sx > 400 ? 400 : sx);
            pops[i].scrY = sy < 40 ? 40 : (sy > 210 ? 210 : sy);
            snprintf(pops[i].text, sizeof(pops[i].text), "%s", txt);
            pops[i].color = col;
            pops[i].timer = 36;
            return;
        }
    }
}

static void spawn_ent(int idx, float x, float y, int type, int hp) {
    if (idx < 0 || idx >= MAX_ENTS) return;
    ents[idx].x = x;
    ents[idx].y = y;
    ents[idx].type = type;
    ents[idx].hp = hp;
    ents[idx].maxHp = hp;
    ents[idx].alive = 1;
    ents[idx].stunTimer = 0;
    ents[idx].hitFlash = 0;
    ents[idx].atkCooldown = 0;
}

static int find_free_ent(void) {
    for (int i = 0; i < MAX_ENTS; i++) {
        if (!ents[i].alive) return i;
    }
    return -1;
}

void load_stage(int stg) {
    if (stg < 1) stg = 1;
    if (stg > 4) stg = 1;
    player.stage = stg;
    player.x = 2.5f;
    player.y = 2.5f;
    player.ang = 0.0f;
    player.aimX = 0;
    player.aimY = 0;

    int wallBase = (stg == 1) ? W_WOOD_WALL : ((stg == 2) ? W_STONE_WALL : ((stg == 3) ? W_CHURCH_WALL : W_BLOOD_WALL));
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            int i = y * MAP_W + x;
            if (x == 0 || y == 0 || x == MAP_W - 1 || y == MAP_H - 1) {
                worldMap[i] = wallBase;
            } else {
                worldMap[i] = W_EMPTY;
            }
        }
    }

    /* Village / Castle buildings & courtyard pillars (32x32) */
    for (int y = 4; y <= 9; y++) {
        for (int x = 6; x <= 11; x++) {
            if (x == 6 || x == 11 || y == 4 || y == 9) {
                if (!(x == 8 && y == 9)) worldMap[y * MAP_W + x] = wallBase;
            }
        }
    }
    for (int y = 18; y <= 24; y++) {
        for (int x = 6; x <= 12; x++) {
            if (x == 6 || x == 12 || y == 18 || y == 24) {
                if (!(x == 9 && y == 18)) worldMap[y * MAP_W + x] = wallBase;
            }
        }
    }
    for (int y = 8; y <= 14; y++) {
        for (int x = 18; x <= 24; x++) {
            if (x == 18 || x == 24 || y == 8 || y == 14) {
                if (!(x == 18 && y == 11) && !(x == 24 && y == 11)) worldMap[y * MAP_W + x] = (stg >= 2) ? W_CHURCH_WALL : wallBase;
            }
        }
    }
    for (int y = 5; y <= 26; y += 4) {
        worldMap[y * MAP_W + 15] = (stg >= 2) ? W_CHURCH_WALL : W_STONE_WALL;
    }
    /* Exit Gate on far east wall */
    worldMap[16 * MAP_W + (MAP_W - 1)] = W_GATE_DOOR;
    worldMap[15 * MAP_W + (MAP_W - 2)] = W_EMPTY;
    worldMap[16 * MAP_W + (MAP_W - 2)] = W_EMPTY;

    memset(ents, 0, sizeof(ents));
    int eIdx = 0;
    /* El Buhonero (Merchant) in starting safe corner */
    spawn_ent(eIdx++, 2.5f, 5.5f, ENT_MERCHANT, 999);

    /* Supply crates & herbs */
    spawn_ent(eIdx++, 4.5f, 2.5f, ENT_CRATE, 1);
    spawn_ent(eIdx++, 8.5f, 6.5f, ENT_CRATE, 1);
    spawn_ent(eIdx++, 9.5f, 21.5f, ENT_CRATE, 1);
    spawn_ent(eIdx++, 21.5f, 11.5f, ENT_CRATE, 1);
    spawn_ent(eIdx++, 26.5f, 6.5f, ENT_CRATE, 1);
    spawn_ent(eIdx++, 13.5f, 3.5f, ENT_HERB_G, 1);
    spawn_ent(eIdx++, 25.5f, 26.5f, ENT_HERB_R, 1);
    /* Explosive Red Barrels */
    spawn_ent(eIdx++, 13.5f, 12.5f, ENT_BARREL, 1);
    spawn_ent(eIdx++, 21.5f, 17.5f, ENT_BARREL, 1);
    spawn_ent(eIdx++, 27.5f, 15.5f, ENT_BARREL, 1);

    /* Infected Villagers (Ganados) */
    int vCount = 10 + stg * 3;
    for (int i = 0; i < vCount && eIdx < MAX_ENTS - 2; i++) {
        float vx = 10.5f + rnd() * 18.0f;
        float vy = 3.5f + rnd() * 24.0f;
        if (worldMap[(int)vy * MAP_W + (int)vx] == W_EMPTY) {
            spawn_ent(eIdx++, vx, vy, ENT_VILLAGER, 14 + stg * 5);
        }
    }

    /* Dr. Salvador (Chainsaw Mini-Boss) / Lord Saddler guarding Exit Gate */
    spawn_ent(eIdx++, 28.5f, 16.5f, (stg == 4) ? ENT_BOSS : ENT_CHAINSAW, 60 + stg * 25);
    if (stg >= 3 && eIdx < MAX_ENTS) {
        spawn_ent(eIdx++, 24.5f, 20.5f, ENT_CHAINSAW, 70);
    }

    static const char *sMsg[5] = {"", "Zona 1: El Pueblo Maldito", "Zona 2: Cementerio e Iglesia", "Zona 3: Castillo del Culto", "Zona 4: Laboratorio Final"};
    set_msg(sMsg[stg]);
}

void save_game(void) {
    SceUID fd = sceIoOpen("bv4_save.dat", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, &player, sizeof(player));
        sceIoWrite(fd, worldMap, sizeof(worldMap));
        sceIoWrite(fd, ents, sizeof(ents));
        sceIoClose(fd);
        audio_play_sfx(SFX_PICKUP);
        set_msg("Partida guardada en Memory Stick!");
    }
}

int load_game(void) {
    SceUID fd = sceIoOpen("bv4_save.dat", PSP_O_RDONLY, 0777);
    if (fd >= 0) {
        sceIoRead(fd, &player, sizeof(player));
        sceIoRead(fd, worldMap, sizeof(worldMap));
        sceIoRead(fd, ents, sizeof(ents));
        sceIoClose(fd);
        set_msg("Partida cargada!");
        return 1;
    }
    return 0;
}

static void init_new_game(void) {
    memset(&player, 0, sizeof(player));
    player.hp = 100;
    player.maxHp = 100;
    player.pesetas = 600;
    player.weapon = WPN_PISTOL;
    player.mag[WPN_PISTOL] = 10;
    player.maxMag[WPN_PISTOL] = 10;
    player.reserve[WPN_PISTOL] = 30;
    player.mag[WPN_SHOTGUN] = 6;
    player.maxMag[WPN_SHOTGUN] = 6;
    player.reserve[WPN_SHOTGUN] = 12;
    player.mag[WPN_RIFLE] = 5;
    player.maxMag[WPN_RIFLE] = 5;
    player.reserve[WPN_RIFLE] = 10;
    player.herbsG = 1;
    load_stage(1);
}

static void drop_loot(float x, float y, int guaranteedBig) {
    int slot = find_free_ent();
    if (slot < 0) return;
    if (guaranteedBig) {
        player.pesetas += 1000;
        spawn_ent(slot, x, y, ENT_HERB_G, 1);
        set_msg("Jefe abatido! +1000 Pesetas!");
        return;
    }
    float r = rnd();
    if (r < 0.40f)      spawn_ent(slot, x, y, ENT_AMMO, 1);
    else if (r < 0.72f) spawn_ent(slot, x, y, ENT_PESETAS, 1);
    else if (r < 0.90f) spawn_ent(slot, x, y, ENT_HERB_G, 1);
    else                spawn_ent(slot, x, y, ENT_HERB_R, 1);
}

static int merchant_near(void) {
    for (int i = 0; i < MAX_ENTS; i++) {
        if (ents[i].alive && ents[i].type == ENT_MERCHANT) {
            float dx = ents[i].x - player.x;
            float dy = ents[i].y - player.y;
            if (dx * dx + dy * dy < 3.4f * 3.4f) return 1;
        }
    }
    return 0;
}

static int boss_alive(void) {
    for (int i = 0; i < MAX_ENTS; i++) {
        if (ents[i].alive && (ents[i].type == ENT_CHAINSAW || ents[i].type == ENT_BOSS)) return 1;
    }
    return 0;
}

static void on_enemy_killed(Entity *e) {
    e->alive = 0;
    player.kills++;
    player.pesetas += (e->type == ENT_VILLAGER) ? 200 : 1000;
    drop_loot(e->x, e->y, e->type == ENT_CHAINSAW || e->type == ENT_BOSS);
}

static int try_roundhouse_kick(void) {
    int kicked = 0;
    for (int i = 0; i < MAX_ENTS; i++) {
        if (!ents[i].alive || (ents[i].type != ENT_VILLAGER && ents[i].type != ENT_CHAINSAW && ents[i].type != ENT_BOSS)) continue;
        float dx = ents[i].x - player.x;
        float dy = ents[i].y - player.y;
        float d = sqrtf(dx * dx + dy * dy);
        if (ents[i].stunTimer > 0 && d < 2.2f) {
            kicked = 1;
            break;
        }
    }
    if (!kicked) return 0;

    /* Execute RE4 Roundhouse Kick! Hits all nearby enemies + knockback */
    player.kickTimer = 14;
    ui.shakeTimer = 8;
    audio_play_sfx(SFX_KICK);
    set_msg("PATADA GIRATORIA!");

    for (int i = 0; i < MAX_ENTS; i++) {
        if (!ents[i].alive || (ents[i].type != ENT_VILLAGER && ents[i].type != ENT_CHAINSAW && ents[i].type != ENT_BOSS)) continue;
        float dx = ents[i].x - player.x;
        float dy = ents[i].y - player.y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d < 2.6f && d > 0.05f) {
            ents[i].hp -= 22;
            ents[i].stunTimer = 0;
            ents[i].hitFlash = 8;
            float nx = ents[i].x + (dx / d) * 1.4f;
            float ny = ents[i].y + (dy / d) * 1.4f;
            if (nx > 1.2f && ny > 1.2f && nx < MAP_W - 1.2f && ny < MAP_H - 1.2f &&
                worldMap[(int)ny * MAP_W + (int)nx] == W_EMPTY) {
                ents[i].x = nx;
                ents[i].y = ny;
            }
            if (ents[i].hp <= 0) {
                ents[i].alive = 0;
                player.kills++;
                player.pesetas += 250;
                drop_loot(ents[i].x, ents[i].y, ents[i].type == ENT_CHAINSAW || ents[i].type == ENT_BOSS);
            }
        }
    }
    return 1;
}

static void fire_weapon(void) {
    if (player.fireTimer > 0 || player.reloadTimer > 0 || player.kickTimer > 0) return;
    if (try_roundhouse_kick()) return;

    int w = player.weapon;
    if (w != WPN_KNIFE) {
        if (player.mag[w] <= 0) {
            if (player.reserve[w] > 0) {
                player.reloadTimer = 35;
                audio_play_sfx(SFX_RELOAD);
                set_msg("Recargando...");
            } else {
                audio_play_sfx(SFX_UI);
                set_msg("Sin municion! Usa Cuchillo [Cuadrado]");
            }
            return;
        }
        player.mag[w]--;
    }

    static const int baseDmg[4] = {6, 10, 26, 42};
    static const int cooldowns[4] = {14, 15, 32, 38};
    static const int sfxIds[4] = {SFX_KNIFE, SFX_PISTOL, SFX_SHOTGUN, SFX_RIFLE};
    int dmg = baseDmg[w] + player.powLv[w] * 5;
    float maxRange = (w == WPN_KNIFE) ? 2.1f : ((w == WPN_SHOTGUN) ? 8.5f : 22.0f);
    int hitSpread = (w == WPN_SHOTGUN) ? 38 : 16;

    player.fireTimer = cooldowns[w];
    ui.shakeTimer = (w == WPN_SHOTGUN || w == WPN_RIFLE) ? 6 : 2;
    audio_play_sfx(sfxIds[w]);

    /* Project 3D entities and find closest hit under Red Laser Sight (aimScrX, aimScrY) */
    float dirX = cosf(player.ang);
    float dirY = sinf(player.ang);
    float fovScale = player.aiming ? ((player.weapon == WPN_RIFLE) ? 0.28f : 0.48f) : 0.66f;
    float planeX = -dirY * fovScale;
    float planeY =  dirX * fovScale;
    float invDet = 1.0f / (planeX * dirY - dirX * planeY);

    int aimScrX = SCR_W / 2 + (player.aiming ? player.aimX : 0);
    int aimScrY = 132 + (player.aiming ? player.aimY : 0);

    int bestEnt = -1;
    float bestDist = maxRange;
    int hitZone = 0; /* 0=torso, 1=headshot, 2=knee */

    for (int i = 0; i < MAX_ENTS; i++) {
        Entity *e = &ents[i];
        if (!e->alive) continue;
        if (e->type != ENT_VILLAGER && e->type != ENT_CHAINSAW && e->type != ENT_BOSS &&
            e->type != ENT_CRATE && e->type != ENT_BARREL) continue;

        float sx = e->x - player.x;
        float sy = e->y - player.y;
        float transX = invDet * (dirY * sx - dirX * sy);
        float transY = invDet * (-planeY * sx + planeX * sy);
        if (transY <= 0.25f || transY >= bestDist) continue;

        int sprScreenX = (int)((SCR_W / 2) * (1.0f + transX / transY));
        int fullH = (int)fabsf(SCR_H / transY);
        int isSmall = (e->type == ENT_CRATE || e->type == ENT_BARREL);
        int sprH = isSmall ? (fullH * 3) / 5 : fullH;
        int sprW = isSmall ? (fullH * 3) / 5 : (fullH * 3) / 4;
        int crouchOff = (e->stunTimer > 0) ? (sprH / 5) : 0;
        int topY = 132 + fullH / 2 - sprH + crouchOff;
        int botY = 132 + fullH / 2;

        if (abs(aimScrX - sprScreenX) <= sprW / 2 + hitSpread &&
            aimScrY >= topY - 8 && aimScrY <= botY + 8) {
            bestDist = transY;
            bestEnt = i;
            float relY = (float)(aimScrY - topY) / (float)(sprH > 0 ? sprH : 1);
            if (relY < 0.28f) hitZone = 1;      /* HEADSHOT! */
            else if (relY > 0.70f) hitZone = 2; /* KNEE SHOT! */
            else hitZone = 0;
        }
    }

    if (w == WPN_SHOTGUN) {
        /* Shotgun spread: pellets also hit nearby enemies around the aim point */
        for (int i = 0; i < MAX_ENTS; i++) {
            Entity *o = &ents[i];
            if (!o->alive || i == bestEnt) continue;
            if (o->type != ENT_VILLAGER && o->type != ENT_CHAINSAW && o->type != ENT_BOSS) continue;
            float osx = o->x - player.x;
            float osy = o->y - player.y;
            float otX = invDet * (dirY * osx - dirX * osy);
            float otY = invDet * (-planeY * osx + planeX * osy);
            if (otY <= 0.25f || otY > 6.0f) continue;
            int oScrX = (int)((SCR_W / 2) * (1.0f + otX / otY));
            if (abs(aimScrX - oScrX) < 90) {
                o->hp -= dmg / 2;
                o->hitFlash = 6;
                o->stunTimer = 40;
                if (o->hp <= 0) on_enemy_killed(o);
            }
        }
    }

    if (bestEnt >= 0) {
        Entity *e = &ents[bestEnt];
        e->hitFlash = 6;
        ui.hitSparkTimer = 7;
        ui.hitMarkerTimer = 10;
        ui.hitSparkX = aimScrX;
        ui.hitSparkY = aimScrY;
        if (e->type == ENT_BARREL) {
            e->alive = 0;
            ui.explosionTimer = 15;
            ui.shakeTimer = 12;
            audio_play_sfx(SFX_SHOTGUN);
            spawn_pop(aimScrX, aimScrY, "BOOM!", 0xFF40C0FF);
            set_msg("BOOM! Barril explosivo!");
            for (int j = 0; j < MAX_ENTS; j++) {
                Entity *v = &ents[j];
                if (!v->alive || (v->type != ENT_VILLAGER && v->type != ENT_CHAINSAW && v->type != ENT_BOSS)) continue;
                float bdx = v->x - e->x, bdy = v->y - e->y;
                if (bdx * bdx + bdy * bdy <= 4.0f * 4.0f) {
                    v->hp -= 65;
                    v->hitFlash = 10;
                    v->stunTimer = 140;
                    if (v->hp <= 0) on_enemy_killed(v);
                }
            }
            return;
        }
        if (e->type == ENT_CRATE) {
            e->alive = 0;
            audio_play_sfx(SFX_CRATE);
            drop_loot(e->x, e->y, 0);
            spawn_pop(aimScrX, aimScrY, "CAJA!", 0xFF70D8FF);
            set_msg("Caja destruida!");
            return;
        }
        int finalDmg = dmg;
        if (hitZone == 1) {
            finalDmg = (dmg * 5) / 2; /* 2.5x Critical Headshot! */
            e->stunTimer = 110;
            audio_play_sfx(SFX_HEADSHOT);
            spawn_pop(aimScrX - 18, aimScrY - 12, "HEADSHOT!", 0xFF5555FF);
            set_msg("HEADSHOT CRITICO!");
        } else if (hitZone == 2) {
            e->stunTimer = 160; /* Kneeling stun ready for Roundhouse Kick! */
            spawn_pop(aimScrX - 14, aimScrY, "RODILLA!", 0xFF70E0FF);
            set_msg("A la rodilla! Acercate y pulsa [X]!");
        } else {
            char db[16];
            snprintf(db, sizeof(db), "-%d", finalDmg);
            spawn_pop(aimScrX, aimScrY, db, 0xFFFFFFFF);
        }
        e->hp -= finalDmg;
        if (e->hp <= 0) {
            e->alive = 0;
            player.kills++;
            player.pesetas += (e->type == ENT_VILLAGER) ? 200 : 1000;
            drop_loot(e->x, e->y, e->type == ENT_CHAINSAW || e->type == ENT_BOSS);
        }
    }
}

static void reload_or_switch(void) {
    int w = player.weapon;
    if (player.aiming && w != WPN_KNIFE && player.mag[w] < player.maxMag[w] && player.reserve[w] > 0) {
        player.reloadTimer = 35;
        audio_play_sfx(SFX_RELOAD);
        set_msg("Recargando...");
        return;
    }
    /* Cycle available weapons */
    for (int step = 1; step <= 4; step++) {
        int nw = (w + step) % 4;
        if (nw == WPN_SHOTGUN && !player.hasShotgun) continue;
        if (nw == WPN_RIFLE && !player.hasRifle) continue;
        player.weapon = nw;
        audio_play_sfx(SFX_UI);
        break;
    }
}

static void buy_shop_item(int sel) {
    if (sel == 0) {
        if (!player.hasShotgun && player.pesetas >= 1500) {
            player.pesetas -= 1500; player.hasShotgun = 1; player.weapon = WPN_SHOTGUN;
            audio_play_sfx(SFX_MERCHANT); set_msg("Buhonero: Buena eleccion! (Escopeta)");
        } else set_msg(player.hasShotgun ? "Ya tienes la Escopeta" : "Faltan Pesetas (1500)");
    } else if (sel == 1) {
        if (!player.hasRifle && player.pesetas >= 2500) {
            player.pesetas -= 2500; player.hasRifle = 1; player.weapon = WPN_RIFLE;
            audio_play_sfx(SFX_MERCHANT); set_msg("Buhonero: Precision letal! (Rifle)");
        } else set_msg(player.hasRifle ? "Ya tienes el Rifle" : "Faltan Pesetas (2500)");
    } else if (sel == 2) {
        if (player.pesetas >= 800 && player.powLv[player.weapon] < 10) {
            player.pesetas -= 800; player.powLv[player.weapon]++;
            audio_play_sfx(SFX_MERCHANT); set_msg("Potencia de arma mejorada!");
        } else set_msg("Faltan Pesetas (800)");
    } else if (sel == 3) {
        if (player.pesetas >= 400) {
            player.pesetas -= 400;
            player.reserve[WPN_PISTOL] += 15;
            player.reserve[WPN_SHOTGUN] += 6;
            player.reserve[WPN_RIFLE] += 4;
            audio_play_sfx(SFX_PICKUP); set_msg("Municion recargada!");
        } else set_msg("Faltan Pesetas (400)");
    } else if (sel == 4) {
        if (player.pesetas >= 500) {
            player.pesetas -= 500; player.sprays++;
            audio_play_sfx(SFX_PICKUP); set_msg("Comprado Spray Primeros Auxilios!");
        } else set_msg("Faltan Pesetas (500)");
    } else if (sel == 5) {
        if (player.pesetas >= 1200 && player.armorLv < 5) {
            player.pesetas -= 1200; player.armorLv++;
            audio_play_sfx(SFX_MERCHANT); set_msg("Chaleco Tactico mejorado!");
        } else set_msg("Faltan Pesetas (1200)");
    }
}

static void use_inv_item(int sel) {
    if (sel == 0) {
        if (player.herbsG > 0) {
            player.herbsG--;
            player.hp += 45; if (player.hp > player.maxHp) player.hp = player.maxHp;
            audio_play_sfx(SFX_PICKUP); set_msg("Usaste Hierba Verde (+45 HP)");
        } else set_msg("No tienes Hierbas Verdes");
    } else if (sel == 1) {
        if (player.herbsG > 0 && player.herbsR > 0) {
            player.herbsG--; player.herbsR--;
            player.maxHp += 15; player.hp = player.maxHp;
            audio_play_sfx(SFX_MERCHANT); set_msg("Mezcla G+R! Salud 100% y +15 HP Max!");
        } else set_msg("Necesitas 1 Hierba Verde y 1 Roja");
    } else if (sel == 2) {
        if (player.sprays > 0) {
            player.sprays--; player.hp = player.maxHp;
            audio_play_sfx(SFX_PICKUP); set_msg("Spray usado! Salud al 100%");
        } else set_msg("No tienes Sprays");
    } else if (sel == 3) {
        reload_or_switch();
    } else if (sel == 4) {
        save_game();
    }
}

static int exit_cb(int arg1, int arg2, void *common) { running = 0; return 0; }
static int cb_thread(SceSize args, void *argp) {
    int cbid = sceKernelCreateCallback("Exit Callback", exit_cb, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

int main(void) {
    int thid = sceKernelCreateThread("update_thread", cb_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);

    render_init();
    audio_init();

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    ui.sfxOn = 1;
    ui.inTitle = 1;
    ui.titleSel = 0;
    init_new_game();

    SceCtrlData pad, oldPad;
    sceCtrlReadBufferPositive(&oldPad, 1);
    int frameCnt = 0;

    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~oldPad.Buttons;

        if (ui.inTitle) {
            if (ui.showHelp) {
                if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE)) { ui.showHelp = 0; audio_play_sfx(SFX_UI); }
            } else {
                if (pressed & PSP_CTRL_UP) { ui.titleSel = (ui.titleSel + 2) % 3; audio_play_sfx(SFX_UI); }
                if (pressed & PSP_CTRL_DOWN) { ui.titleSel = (ui.titleSel + 1) % 3; audio_play_sfx(SFX_UI); }
                if (pressed & PSP_CTRL_CROSS) {
                    if (ui.titleSel == 0) {
                        if (!load_game()) init_new_game();
                        ui.inTitle = 0; audio_play_sfx(SFX_MERCHANT);
                    } else if (ui.titleSel == 1) {
                        init_new_game();
                        ui.inTitle = 0; audio_play_sfx(SFX_MERCHANT);
                    } else {
                        ui.showHelp = 1; audio_play_sfx(SFX_UI);
                    }
                }
            }
            oldPad = pad;
            render_frame();
            continue;
        }

        if (ui.gameWon) {
            if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_START)) {
                ui.gameWon = 0;
                ui.inTitle = 1;
                init_new_game();
                audio_play_sfx(SFX_UI);
            }
            oldPad = pad;
            render_frame();
            continue;
        }

        if (player.deadTimer > 0) {
            player.deadTimer--;
            if (player.deadTimer == 0) {
                int stg = player.stage;
                player.hp = player.maxHp;
                player.pesetas = player.pesetas / 2;
                load_stage(stg);
                set_msg("Reaparecido. Perdiste la mitad de Pesetas");
            }
            oldPad = pad;
            render_frame();
            continue;
        }

        if (pressed & PSP_CTRL_SELECT) {
            ui.sfxOn = !ui.sfxOn;
            set_msg(ui.sfxOn ? "Audio: ON" : "Audio: OFF");
        }
        if (pressed & PSP_CTRL_START) save_game();

        if (pressed & PSP_CTRL_TRIANGLE) {
            ui.invOpen = 0;
            if (!ui.shopOpen && !merchant_near()) {
                set_msg("Acercate al Buhonero (zona segura)");
                audio_play_sfx(SFX_UI);
            } else {
                ui.shopOpen = !ui.shopOpen;
                audio_play_sfx(ui.shopOpen ? SFX_MERCHANT : SFX_UI);
            }
        }
        if (pressed & PSP_CTRL_CIRCLE) {
            ui.shopOpen = 0;
            ui.invOpen = !ui.invOpen;
            audio_play_sfx(SFX_UI);
        }

        if (ui.shopOpen) {
            if (pressed & PSP_CTRL_UP) { ui.shopSel = (ui.shopSel + 5) % 6; audio_play_sfx(SFX_UI); }
            if (pressed & PSP_CTRL_DOWN) { ui.shopSel = (ui.shopSel + 1) % 6; audio_play_sfx(SFX_UI); }
            if (pressed & PSP_CTRL_CROSS) buy_shop_item(ui.shopSel);
        } else if (ui.invOpen) {
            if (pressed & PSP_CTRL_UP) { ui.invSel = (ui.invSel + 4) % 5; audio_play_sfx(SFX_UI); }
            if (pressed & PSP_CTRL_DOWN) { ui.invSel = (ui.invSel + 1) % 5; audio_play_sfx(SFX_UI); }
            if (pressed & PSP_CTRL_CROSS) use_inv_item(ui.invSel);
        } else {
            player.aiming = (pad.Buttons & PSP_CTRL_LTRIGGER) ? 1 : 0;

            if (pressed & PSP_CTRL_SQUARE) {
                if (!player.aiming && ((pad.Buttons & PSP_CTRL_DOWN) || pad.Ly > 195)) {
                    /* RE4 Quick 180-degree Turn! */
                    player.ang += 3.14159f;
                    audio_play_sfx(SFX_UI);
                    set_msg("Giro rapido 180!");
                } else {
                    reload_or_switch();
                }
            }
            if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_RTRIGGER)) fire_weapon();

            float ax = ((int)pad.Lx - 128) / 128.0f;
            float ay = ((int)pad.Ly - 128) / 128.0f;
            if (fabsf(ax) < 0.22f) ax = 0.0f;
            if (fabsf(ay) < 0.22f) ay = 0.0f;
            if (pad.Buttons & PSP_CTRL_LEFT)  ax = -0.85f;
            if (pad.Buttons & PSP_CTRL_RIGHT) ax =  0.85f;
            if (pad.Buttons & PSP_CTRL_UP)    ay = -0.85f;
            if (pad.Buttons & PSP_CTRL_DOWN)  ay =  0.85f;

            if (player.aiming) {
                player.isMoving = 0;
                /* Move Red Laser Sight dot across enemy head/knees! */
                player.aimX += (int)(ax * 4.5f);
                player.aimY += (int)(ay * 3.5f);
                if (player.aimX < -110) { player.aimX = -110; player.ang -= 0.025f; }
                if (player.aimX >  110) { player.aimX =  110; player.ang += 0.025f; }
                if (player.aimY <  -75) player.aimY = -75;
                if (player.aimY >   75) player.aimY =  75;
            } else {
                player.aimX = (player.aimX * 3) / 4;
                player.aimY = (player.aimY * 3) / 4;
                player.ang += ax * 0.055f;
                float moveSpd = -ay * 0.068f;
                player.isMoving = (fabsf(moveSpd) > 0.001f) ? 1 : 0;
                if (player.isMoving) {
                    float nx = player.x + cosf(player.ang) * moveSpd;
                    float ny = player.y + sinf(player.ang) * moveSpd;
                    int wtX = worldMap[(int)player.y * MAP_W + (int)(nx + (moveSpd > 0 ? 0.25f : -0.25f))];
                    int wtY = worldMap[(int)(ny + (moveSpd > 0 ? 0.25f : -0.25f)) * MAP_W + (int)player.x];
                    if (wtX == W_GATE_DOOR || wtY == W_GATE_DOOR) {
                        if (boss_alive()) {
                            set_msg("Puerta sellada: derrota al Jefe!");
                        } else if (player.stage >= 4) {
                            ui.gameWon = 1;
                            audio_play_sfx(SFX_MERCHANT);
                        } else {
                            load_stage(player.stage + 1);
                            audio_play_sfx(SFX_MERCHANT);
                        }
                    } else {
                        if (wtX == W_EMPTY) player.x = nx;
                        if (wtY == W_EMPTY) player.y = ny;
                    }
                }
            }
        }

        /* Timers & Reload completion */
        if (player.fireTimer > 0) player.fireTimer--;
        if (player.kickTimer > 0) player.kickTimer--;
        if (player.damageFlash > 0) player.damageFlash--;
        if (player.iframes > 0) player.iframes--;
        if (ui.shakeTimer > 0) ui.shakeTimer--;
        if (ui.explosionTimer > 0) ui.explosionTimer--;
        if (ui.hitSparkTimer > 0) ui.hitSparkTimer--;
        if (ui.hitMarkerTimer > 0) ui.hitMarkerTimer--;
        if (ui.msgTimer > 0) ui.msgTimer--;
        for (int pi = 0; pi < MAX_POPS; pi++) if (pops[pi].timer > 0) pops[pi].timer--;

        if (player.reloadTimer > 0) {
            player.reloadTimer--;
            if (player.reloadTimer == 0) {
                int w = player.weapon;
                int need = player.maxMag[w] - player.mag[w];
                if (need > player.reserve[w]) need = player.reserve[w];
                player.mag[w] += need;
                player.reserve[w] -= need;
                audio_play_sfx(SFX_RELOAD);
                set_msg("Arma recargada!");
            }
        }

        /* Entity Updates: Pickups & Aggressive Infected Villager / Chainsaw AI */
        frameCnt++;
        for (int i = 0; i < MAX_ENTS; i++) {
            Entity *e = &ents[i];
            if (!e->alive) continue;
            if (e->hitFlash > 0) e->hitFlash--;
            if (e->stunTimer > 0) { e->stunTimer--; continue; }

            float dx = player.x - e->x;
            float dy = player.y - e->y;
            float dist = sqrtf(dx * dx + dy * dy);

            /* Pickups */
            if (e->type >= ENT_HERB_G && e->type <= ENT_PESETAS) {
                if (dist < 0.85f) {
                    e->alive = 0;
                    audio_play_sfx(SFX_PICKUP);
                    if (e->type == ENT_HERB_G) { player.herbsG++; set_msg("Recogiste Hierba Verde!"); }
                    else if (e->type == ENT_HERB_R) { player.herbsR++; set_msg("Recogiste Hierba Roja!"); }
                    else if (e->type == ENT_AMMO) {
                        player.reserve[WPN_PISTOL] += 10;
                        player.reserve[WPN_SHOTGUN] += 4;
                        set_msg("Recogiste Municion!");
                    } else {
                        player.pesetas += 350;
                        set_msg("Recogiste +350 Pesetas!");
                    }
                }
                continue;
            }

            if (e->type != ENT_VILLAGER && e->type != ENT_CHAINSAW && e->type != ENT_BOSS) continue;

            if (e->type == ENT_CHAINSAW && dist < 8.0f && (frameCnt % 90 == 0)) {
                audio_play_sfx(SFX_CHAINSAW);
            }

            if (dist < 11.0f && dist > 0.85f && !ui.shopOpen && !ui.invOpen) {
                float spd = (e->type == ENT_VILLAGER) ? 0.018f : 0.023f;
                float nx = e->x + (dx / dist) * spd;
                float ny = e->y + (dy / dist) * spd;
                if (worldMap[(int)e->y * MAP_W + (int)nx] == W_EMPTY) e->x = nx;
                if (worldMap[(int)ny * MAP_W + (int)e->x] == W_EMPTY) e->y = ny;
            } else if (dist <= 0.90f && player.iframes <= 0 && !ui.shopOpen && !ui.invOpen) {
                int raw = (e->type == ENT_VILLAGER) ? 14 : 32;
                int dmg = raw - player.armorLv * 3;
                if (dmg < 4) dmg = 4;
                player.hp -= dmg;
                player.iframes = 40;
                player.damageFlash = 12;
                ui.shakeTimer = 7;
                audio_play_sfx(SFX_HURT);
                set_msg(e->type == ENT_VILLAGER ? "Un Aldeano te ataco!" : "Motosierra! Cuidado!");
                if (player.hp <= 0) {
                    player.hp = 0;
                    player.deadTimer = 160;
                    audio_play_sfx(SFX_HURT);
                    set_msg("HAS MUERTO...");
                }
            }
        }

        oldPad = pad;
        render_frame();
    }

    audio_shutdown();
    sceKernelExitGame();
    return 0;
}
