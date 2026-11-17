#include "game.h"
#include <pspctrl.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>

PSP_MODULE_INFO("PixelariaPocket", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

unsigned char world[WORLD_SIZE * WORLD_SIZE];
unsigned char biomes[WORLD_SIZE * WORLD_SIZE];
Mob mobs[MAX_MOBS];
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
int maxHp(void) { return 100 + (skills.combate.lv - 1) * 5; }
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
    return t == T_GRASS || t == T_FOREST || t == T_SAND || t == T_SNOW || t == T_WATER || t == T_FIRE;
}

static int mob_at(int x, int y) {
    for (int i = 0; i < MAX_MOBS; i++) {
        if (mobs[i].alive && mobs[i].x == x && mobs[i].y == y) return i;
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
            if (x < 3 || y < 3 || x >= WORLD_SIZE - 3 || y >= WORLD_SIZE - 3) {
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
                world[i] = r < 0.04f ? T_WATER : (r < 0.14f ? T_TREE : (r < 0.20f ? T_BERRY : (r < 0.24f ? T_ROCK : T_GRASS)));
            } else if (b == B_FOREST) {
                world[i] = r < 0.28f ? T_TREE : (r < 0.36f ? T_PINE : (r < 0.41f ? T_BERRY : (r < 0.45f ? T_ROCK : T_FOREST)));
            } else if (b == B_DESERT) {
                world[i] = r < 0.08f ? T_CACTUS : (r < 0.13f ? T_ROCK : T_SAND);
            } else {
                world[i] = r < 0.07f ? T_WATER : (r < 0.18f ? T_PINE : (r < 0.24f ? T_IRON : (r < 0.28f ? T_ROCK : T_SNOW)));
            }
        }
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

    memset(mobs, 0, sizeof(mobs));
    for (int i = 0; i < 10; i++) {
        int mx = 10 + (int)(rnd() * 44.0f);
        int my = 10 + (int)(rnd() * 44.0f);
        if ((mx < 28 || mx > 36 || my < 28 || my > 36) && is_walkable(world[my * WORLD_SIZE + mx])) {
            mobs[i].x = mx; mobs[i].y = my; mobs[i].hp = 2; mobs[i].alive = 1;
        }
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
        sceIoWrite(fd, mobs, sizeof(mobs));
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
        sceIoRead(fd, mobs, sizeof(mobs));
        sceIoClose(fd);
        set_msg("Partida cargada!");
    }
}

static void move_player(int dx, int dy) {
    player.fx = dx; player.fy = dy;
    int nx = clampi(player.x + dx, 0, WORLD_SIZE - 1);
    int ny = clampi(player.y + dy, 0, WORLD_SIZE - 1);
    if (mob_at(nx, ny) >= 0) {
        audio_play_sfx(SFX_BLOCKED);
        set_msg("Slime! Pulsa [X]");
        return;
    }
    int t = world[ny * WORLD_SIZE + nx];
    if (is_walkable(t)) {
        player.x = nx; player.y = ny;
        stats.steps++;
        if (stats.steps % 10 == 0) add_xp(&skills.supervivencia, "Sup", 2, 0);
        player.stamina = clampf(player.stamina - (player.wet > 0 ? 1.5f : 0.6f), 0.0f, 100.0f);
        if (t == T_WATER) {
            player.wet = 20;
            audio_play_sfx(SFX_SPLASH);
            set_msg("Ropa mojada!");
        } else {
            audio_play_sfx(SFX_STEP);
        }
    } else {
        audio_play_sfx(SFX_BLOCKED);
        set_msg("Bloqueado");
    }
    check_near_fire();
}

static void use_tool(void) {
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

    int tx = clampi(player.x + player.fx, 0, WORLD_SIZE - 1);
    int ty = clampi(player.y + player.fy, 0, WORLD_SIZE - 1);
    int ti = ty * WORLD_SIZE + tx;
    int t = world[ti];
    int mb = mob_at(tx, ty);
    int mult = player.ironTools ? 2 : 1;
    ui.lungeTimer = 5;

    if (t == T_WATER && player.slot < 3) {
        player.thirst = clampf(player.thirst + 35.0f, 0.0f, 100.0f);
        add_xp(&skills.supervivencia, "Sup", 3, 0);
        audio_play_sfx(SFX_DRINK);
        spawn_pop(tx, ty, "+35 Sed", 0xFFFFA959);
        set_msg("Bebiste agua!");
        return;
    }

    if (mb >= 0) {
        int dmg = ((player.slot == 0 ? 2 : 1) * mult) + (skills.combate.lv - 1) / 3;
        mobs[mb].hp -= dmg;
        audio_play_sfx(SFX_HIT);
        ui.shakeTimer = 4;
        if (mobs[mb].hp <= 0) {
            mobs[mb].alive = 0;
            player.food++; player.iron++; stats.slimes++;
            add_xp(&skills.combate, "Comb", 15, 1);
            audio_play_sfx(SFX_PICKUP);
            spawn_pop(tx, ty, "+1 Bay +1 Hie", 0xFF7AE2FF);
            set_msg("Slime derrotado!");
        } else {
            add_xp(&skills.combate, "Comb", 4, 1);
            spawn_pop(tx, ty, "HIT!", 0xFF8C8CFF);
        }
        return;
    }

    if (t == T_BERRY) {
        world[ti] = T_GRASS; player.food += 2;
        add_xp(&skills.supervivencia, "Sup", 5, 0);
        audio_play_sfx(SFX_PICKUP);
        spawn_pop(tx, ty, "+2 Bayas", 0xFFB899FF);
        set_msg("+2 Bayas");
    } else if (t == T_CHEST) {
        world[ti] = T_SAND; player.iron += 5; player.food += 3; player.wood += 4; stats.chests++;
        add_xp(&skills.supervivencia, "Sup", 25, 0);
        audio_play_sfx(SFX_CRAFT);
        ui.shakeTimer = 5;
        spawn_pop(tx, ty, "COFRE!", 0xFF7AE2FF);
        set_msg("Cofre! +5 Hierro");
    } else if ((t == T_TREE || t == T_PINE) && player.slot <= 1) {
        world[ti] = (biomes[ti] == B_FOREST) ? T_FOREST : (biomes[ti] == B_TUNDRA ? T_SNOW : T_GRASS);
        int gain = 2 * mult + (rnd() < (skills.tala.lv - 1) * 0.08f ? 1 : 0);
        player.wood += gain; player.stamina = clampf(player.stamina - 3.0f, 0.0f, 100.0f); stats.trees++;
        add_xp(&skills.tala, "Tala", 8, 0);
        audio_play_sfx(SFX_CHOP);
        ui.shakeTimer = 4;
        spawn_pop(tx, ty, "+Mad", 0xFF66A0D9);
        set_msg("Talaste madera");
    } else if (t == T_CACTUS && player.slot <= 1) {
        world[ti] = T_SAND; player.thirst = clampf(player.thirst + 20.0f, 0.0f, 100.0f); player.wood++;
        add_xp(&skills.supervivencia, "Sup", 5, 0);
        audio_play_sfx(SFX_DRINK);
        spawn_pop(tx, ty, "+20 Sed", 0xFFFFA959);
        set_msg("Cactus +20 Sed");
    } else if ((t == T_ROCK || t == T_RUIN) && player.slot == 2) {
        world[ti] = (biomes[ti] == B_DESERT) ? T_SAND : (biomes[ti] == B_TUNDRA ? T_SNOW : T_GRASS);
        int gain = 2 * mult + (rnd() < (skills.mineria.lv - 1) * 0.08f ? 1 : 0);
        player.stone += gain; player.stamina = clampf(player.stamina - 3.0f, 0.0f, 100.0f); stats.ores++;
        add_xp(&skills.mineria, "Min", 8, 0);
        audio_play_sfx(SFX_MINE);
        ui.shakeTimer = 4;
        spawn_pop(tx, ty, "+Pie", 0xFFE6D6CF);
        set_msg("Picaste piedra");
    } else if (t == T_IRON && player.slot == 2) {
        world[ti] = T_SNOW;
        int gain = 2 * mult + (rnd() < (skills.mineria.lv - 1) * 0.08f ? 1 : 0);
        player.iron += gain; player.stone++; player.stamina = clampf(player.stamina - 4.0f, 0.0f, 100.0f); stats.ores++;
        add_xp(&skills.mineria, "Min", 14, 0);
        audio_play_sfx(SFX_MINE);
        ui.shakeTimer = 4;
        spawn_pop(tx, ty, "+Hierro", 0xFF9ED2FF);
        set_msg("Minaste hierro!");
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
        if (player.warm) {
            player.stamina = 100.0f; player.hp = clampf(player.hp + 30.0f, 0.0f, (float)maxHp());
            gameTime.day++; gameTime.minute = 420; gameTime.season = ((gameTime.day - 1) / 3) % 4;
            add_xp(&skills.supervivencia, "Sup", 15, 0);
            ui.craftOpen = 0; save_game();
        } else { audio_play_sfx(SFX_BLOCKED); set_msg("Necesitas Hoguera!"); }
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
    set_msg("Explora Pixelaria!");

    generate_world();
    load_game();

    SceCtrlData pad, oldPad;
    sceCtrlReadBufferPositive(&oldPad, 1);
    int moveCd = 0, frameCnt = 0;

    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~oldPad.Buttons;

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
            if (pressed & PSP_CTRL_UP) { ui.menuSel = (ui.menuSel + 4) % 5; audio_play_sfx(SFX_UI); }
            if (pressed & PSP_CTRL_DOWN) { ui.menuSel = (ui.menuSel + 1) % 5; audio_play_sfx(SFX_UI); }
            if (pressed & PSP_CTRL_CROSS) craft_selected(ui.menuSel);
        } else if (!ui.statsOpen) {
            if (pressed & (PSP_CTRL_SQUARE | PSP_CTRL_RTRIGGER)) {
                player.slot = (player.slot + 1) % 6;
                audio_play_sfx(SFX_UI);
            }
            if (pressed & PSP_CTRL_LTRIGGER) {
                player.slot = (player.slot + 5) % 6;
                audio_play_sfx(SFX_UI);
            }
            if (pressed & PSP_CTRL_CROSS) use_tool();

            if (moveCd > 0) moveCd--;
            else {
                int dx = 0, dy = 0;
                if ((pad.Buttons & PSP_CTRL_UP) || pad.Ly < 60) dy = -1;
                else if ((pad.Buttons & PSP_CTRL_DOWN) || pad.Ly > 195) dy = 1;
                else if ((pad.Buttons & PSP_CTRL_LEFT) || pad.Lx < 60) dx = -1;
                else if ((pad.Buttons & PSP_CTRL_RIGHT) || pad.Lx > 195) dx = 1;
                if (dx != 0 || dy != 0) {
                    move_player(dx, dy);
                    moveCd = 8;
                }
            }
        }

        oldPad = pad;
        if (ui.shakeTimer > 0) ui.shakeTimer--;
        if (ui.damageFlash > 0) ui.damageFlash--;
        if (ui.lungeTimer > 0) ui.lungeTimer--;
        for (int i = 0; i < MAX_POPS; i++) if (pops[i].timer > 0) pops[i].timer--;

        /* 60 FPS -> 1 game tick per second */
        frameCnt++;
        if (frameCnt >= 60) {
            frameCnt = 0;
            gameTime.ticks++;
            gameTime.minute += 10;
            if (gameTime.minute >= 1440) {
                gameTime.minute = 0; gameTime.day++;
                gameTime.season = ((gameTime.day - 1) / 3) % 4;
                add_xp(&skills.supervivencia, "Sup", 15, 0);
            }
            if (gameTime.ticks % 15 == 0) {
                int curB = biomes[player.y * WORLD_SIZE + player.x];
                float wr = rnd();
                if (curB == B_TUNDRA || gameTime.season == 3) gameTime.weather = wr < 0.45f ? 2 : 0;
                else if (curB == B_DESERT) gameTime.weather = 0;
                else gameTime.weather = wr < 0.30f ? 1 : 0;
            }
            if (gameTime.weather > 0 && !player.warm) {
                if (player.wet < 10) { player.wet = 10; set_msg("Ropa mojada!"); }
            } else if (player.wet > 0) player.wet--;

            float drain = 1.0f - (skills.supervivencia.lv - 1) * 0.03f;
            if (drain < 0.6f) drain = 0.6f;
            player.hunger = clampf(player.hunger - 0.35f * drain, 0.0f, 100.0f);
            player.thirst = clampf(player.thirst - (biomes[player.y * WORLD_SIZE + player.x] == B_DESERT ? 0.65f : 0.40f) * drain, 0.0f, 100.0f);
            player.stamina = clampf(player.stamina + (player.warm ? 3.0f : 0.8f), 0.0f, 100.0f);

            int mhp = maxHp();
            if (player.hunger <= 0.0f || player.thirst <= 0.0f) player.hp = clampf(player.hp - 2.0f, 0.0f, (float)mhp);
            else if (player.hunger > 60.0f && player.thirst > 60.0f && player.hp < (float)mhp) player.hp = clampf(player.hp + 0.5f, 0.0f, (float)mhp);

            if (player.hp <= 0.0f) {
                player.hp = (float)mhp; player.hunger = 80.0f; player.thirst = 80.0f; player.stamina = 100.0f;
                player.x = 32; player.y = 32; set_msg("Desmayo!");
            }

            if (gameTime.ticks % 2 == 0) {
                for (int i = 0; i < MAX_MOBS; i++) {
                    if (!mobs[i].alive) continue;
                    int d = (mobs[i].x > player.x ? mobs[i].x - player.x : player.x - mobs[i].x) +
                            (mobs[i].y > player.y ? mobs[i].y - player.y : player.y - mobs[i].y);
                    if (d == 1) {
                        player.hp = clampf(player.hp - 6.0f, 0.0f, (float)maxHp());
                        audio_play_sfx(SFX_HURT);
                        ui.damageFlash = 8; ui.shakeTimer = 5;
                        spawn_pop(player.x, player.y, "-6 HP", 0xFF4444FF);
                    } else if (d < 7 && rnd() < 0.6f) {
                        int mx = mobs[i].x + (player.x > mobs[i].x ? 1 : (player.x < mobs[i].x ? -1 : 0));
                        int my = mobs[i].y + (player.y > mobs[i].y ? 1 : (player.y < mobs[i].y ? -1 : 0));
                        if (rnd() < 0.5f) my = mobs[i].y; else mx = mobs[i].x;
                        if (mx > 2 && my > 2 && mx < WORLD_SIZE - 2 && my < WORLD_SIZE - 2 &&
                            is_walkable(world[my * WORLD_SIZE + mx]) && !(mx == player.x && my == player.y) && mob_at(mx, my) < 0) {
                            mobs[i].x = mx; mobs[i].y = my;
                        }
                    }
                }
            }
        }

        render_frame();
    }

    audio_shutdown();
    sceKernelExitGame();
    return 0;
}
