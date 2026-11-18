#include "game.h"
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

PSP_MODULE_INFO("PixelariaPocket", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

unsigned char world[WORLD_SIZE * WORLD_SIZE];
unsigned char biomes[WORLD_SIZE * WORLD_SIZE];
unsigned char dungeon[DUNG_W * DUNG_H];
Mob mobs[MAX_MOBS];
Mob dungMobs[MAX_MOBS];
Projectile proj;
Popup pops[MAX_POPS];
Player player;
Skills skills;
Stats stats;
GameTime gameTime;
UIState ui;

static int running = 1;
static unsigned int rng_seed = 1337;

static float rnd(void) {
    rng_seed = (rng_seed * 16807) % 2147483647;
    return (float)(rng_seed - 1) / 2147483646.0f;
}

static int clampi(int v, int mn, int mx) { return v < mn ? mn : (v > mx ? mx : v); }
static float clampf(float v, float mn, float mx) { return v < mn ? mn : (v > mx ? mx : v); }

int nextXp(int lv) { return lv * lv * 25; }
int maxHp(void) { return 100 + (skills.combate.lv - 1) * 5 + player.armorPieces * 10; }
int totalLevel(void) {
    return skills.tala.lv + skills.mineria.lv + skills.combate.lv +
           skills.supervivencia.lv + skills.construccion.lv;
}

static void set_msg(const char *s) {
    snprintf(ui.msg, sizeof(ui.msg), "%s", s);
}

static void spawn_pop(int wx, int wy, const char *txt, unsigned int col) {
    for (int i = 0; i < MAX_POPS; i++) {
        if (pops[i].timer <= 0) {
            pops[i].wx = wx;
            pops[i].wy = wy;
            snprintf(pops[i].text, sizeof(pops[i].text), "%s", txt);
            pops[i].color = col;
            pops[i].timer = 30;
            return;
        }
    }
}

static void add_xp(Skill *sk, const char *name, int amt, int is_combat) {
    if (amt <= 0) return;
    sk->xp += amt;
    while (sk->xp >= nextXp(sk->lv) && sk->lv < 99) {
        sk->xp -= nextXp(sk->lv);
        sk->lv++;
        if (is_combat) player.hp = clampf(player.hp + 5.0f, 0.0f, (float)maxHp());
        audio_play_sfx(SFX_LEVELUP);
        ui.shakeTimer = 6;
        char b[24];
        snprintf(b, sizeof(b), "%s Nv.%d!", name, sk->lv);
        spawn_pop(player.x, player.y, b, 0xFF7AE2FF);
        set_msg(b);
    }
}

static int is_walkable(int t) {
    return t == T_GRASS || t == T_FOREST || t == T_SAND || t == T_SNOW ||
           t == T_WATER || t == T_FIRE || t == T_DFLOOR || t == T_DUNGEON_DOOR ||
           t == T_EXIT || t == T_STAIRS_DOWN;
}

static int mob_at(int x, int y) {
    Mob *arr = player.inDungeon ? dungMobs : mobs;
    for (int i = 0; i < MAX_MOBS; i++) {
        if (arr[i].alive && arr[i].x == x && arr[i].y == y) return i;
    }
    return -1;
}

static void check_near_fire(void) {
    player.warm = 0;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int cx = player.x + dx, cy = player.y + dy;
            if (cx >= 0 && cy >= 0 && cx < WORLD_SIZE && cy < WORLD_SIZE) {
                if (world[cy * WORLD_SIZE + cx] == T_FIRE) {
                    player.warm = 1;
                    if (player.wet > 0) {
                        player.wet = 0;
                        set_msg("Ropa seca!");
                    }
                    return;
                }
            }
        }
    }
}

static void generate_world(void) {
    for (int y = 0; y < WORLD_SIZE; y++) {
        for (int x = 0; x < WORLD_SIZE; x++) {
            int i = y * WORLD_SIZE + x;
            int coast = 3 + (int)(1.6f * sinf(x * 0.22f) + 1.6f * cosf(y * 0.22f));
            if (x < coast || y < coast || x >= WORLD_SIZE - coast || y >= WORLD_SIZE - coast) {
                world[i] = T_WATER;
                biomes[i] = B_MEADOW;
                continue;
            }
            /* Southern Bay */
            if (y > 54 && x > 18 && x < 42 && (y + (int)(2.0f * sinf(x * 0.35f)) > 55)) {
                world[i] = T_WATER;
                biomes[i] = B_MEADOW;
                continue;
            }

            int nx = x + (int)((rnd() - 0.5f) * 6.0f);
            int ny = y + (int)((rnd() - 0.5f) * 6.0f);
            int b = (nx < 32 && ny < 32) ? B_FOREST : ((nx >= 32 && ny < 32) ? B_TUNDRA : ((nx < 32 && ny >= 32) ? B_MEADOW : B_DESERT));
            biomes[i] = (unsigned char)b;
            float r = rnd();
            if (b == B_MEADOW) {
                world[i] = r < 0.12f ? T_TREE : (r < 0.19f ? T_BERRY : (r < 0.23f ? T_ROCK : T_GRASS));
            } else if (b == B_FOREST) {
                world[i] = r < 0.28f ? T_TREE : (r < 0.36f ? T_PINE : (r < 0.41f ? T_BERRY : (r < 0.45f ? T_ROCK : T_FOREST)));
            } else if (b == B_DESERT) {
                world[i] = r < 0.08f ? T_CACTUS : (r < 0.13f ? T_ROCK : T_SAND);
            } else {
                world[i] = r < 0.16f ? T_PINE : (r < 0.23f ? T_IRON : (r < 0.28f ? T_ROCK : T_SNOW));
            }
        }
    }

    /* Winding River from northern mountains to southern bay */
    for (int y = 3; y < WORLD_SIZE - 3; y++) {
        int rx = 25 + (int)(3.0f * sinf(y * 0.18f));
        world[y * WORLD_SIZE + rx] = T_WATER;
        world[y * WORLD_SIZE + rx + 1] = T_WATER;
    }

    for (int y = 45; y <= 51; y++) {
        for (int x = 45; x <= 51; x++) {
            if ((x == 45 || x == 51 || y == 45 || y == 51) && !(x == 48 && y == 45)) {
                world[y * WORLD_SIZE + x] = T_RUIN;
            } else {
                world[y * WORLD_SIZE + x] = T_SAND;
            }
        }
    }
    world[48 * WORLD_SIZE + 48] = T_CHEST;
    world[48 * WORLD_SIZE + 47] = T_IRON;
    world[48 * WORLD_SIZE + 49] = T_IRON;

    for (int y = 30; y <= 34; y++) {
        for (int x = 30; x <= 34; x++) {
            world[y * WORLD_SIZE + x] = T_GRASS;
            biomes[y * WORLD_SIZE + x] = B_MEADOW;
        }
    }
    world[31 * WORLD_SIZE + 33] = T_TREE;
    world[32 * WORLD_SIZE + 30] = T_BERRY;
    world[33 * WORLD_SIZE + 34] = T_ROCK;
    world[34 * WORLD_SIZE + 31] = T_WATER;
    /* 4 Surface Crypts across biomes */
    world[32 * WORLD_SIZE + 36] = T_DUNGEON_DOOR;
    world[14 * WORLD_SIZE + 14] = T_DUNGEON_DOOR;
    world[14 * WORLD_SIZE + 48] = T_DUNGEON_DOOR;
    world[49 * WORLD_SIZE + 48] = T_DUNGEON_DOOR;

    memset(mobs, 0, sizeof(mobs));
    for (int i = 0; i < 10; i++) {
        int mx = 10 + (int)(rnd() * 44.0f);
        int my = 10 + (int)(rnd() * 44.0f);
        if ((mx < 28 || mx > 36 || my < 28 || my > 36) && is_walkable(world[my * WORLD_SIZE + mx])) {
            mobs[i].x = mx; mobs[i].y = my; mobs[i].hp = 2; mobs[i].alive = 1; mobs[i].type = MOB_SLIME;
        }
    }
}

static void generate_dungeon_floor(int floor) {
    for (int y = 0; y < DUNG_H; y++) {
        for (int x = 0; x < DUNG_W; x++) {
            int i = y * DUNG_W + x;
            if (x == 0 || y == 0 || x == DUNG_W - 1 || y == DUNG_H - 1) {
                dungeon[i] = T_DWALL;
            } else {
                dungeon[i] = T_DFLOOR;
            }
        }
    }
    /* Stone pillars & chambers (shift slightly per floor) */
    int p1 = 9 + (floor % 3);
    int p2 = 20 + ((floor + 1) % 3);
    for (int y = 3; y < DUNG_H - 3; y++) {
        if (y != 9 && y != 10) {
            dungeon[y * DUNG_W + p1] = T_DWALL;
            dungeon[y * DUNG_W + p2] = T_DWALL;
        }
    }
    dungeon[2 * DUNG_W + 2] = T_FIRE;
    dungeon[2 * DUNG_W + 29] = T_FIRE;
    dungeon[17 * DUNG_W + 29] = T_FIRE;
    dungeon[10 * DUNG_W + 2] = T_EXIT;
    if (floor < 10) {
        dungeon[10 * DUNG_W + 29] = T_STAIRS_DOWN;
    } else {
        /* Floor 10 Treasure Vault */
        dungeon[8 * DUNG_W + 29] = T_CHEST;
        dungeon[12 * DUNG_W + 29] = T_CHEST;
    }
    dungeon[5 * DUNG_W + 15] = T_CHEST;
    dungeon[14 * DUNG_W + 16] = T_CHEST;
    dungeon[10 * DUNG_W + 27] = T_CHEST;

    memset(dungMobs, 0, sizeof(dungMobs));
    int count = 4 + floor;
    if (count > MAX_MOBS - 1) count = MAX_MOBS - 1;
    for (int i = 0; i < count; i++) {
        int sx = 7 + (int)(rnd() * 21.0f);
        int sy = 2 + (int)(rnd() * 16.0f);
        if (dungeon[sy * DUNG_W + sx] == T_DFLOOR && (sx > 6 || sy < 7 || sy > 13)) {
            dungMobs[i].x = sx;
            dungMobs[i].y = sy;
            dungMobs[i].hp = 3 + floor * 2;
            dungMobs[i].maxHp = dungMobs[i].hp;
            dungMobs[i].alive = 1;
            dungMobs[i].type = MOB_SKELETON;
        }
    }
    /* Random Crypt Boss (1 of 100 distinct bosses) on floors 2,4,6,8,10 */
    if ((floor % 2 == 0) || floor == 10) {
        int bid = ((int)(rnd() * 100.0f)) % 100;
        int bhp = 24 + floor * 4 + (bid % 10) * 2;
        dungMobs[MAX_MOBS - 1].x = 25;
        dungMobs[MAX_MOBS - 1].y = 10;
        dungMobs[MAX_MOBS - 1].hp = bhp;
        dungMobs[MAX_MOBS - 1].maxHp = bhp;
        dungMobs[MAX_MOBS - 1].alive = 1;
        dungMobs[MAX_MOBS - 1].type = MOB_BOSS;
        dungMobs[MAX_MOBS - 1].bossId = bid;
    }
}

static void save_game(void) {
    SceUID fd = sceIoOpen("save.dat", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, &player, sizeof(player));
        sceIoWrite(fd, &skills, sizeof(skills));
        sceIoWrite(fd, &stats, sizeof(stats));
        sceIoWrite(fd, &gameTime, sizeof(gameTime));
        sceIoWrite(fd, world, sizeof(world));
        sceIoWrite(fd, dungeon, sizeof(dungeon));
        sceIoWrite(fd, mobs, sizeof(mobs));
        sceIoWrite(fd, dungMobs, sizeof(dungMobs));
        sceIoClose(fd);
        audio_play_sfx(SFX_CRAFT);
        set_msg("Partida guardada!");
    } else {
        set_msg("Error al guardar");
    }
}

static void load_game(void) {
    SceUID fd = sceIoOpen("save.dat", PSP_O_RDONLY, 0777);
    if (fd >= 0) {
        sceIoRead(fd, &player, sizeof(player));
        sceIoRead(fd, &skills, sizeof(skills));
        sceIoRead(fd, &stats, sizeof(stats));
        sceIoRead(fd, &gameTime, sizeof(gameTime));
        sceIoRead(fd, world, sizeof(world));
        sceIoRead(fd, dungeon, sizeof(dungeon));
        sceIoRead(fd, mobs, sizeof(mobs));
        sceIoRead(fd, dungMobs, sizeof(dungMobs));
        sceIoClose(fd);
        set_msg("Partida cargada!");
    }
}

static void move_player(int dx, int dy, int direct) {
    if (!direct && (player.fx != dx || player.fy != dy)) {
        player.fx = dx;
        player.fy = dy;
        audio_play_sfx(SFX_UI);
        return;
    }
    player.fx = dx;
    player.fy = dy;
    int mapW = player.inDungeon ? DUNG_W : WORLD_SIZE;
    int mapH = player.inDungeon ? DUNG_H : WORLD_SIZE;
    unsigned char *curMap = player.inDungeon ? dungeon : world;

    int nx = clampi(player.x + dx, 0, mapW - 1);
    int ny = clampi(player.y + dy, 0, mapH - 1);
    if (mob_at(nx, ny) >= 0) {
        audio_play_sfx(SFX_BLOCKED);
        set_msg("Enemigo! Pulsa [X]");
        return;
    }
    int t = curMap[ny * mapW + nx];
    if (t == T_WATER && !player.hasBoat) {
        audio_play_sfx(SFX_BLOCKED);
        set_msg("Necesitas Barca! (Craft)");
        return;
    }
    if (is_walkable(t)) {
        ui.gatherTimer = 0;
        player.x = nx; player.y = ny;
        player.inBoat = (t == T_WATER) ? 1 : 0;
        ui.walkTimer = 10;
        ui.walkFrame ^= 1;
        stats.steps++;
        if (stats.steps % 10 == 0) add_xp(&skills.supervivencia, "Sup", 2, 0);
        player.stamina = clampf(player.stamina - (player.wet > 0 ? 1.5f : 0.6f), 0.0f, 100.0f);

        if (t == T_DUNGEON_DOOR) {
            player.savedX = nx; player.savedY = ny;
            player.inDungeon = 1; player.inBoat = 0;
            player.dungeonFloor = 1;
            generate_dungeon_floor(1);
            player.x = 3; player.y = 10; player.fx = 1; player.fy = 0;
            audio_play_sfx(SFX_DOOR);
            set_msg("Cripta Piso 1/10!");
            return;
        }
        if (t == T_STAIRS_DOWN) {
            if (player.dungeonFloor < 10) {
                player.dungeonFloor++;
                generate_dungeon_floor(player.dungeonFloor);
                player.x = 3; player.y = 10; player.fx = 1; player.fy = 0;
                audio_play_sfx(SFX_DOOR);
                char b[32];
                snprintf(b, sizeof(b), "Bajaste al Piso %d/10!", player.dungeonFloor);
                set_msg(b);
            }
            return;
        }
        if (t == T_EXIT) {
            player.inDungeon = 0; player.inBoat = 0;
            player.x = player.savedX; player.y = player.savedY + 1;
            player.fx = 0; player.fy = 1;
            audio_play_sfx(SFX_DOOR);
            set_msg("Saliste de la Cripta!");
            return;
        }
        if (t == T_WATER) {
            audio_play_sfx(SFX_SPLASH);
        } else {
            audio_play_sfx(SFX_STEP);
        }
    } else {
        audio_play_sfx(SFX_BLOCKED);
        set_msg("Bloqueado");
    }
    if (!player.inDungeon) check_near_fire();
}

static void hit_mob_at(int hx, int hy, int dmg) {
    int mapW = player.inDungeon ? DUNG_W : WORLD_SIZE;
    int mapH = player.inDungeon ? DUNG_H : WORLD_SIZE;
    unsigned char *curMap = player.inDungeon ? dungeon : world;
    Mob *curMobs = player.inDungeon ? dungMobs : mobs;
    if (hx < 0 || hy < 0 || hx >= mapW || hy >= mapH) return;

    int mb = mob_at(hx, hy);
    if (mb < 0) return;
    curMobs[mb].hp -= dmg;
    if (player.slot == 0 && player.swordPct < 100) {
        player.swordPct += 2;
        if (player.swordPct > 100) player.swordPct = 100;
    }
    audio_play_sfx(curMobs[mb].type == MOB_SKELETON ? SFX_BONE_HIT : SFX_HIT);
    ui.shakeTimer = 4;
    if (curMobs[mb].hp <= 0) {
        curMobs[mb].alive = 0;
        if (player.slot == 0 && player.swordPct < 100) {
            player.swordPct += 4;
            if (player.swordPct > 100) player.swordPct = 100;
        }
        if (curMobs[mb].type == MOB_BOSS) {
            player.armorFrags++;
            player.bones += 3;
            player.iron += 3;
            spawn_pop(hx, hy, "+1 FRAG ARM!", 0xFF7AE2FF);
            set_msg("BOSS vencido! +1 Frag.Armadura");
            if (player.armorFrags >= 50) {
                player.armorFrags -= 50;
                player.armorPieces++;
                player.hp = clampf(player.hp + 20.0f, 0.0f, (float)maxHp());
                audio_play_sfx(SFX_LEVELUP);
                spawn_pop(player.x, player.y, "ARMADURA +1!", 0xFF66EE66);
                set_msg("Forjada 1 Parte de Armadura!");
            }
            add_xp(&skills.combate, "Comb", 40, 1);
            audio_play_sfx(SFX_CRAFT);
        } else if (curMobs[mb].type == MOB_SKELETON) {
            player.bones += 2; player.iron += 1;
            spawn_pop(hx, hy, "+2 Hue +1 Hie", 0xFF7AE2FF);
            set_msg("Esqueleto derrotado!");
            add_xp(&skills.combate, "Comb", 15, 1);
            audio_play_sfx(SFX_PICKUP);
        } else {
            player.food++; player.iron++;
            spawn_pop(hx, hy, "+1 Bay +1 Hie", 0xFF7AE2FF);
            set_msg("Slime derrotado!");
            add_xp(&skills.combate, "Comb", 15, 1);
            audio_play_sfx(SFX_PICKUP);
        }
        stats.slimes++;
    } else {
        /* Zelda Game Boy knockback */
        int kx = clampi(curMobs[mb].x + player.fx, 0, mapW - 1);
        int ky = clampi(curMobs[mb].y + player.fy, 0, mapH - 1);
        if (is_walkable(curMap[ky * mapW + kx]) && mob_at(kx, ky) < 0 && !(kx == player.x && ky == player.y)) {
            curMobs[mb].x = kx; curMobs[mb].y = ky;
        }
        add_xp(&skills.combate, "Comb", 4, 1);
        spawn_pop(hx, hy, "HIT!", 0xFF8C8CFF);
    }
}

static void drink_potion(void) {
    static const char *pnames[7] = {"Roja", "Azul", "Verde", "Dorada", "Oscura", "Blanca", "Violeta"};
    int p = player.selPotion % 7;
    if (player.potions[p] <= 0) {
        int found = -1;
        for (int i = 0; i < 7; i++) {
            if (player.potions[i] > 0) { found = i; break; }
        }
        if (found < 0) {
            audio_play_sfx(SFX_BLOCKED);
            set_msg("Sin pociones! Busca cofres");
            return;
        }
        p = found;
        player.selPotion = p;
    }
    player.potions[p]--;
    int mhp = maxHp();
    audio_play_sfx(SFX_DRINK);
    if (p == 0) { /* Roja: +50 HP, -20 Sed */
        player.hp = clampf(player.hp + 50.0f, 0.0f, (float)mhp);
        player.thirst = clampf(player.thirst - 20.0f, 0.0f, 100.0f);
        spawn_pop(player.x, player.y, "+50HP -20SD", 0xFF6666FF);
    } else if (p == 1) { /* Azul: +35 HP +40 Sed, -15 Hambre */
        player.hp = clampf(player.hp + 35.0f, 0.0f, (float)mhp);
        player.thirst = clampf(player.thirst + 40.0f, 0.0f, 100.0f);
        player.hunger = clampf(player.hunger - 15.0f, 0.0f, 100.0f);
        spawn_pop(player.x, player.y, "+35HP +40SD", 0xFFFFB055);
    } else if (p == 2) { /* Verde: +30 HP +30 Hambre, -25 Energia */
        player.hp = clampf(player.hp + 30.0f, 0.0f, (float)mhp);
        player.hunger = clampf(player.hunger + 30.0f, 0.0f, 100.0f);
        player.stamina = clampf(player.stamina - 25.0f, 0.0f, 100.0f);
        spawn_pop(player.x, player.y, "+30HP +30HM", 0xFF66EE66);
    } else if (p == 3) { /* Dorada: +45 HP +5% Espada, -20 HM/SD */
        player.hp = clampf(player.hp + 45.0f, 0.0f, (float)mhp);
        if (player.swordPct < 100) player.swordPct = clampi(player.swordPct + 5, 0, 100);
        player.hunger = clampf(player.hunger - 20.0f, 0.0f, 100.0f);
        player.thirst = clampf(player.thirst - 20.0f, 0.0f, 100.0f);
        spawn_pop(player.x, player.y, "+45HP +5%ESP", 0xFF66E2FF);
    } else if (p == 4) { /* Oscura: +80 HP, moja ropa + -30 Energia */
        player.hp = clampf(player.hp + 80.0f, 0.0f, (float)mhp);
        player.wet = 15;
        player.stamina = clampf(player.stamina - 30.0f, 0.0f, 100.0f);
        spawn_pop(player.x, player.y, "+80HP MOJADO", 0xFFCC88FF);
    } else if (p == 5) { /* Blanca: +25 HP + seca ropa, -10 Energia */
        player.hp = clampf(player.hp + 25.0f, 0.0f, (float)mhp);
        player.wet = 0;
        player.stamina = clampf(player.stamina - 10.0f, 0.0f, 100.0f);
        spawn_pop(player.x, player.y, "+25HP SECO", 0xFFFFFFFF);
    } else { /* Violeta: +60 HP +50 Energia, -25 Hambre */
        player.hp = clampf(player.hp + 60.0f, 0.0f, (float)mhp);
        player.stamina = clampf(player.stamina + 50.0f, 0.0f, 100.0f);
        player.hunger = clampf(player.hunger - 25.0f, 0.0f, 100.0f);
        spawn_pop(player.x, player.y, "+60HP +50EN", 0xFFEE88FF);
    }
    char msg[40];
    snprintf(msg, sizeof(msg), "Pocion %s!", pnames[p]);
    set_msg(msg);
}

static void start_gather(int tx, int ty, int kind, int baseFrames, int skLv) {
    float toolMul = player.ironTools ? 0.65f : 1.0f;
    float skillMul = 1.0f - (float)(skLv - 1) * 0.0001f; /* 0.01% per level */
    if (skillMul < 0.25f) skillMul = 0.25f;
    int dur = (int)((float)baseFrames * toolMul * skillMul);
    if (dur < 15) dur = 15;
    ui.gatherX = tx; ui.gatherY = ty;
    ui.gatherKind = kind;
    ui.gatherMax = dur;
    ui.gatherTimer = dur;
    audio_play_sfx(kind == 1 ? SFX_CHOP : SFX_MINE);
    set_msg(kind == 1 ? "Talando..." : "Picando...");
}

static void complete_gather(int tx, int ty) {
    int ti = ty * WORLD_SIZE + tx;
    int t = world[ti];
    int mult = player.ironTools ? 2 : 1;
    if (t == T_TREE || t == T_PINE) {
        world[ti] = (biomes[ti] == B_FOREST) ? T_FOREST : (biomes[ti] == B_TUNDRA ? T_SNOW : T_GRASS);
        int gain = 2 * mult + (rnd() < (skills.tala.lv - 1) * 0.08f ? 1 : 0);
        player.wood += gain; player.stamina = clampf(player.stamina - 3.0f, 0.0f, 100.0f); stats.trees++;
        add_xp(&skills.tala, "Tala", 8, 0);
        audio_play_sfx(SFX_CHOP);
        ui.shakeTimer = 4;
        spawn_pop(tx, ty, "+Mad", 0xFF66A0D9);
        set_msg("Talaste madera");
    } else if (t == T_ROCK || t == T_RUIN) {
        world[ti] = (biomes[ti] == B_DESERT) ? T_SAND : (biomes[ti] == B_TUNDRA ? T_SNOW : T_GRASS);
        int gain = 2 * mult + (rnd() < (skills.mineria.lv - 1) * 0.08f ? 1 : 0);
        player.stone += gain; player.stamina = clampf(player.stamina - 3.0f, 0.0f, 100.0f); stats.ores++;
        add_xp(&skills.mineria, "Min", 8, 0);
        audio_play_sfx(SFX_MINE);
        ui.shakeTimer = 4;
        spawn_pop(tx, ty, "+Pie", 0xFFE6D6CF);
        set_msg("Picaste piedra");
    } else if (t == T_IRON) {
        world[ti] = T_SNOW;
        int gain = 2 * mult + (rnd() < (skills.mineria.lv - 1) * 0.08f ? 1 : 0);
        player.iron += gain; player.stone++; player.stamina = clampf(player.stamina - 4.0f, 0.0f, 100.0f); stats.ores++;
        add_xp(&skills.mineria, "Min", 14, 0);
        audio_play_sfx(SFX_MINE);
        ui.shakeTimer = 4;
        spawn_pop(tx, ty, "+Hierro", 0xFF9ED2FF);
        set_msg("Minaste hierro!");
    }
}

static void use_tool(void) {
    if (player.slot == 6) {
        drink_potion();
        return;
    }
    if (player.slot == 5) {
        if (player.food > 0) {
            player.food--;
            player.hunger = clampf(player.hunger + 30.0f, 0.0f, 100.0f);
            player.hp = clampf(player.hp + 10.0f, 0.0f, (float)maxHp());
            add_xp(&skills.supervivencia, "Sup", 4, 0);
            audio_play_sfx(SFX_EAT);
            spawn_pop(player.x, player.y, "+30 HM", 0xFFA3E6A8);
            set_msg("Comiste baya!");
        } else {
            audio_play_sfx(SFX_BLOCKED);
            set_msg("Sin bayas!");
        }
        return;
    }

    int mapW = player.inDungeon ? DUNG_W : WORLD_SIZE;
    int mapH = player.inDungeon ? DUNG_H : WORLD_SIZE;
    unsigned char *curMap = player.inDungeon ? dungeon : world;

    int tx = clampi(player.x + player.fx, 0, mapW - 1);
    int ty = clampi(player.y + player.fy, 0, mapH - 1);
    int ti = ty * mapW + tx;
    int t = curMap[ti];
    int mult = player.ironTools ? 2 : 1;
    ui.lungeTimer = 10;
    ui.actionType = (player.slot == 3 || player.slot == 4) ? 2 : 1;

    /* Distinct Weapon Attacks by Tier: 0=Estocada, 1=Barrido, 2=Distancia, 3=Area 360 */
    if (player.slot == 0) {
        int dmg = 2 + player.swordTier * 2 + (player.swordPct / 25) + (skills.combate.lv - 1) / 3;
        int hitAny = 0;
        audio_play_sfx(SFX_SLASH);

        if (player.swordTier == 0) {
            /* Madera: 2-tile precision thrust */
            ui.slashTimer = 8;
            for (int d = 1; d <= 2; d++) {
                int hx = player.x + player.fx * d;
                int hy = player.y + player.fy * d;
                if (mob_at(hx, hy) >= 0) { hit_mob_at(hx, hy, dmg); hitAny = 1; break; }
            }
        } else if (player.swordTier == 1) {
            /* Hierro: 3-tile Zelda wide sweep */
            ui.slashTimer = 8;
            for (int a = -1; a <= 1; a++) {
                int hx = player.x + player.fx + (player.fy != 0 ? a : 0);
                int hy = player.y + player.fy + (player.fx != 0 ? a : 0);
                if (mob_at(hx, hy) >= 0) { hit_mob_at(hx, hy, dmg); hitAny = 1; }
            }
        } else if (player.swordTier == 2) {
            /* Acero: Ranged energy wave projectile (6 tiles) */
            ui.slashTimer = 6;
            if (mob_at(tx, ty) >= 0) {
                hit_mob_at(tx, ty, dmg);
                hitAny = 1;
            } else {
                proj.x = player.x + player.fx;
                proj.y = player.y + player.fy;
                proj.dx = player.fx;
                proj.dy = player.fy;
                proj.life = 6;
                proj.dmg = dmg;
                proj.active = 1;
                return;
            }
        } else {
            /* Real: 360-degree Area Whirlwind (all 8 surrounding tiles) */
            ui.spinTimer = 10;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int hx = player.x + dx;
                    int hy = player.y + dy;
                    if (mob_at(hx, hy) >= 0) { hit_mob_at(hx, hy, dmg); hitAny = 1; }
                }
            }
        }
        if (hitAny) return;
        if (t != T_BERRY && t != T_CHEST && t != T_WATER && t != T_TREE && t != T_PINE && t != T_CACTUS) {
            return;
        }
    }

    if (t == T_WATER && player.slot < 3) {
        player.thirst = clampf(player.thirst + 35.0f, 0.0f, 100.0f);
        add_xp(&skills.supervivencia, "Sup", 3, 0);
        audio_play_sfx(SFX_DRINK);
        spawn_pop(tx, ty, "+35 Sed", 0xFFFFA959);
        set_msg("Bebiste agua!");
        return;
    }

    if (mob_at(tx, ty) >= 0) {
        hit_mob_at(tx, ty, mult + (skills.combate.lv - 1) / 3);
        return;
    }

    if (t == T_BERRY) {
        curMap[ti] = T_GRASS; player.food += 2;
        add_xp(&skills.supervivencia, "Sup", 5, 0);
        audio_play_sfx(SFX_PICKUP);
        spawn_pop(tx, ty, "+2 Bayas", 0xFFB899FF);
        set_msg("+2 Bayas");
    } else if (t == T_CHEST) {
        static const char *pnames[7] = {"Roja", "Azul", "Verde", "Dorada", "Oscura", "Blanca", "Violeta"};
        curMap[ti] = player.inDungeon ? T_DFLOOR : T_SAND;
        player.iron += 5; player.food += 3; player.bones += 2; stats.chests++;
        int pt = ((int)(rnd() * 7.0f)) % 7;
        player.potions[pt]++;
        player.selPotion = pt;
        if (player.inDungeon && player.swordTier < 3) {
            player.swordTier++;
            spawn_pop(tx, ty, "ESPADA+POC!", 0xFF7AE2FF);
            char cb[40];
            snprintf(cb, sizeof(cb), "Espada +1 & Poc.%s!", pnames[pt]);
            set_msg(cb);
        } else {
            spawn_pop(tx, ty, "COFRE+POC!", 0xFF7AE2FF);
            char cb[40];
            snprintf(cb, sizeof(cb), "Cofre! +Poc.%s", pnames[pt]);
            set_msg(cb);
        }
        add_xp(&skills.supervivencia, "Sup", 25, 0);
        audio_play_sfx(SFX_CRAFT);
        ui.shakeTimer = 5;
    } else if (player.inDungeon) {
        audio_play_sfx(SFX_BLOCKED);
        set_msg("Piedra de mazmorra");
        return;
    } else if ((t == T_TREE || t == T_PINE) && player.slot <= 1) {
        if (ui.gatherTimer <= 0) start_gather(tx, ty, 1, 180, skills.tala.lv);
    } else if (t == T_CACTUS && player.slot <= 1) {
        world[ti] = T_SAND; player.thirst = clampf(player.thirst + 20.0f, 0.0f, 100.0f); player.wood++;
        add_xp(&skills.supervivencia, "Sup", 5, 0);
        audio_play_sfx(SFX_DRINK);
        spawn_pop(tx, ty, "+20 Sed", 0xFFFFA959);
        set_msg("Cactus +20 Sed");
    } else if ((t == T_ROCK || t == T_RUIN) && player.slot == 2) {
        if (ui.gatherTimer <= 0) start_gather(tx, ty, 2, 180, skills.mineria.lv);
    } else if (t == T_IRON && player.slot == 2) {
        if (ui.gatherTimer <= 0) start_gather(tx, ty, 2, 240, skills.mineria.lv);
    } else if (player.slot == 3 && player.campfires > 0 && is_walkable(t) && t != T_WATER) {
        world[ti] = T_FIRE; player.campfires--; check_near_fire();
        add_xp(&skills.construccion, "Cons", 10, 0);
        audio_play_sfx(SFX_BUILD);
        set_msg("Hoguera encendida!");
    } else if (player.slot == 4 && player.walls > 0 && is_walkable(t) && t != T_WATER) {
        world[ti] = T_WALL; player.walls--;
        add_xp(&skills.construccion, "Cons", 6, 0);
        audio_play_sfx(SFX_BUILD);
        set_msg("Muro construido!");
    } else {
        audio_play_sfx(SFX_BLOCKED);
        set_msg("Usa otra herramienta");
    }
}

static void craft_selected(int sel) {
    if (sel == 0) {
        if (player.wood >= 3 && player.stone >= 2) {
            player.wood -= 3; player.stone -= 2; player.campfires++; stats.crafted++;
            add_xp(&skills.construccion, "Cons", 12, 0);
            audio_play_sfx(SFX_CRAFT); set_msg("Crafteada Hoguera!");
        } else { audio_play_sfx(SFX_BLOCKED); set_msg("Falta 3 Mad, 2 Pie"); }
    } else if (sel == 1) {
        if (player.wood >= 2 && player.stone >= 1) {
            player.wood -= 2; player.stone -= 1; player.walls += 2; stats.crafted++;
            add_xp(&skills.construccion, "Cons", 8, 0);
            audio_play_sfx(SFX_CRAFT); set_msg("Crafteados 2 Muros!");
        } else { audio_play_sfx(SFX_BLOCKED); set_msg("Falta 2 Mad, 1 Pie"); }
    } else if (sel == 2) {
        if (player.food >= 2 && player.wood >= 1) {
            player.food -= 2; player.wood -= 1;
            player.hunger = clampf(player.hunger + 45.0f, 0.0f, 100.0f);
            player.hp = clampf(player.hp + 20.0f, 0.0f, (float)maxHp());
            stats.crafted++; add_xp(&skills.supervivencia, "Sup", 10, 0);
            audio_play_sfx(SFX_EAT); set_msg("Racion! +45 Hambre");
        } else { audio_play_sfx(SFX_BLOCKED); set_msg("Falta 2 Bayas, 1 Mad"); }
    } else if (sel == 3) {
        if (!player.ironTools && player.iron >= 4 && player.wood >= 2) {
            player.iron -= 4; player.wood -= 2; player.ironTools = 1; stats.crafted++;
            add_xp(&skills.construccion, "Cons", 30, 0);
            audio_play_sfx(SFX_CRAFT); set_msg("Equipo de HIERRO!");
        } else { audio_play_sfx(SFX_BLOCKED); set_msg("Falta 4 Hie, 2 Mad"); }
    } else if (sel == 4) {
        if (player.swordTier >= 3) {
            audio_play_sfx(SFX_BLOCKED); set_msg("Espada al maximo!");
        } else if (player.iron >= 3 && player.bones >= 3) {
            player.iron -= 3; player.bones -= 3; player.swordTier++; stats.crafted++;
            add_xp(&skills.combate, "Comb", 25, 1);
            audio_play_sfx(SFX_LEVELUP); set_msg("Espada mejorada!");
        } else { audio_play_sfx(SFX_BLOCKED); set_msg("Falta 3 Hie, 3 Hue"); }
    } else if (sel == 5) {
        if (player.hasBoat) {
            audio_play_sfx(SFX_BLOCKED); set_msg("Ya tienes Barca!");
        } else if (player.wood >= 5) {
            player.wood -= 5; player.hasBoat = 1; stats.crafted++;
            add_xp(&skills.construccion, "Cons", 18, 0);
            audio_play_sfx(SFX_CRAFT); set_msg("Barca construida!");
        } else { audio_play_sfx(SFX_BLOCKED); set_msg("Falta 5 Madera"); }
    } else if (sel == 6) {
        if (player.warm && !player.inDungeon) {
            player.stamina = 100.0f; player.hp = clampf(player.hp + 30.0f, 0.0f, (float)maxHp());
            gameTime.day++; gameTime.minute = 420; gameTime.season = ((gameTime.day - 1) / 3) % 4;
            add_xp(&skills.supervivencia, "Sup", 15, 0);
            ui.craftOpen = 0; save_game();
        } else { audio_play_sfx(SFX_BLOCKED); set_msg("Necesitas Hoguera fuera!"); }
    }
}

static int exit_cb(int arg1, int arg2, void *common) { running = 0; return 0; }
static int cb_thread(SceSize args, void *argp) {
    int cbid = sceKernelCreateCallback("Exit Callback", exit_cb, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void) {
    int thid = sceKernelCreateThread("update_thread", cb_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);
}

int main(void) {
    setup_callbacks();
    render_init();
    audio_init();

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    memset(&player, 0, sizeof(player));
    player.x = 32; player.y = 32; player.fy = 1;
    player.hp = 100; player.hunger = 100; player.thirst = 100; player.stamina = 100;
    player.wood = 2; player.stone = 1; player.campfires = 1; player.walls = 2; player.food = 2;

    skills.tala.lv = 1; skills.mineria.lv = 1; skills.combate.lv = 1;
    skills.supervivencia.lv = 1; skills.construccion.lv = 1;
    memset(&stats, 0, sizeof(stats));

    gameTime.day = 1; gameTime.minute = 480; gameTime.season = 1;
    ui.sfxOn = 1;
    ui.inTitle = 1;
    ui.titleSel = 0;
    set_msg("Explora Pixelaria!");

    generate_world();
    generate_dungeon_floor(1);

    SceCtrlData pad, oldPad;
    sceCtrlReadBufferPositive(&oldPad, 1);
    int moveCd = 0, frameCnt = 0;

    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~oldPad.Buttons;

        if (ui.inTitle) {
            if (ui.showHelp) {
                if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE)) {
                    ui.showHelp = 0;
                    audio_play_sfx(SFX_UI);
                }
            } else {
                if (pressed & PSP_CTRL_UP) { ui.titleSel = (ui.titleSel + 2) % 3; audio_play_sfx(SFX_UI); }
                if (pressed & PSP_CTRL_DOWN) { ui.titleSel = (ui.titleSel + 1) % 3; audio_play_sfx(SFX_UI); }
                if (pressed & PSP_CTRL_CROSS) {
                    if (ui.titleSel == 0) {
                        load_game();
                        ui.inTitle = 0;
                        audio_play_sfx(SFX_CRAFT);
                    } else if (ui.titleSel == 1) {
                        generate_world();
                        generate_dungeon_floor(1);
                        ui.inTitle = 0;
                        audio_play_sfx(SFX_CRAFT);
                    } else {
                        ui.showHelp = 1;
                        audio_play_sfx(SFX_UI);
                    }
                }
            }
            oldPad = pad;
            render_frame();
            continue;
        }

        if (pressed & PSP_CTRL_SELECT) {
            ui.sfxOn = !ui.sfxOn;
            set_msg(ui.sfxOn ? "SFX: ON" : "SFX: OFF");
        }
        if (pressed & PSP_CTRL_START) save_game();

        if (pressed & PSP_CTRL_TRIANGLE) {
            ui.statsOpen = 0;
            ui.craftOpen = !ui.craftOpen;
            audio_play_sfx(SFX_UI);
        }
        if (pressed & PSP_CTRL_CIRCLE) {
            ui.craftOpen = 0;
            ui.statsOpen = !ui.statsOpen;
            audio_play_sfx(SFX_UI);
        }

        if (ui.craftOpen) {
            if (pressed & PSP_CTRL_UP) { ui.menuSel = (ui.menuSel + 6) % 7; audio_play_sfx(SFX_UI); }
            if (pressed & PSP_CTRL_DOWN) { ui.menuSel = (ui.menuSel + 1) % 7; audio_play_sfx(SFX_UI); }
            if (pressed & PSP_CTRL_CROSS) craft_selected(ui.menuSel);
        } else if (!ui.statsOpen) {
            if (pressed & (PSP_CTRL_SQUARE | PSP_CTRL_RTRIGGER)) {
                player.slot = (player.slot + 1) % 7;
                audio_play_sfx(SFX_UI);
            }
            if (pressed & PSP_CTRL_LTRIGGER) {
                player.slot = (player.slot + 6) % 7;
                audio_play_sfx(SFX_UI);
            }
            if (pressed & PSP_CTRL_CROSS) use_tool();

            if (moveCd > 0) moveCd--;
            else {
                int dx = 0, dy = 0, direct = 0;
                if (pad.Ly < 60) { dy = -1; direct = 1; }
                else if (pad.Ly > 195) { dy = 1; direct = 1; }
                else if (pad.Lx < 60) { dx = -1; direct = 1; }
                else if (pad.Lx > 195) { dx = 1; direct = 1; }
                else if (pad.Buttons & PSP_CTRL_UP) dy = -1;
                else if (pad.Buttons & PSP_CTRL_DOWN) dy = 1;
                else if (pad.Buttons & PSP_CTRL_LEFT) dx = -1;
                else if (pad.Buttons & PSP_CTRL_RIGHT) dx = 1;
                if (dx != 0 || dy != 0) {
                    move_player(dx, dy, direct);
                    moveCd = direct ? 7 : 9;
                }
            }
        }

        oldPad = pad;
        if (ui.shakeTimer > 0) ui.shakeTimer--;
        if (ui.damageFlash > 0) ui.damageFlash--;
        if (ui.lungeTimer > 0) ui.lungeTimer--;
        if (ui.walkTimer > 0) ui.walkTimer--;
        if (ui.slashTimer > 0) ui.slashTimer--;
        if (ui.spinTimer > 0) ui.spinTimer--;
        if (player.iframes > 0) player.iframes--;
        if (ui.gatherTimer > 0) {
            ui.gatherTimer--;
            if (ui.gatherTimer > 0 && (ui.gatherTimer % 30 == 0)) {
                audio_play_sfx(ui.gatherKind == 1 ? SFX_CHOP : SFX_MINE);
                ui.lungeTimer = 6;
                ui.shakeTimer = 2;
            }
            if (ui.gatherTimer == 0) {
                complete_gather(ui.gatherX, ui.gatherY);
            }
        }
        if (proj.active && (frameCnt % 3 == 0)) {
            int mapW = player.inDungeon ? DUNG_W : WORLD_SIZE;
            int mapH = player.inDungeon ? DUNG_H : WORLD_SIZE;
            unsigned char *curMap = player.inDungeon ? dungeon : world;
            if (proj.x < 0 || proj.y < 0 || proj.x >= mapW || proj.y >= mapH || !is_walkable(curMap[proj.y * mapW + proj.x])) {
                proj.active = 0;
            } else if (mob_at(proj.x, proj.y) >= 0) {
                hit_mob_at(proj.x, proj.y, proj.dmg);
                proj.active = 0;
            } else if (proj.life <= 0) {
                proj.active = 0;
            } else {
                proj.x += proj.dx;
                proj.y += proj.dy;
                proj.life--;
            }
        }
        for (int i = 0; i < MAX_POPS; i++) if (pops[i].timer > 0) pops[i].timer--;

        /* Fast & Dangerous Enemy AI (~2x per second, smart 2-axis chase) */
        if (frameCnt % 27 == 0) {
            int mapW = player.inDungeon ? DUNG_W : WORLD_SIZE;
            int mapH = player.inDungeon ? DUNG_H : WORLD_SIZE;
            unsigned char *curMap = player.inDungeon ? dungeon : world;
            Mob *curMobs = player.inDungeon ? dungMobs : mobs;
            for (int i = 0; i < MAX_MOBS; i++) {
                if (!curMobs[i].alive) continue;
                int dx = player.x - curMobs[i].x;
                int dy = player.y - curMobs[i].y;
                int d = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                int aggro = (curMobs[i].type == MOB_BOSS) ? 14 : ((curMobs[i].type == MOB_SKELETON) ? 12 : 8);

                if (d == 1 && player.iframes <= 0) {
                    int raw = (curMobs[i].type == MOB_BOSS) ? (12 + (curMobs[i].bossId % 5) * 3)
                            : ((curMobs[i].type == MOB_SKELETON) ? (8 + player.dungeonFloor * 2) : 6);
                    int edmg = raw - player.armorPieces * 2;
                    if (edmg < 1) edmg = 1;
                    player.hp = clampf(player.hp - (float)edmg, 0.0f, (float)maxHp());
                    player.iframes = 28;
                    audio_play_sfx(curMobs[i].type != MOB_SLIME ? SFX_SKEL_ATK : SFX_HURT);
                    ui.damageFlash = 8; ui.shakeTimer = 5;
                    char db[16];
                    snprintf(db, sizeof(db), "-%d HP", edmg);
                    spawn_pop(player.x, player.y, db, 0xFF4444FF);
                } else if (d > 1 && d <= aggro) {
                    int sx = (dx > 0) ? 1 : ((dx < 0) ? -1 : 0);
                    int sy = (dy > 0) ? 1 : ((dy < 0) ? -1 : 0);
                    int moved = 0;
                    int tryXFirst = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy);
                    for (int pass = 0; pass < 2 && !moved; pass++) {
                        int nx = curMobs[i].x + ((pass == 0 ? tryXFirst : !tryXFirst) ? sx : 0);
                        int ny = curMobs[i].y + ((pass == 0 ? !tryXFirst : tryXFirst) ? sy : 0);
                        if ((nx != curMobs[i].x || ny != curMobs[i].y) &&
                            nx > 1 && ny > 1 && nx < mapW - 1 && ny < mapH - 1 &&
                            is_walkable(curMap[ny * mapW + nx]) && curMap[ny * mapW + nx] != T_WATER &&
                            !(nx == player.x && ny == player.y) && mob_at(nx, ny) < 0) {
                            curMobs[i].x = nx;
                            curMobs[i].y = ny;
                            moved = 1;
                        }
                    }
                }
            }
        }

        /* 60 FPS -> 1 game tick per second */
        frameCnt++;
        if (frameCnt >= 60) {
            frameCnt = 0;
            gameTime.ticks++;
            gameTime.minute += 2;
            if (gameTime.minute >= 1440) {
                gameTime.minute = 0; gameTime.day++;
                gameTime.season = ((gameTime.day - 1) / 3) % 4;
                add_xp(&skills.supervivencia, "Sup", 15, 0);
            }
            if (gameTime.ticks % 45 == 0) {
                int curB = biomes[player.y * WORLD_SIZE + player.x];
                float wr = rnd();
                if (curB == B_TUNDRA || gameTime.season == 3) gameTime.weather = wr < 0.45f ? 2 : 0;
                else if (curB == B_DESERT) gameTime.weather = 0;
                else gameTime.weather = wr < 0.30f ? 1 : 0;
            }
            if (!player.inDungeon && gameTime.weather > 0 && !player.warm) {
                if (player.wet < 10) { player.wet = 10; set_msg("Ropa mojada!"); }
            } else if (player.wet > 0) player.wet--;

            float drain = 1.0f - (skills.supervivencia.lv - 1) * 0.03f;
            if (drain < 0.6f) drain = 0.6f;
            player.hunger = clampf(player.hunger - 0.07f * drain, 0.0f, 100.0f);
            player.thirst = clampf(player.thirst - (biomes[player.y * WORLD_SIZE + player.x] == B_DESERT ? 0.13f : 0.08f) * drain, 0.0f, 100.0f);
            player.stamina = clampf(player.stamina + (player.warm ? 3.0f : 0.8f), 0.0f, 100.0f);

            int mhp = maxHp();
            if (player.hunger <= 0.0f || player.thirst <= 0.0f) player.hp = clampf(player.hp - 2.0f, 0.0f, (float)mhp);

            if (player.hp <= 0.0f) {
                player.hp = (float)mhp; player.hunger = 80.0f; player.thirst = 80.0f; player.stamina = 100.0f;
                player.inDungeon = 0; player.x = 32; player.y = 32; set_msg("Desmayo!");
            }
        }

        render_frame();
    }

    audio_shutdown();
    sceKernelExitGame();
    return 0;
}
