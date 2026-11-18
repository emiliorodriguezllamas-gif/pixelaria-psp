#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "game.h"

PSP_MODULE_INFO("NovaWing64", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

Player player;
Enemy enemies[MAX_ENEMIES];
Bullet bullets[MAX_BULLETS];
Bullet ebullets[MAX_EBULLETS];
Particle parts[MAX_PARTS];
Star stars[MAX_STARS];
UIState ui;

static int running = 1;
static unsigned int rng_state = 0x12345678u;

static unsigned int xorshift32(void) {
    unsigned int x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static float randf(float lo, float hi) {
    float t = (float)(xorshift32() & 0xFFFF) / 65535.0f;
    return lo + t * (hi - lo);
}

static int exit_callback(int arg1, int arg2, void *common) {
    (void)arg1; (void)arg2; (void)common;
    running = 0;
    return 0;
}

static int callback_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void) {
    int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
}

static void load_best_score(void) {
    FILE *f = fopen("nw64_save.dat", "rb");
    if (f) {
        int val = 0;
        if (fread(&val, sizeof(int), 1, f) == 1 && val > 0 && val < 99999999) {
            player.bestScore = val;
        }
        fclose(f);
    }
}

static void save_best_score(void) {
    if (player.score > player.bestScore) {
        player.bestScore = player.score;
        FILE *f = fopen("nw64_save.dat", "wb");
        if (f) {
            fwrite(&player.bestScore, sizeof(int), 1, f);
            fclose(f);
        }
    }
}

static void set_msg(const char *s, int frames) {
    snprintf(ui.msg, sizeof(ui.msg), "%s", s);
    ui.msgTimer = frames;
}

static void spawn_particles(float x, float y, float z, unsigned int color, int count, float speed) {
    int spawned = 0;
    for (int i = 0; i < MAX_PARTS && spawned < count; i++) {
        if (parts[i].life <= 0) {
            parts[i].x = x;
            parts[i].y = y;
            parts[i].z = z;
            parts[i].vx = randf(-speed, speed);
            parts[i].vy = randf(-speed, speed);
            parts[i].vz = randf(-speed * 0.6f, speed * 0.8f);
            parts[i].color = color;
            parts[i].size = randf(0.14f, 0.36f);
            parts[i].life = 18 + (int)(xorshift32() % 18);
            spawned++;
        }
    }
}

static void init_stars(void) {
    for (int i = 0; i < MAX_STARS; i++) {
        stars[i].x = randf(-24.0f, 24.0f);
        stars[i].y = randf(-14.0f, 14.0f);
        stars[i].z = randf(-65.0f, 2.0f);
    }
}

static void clear_entities(void) {
    memset(enemies, 0, sizeof(enemies));
    memset(bullets, 0, sizeof(bullets));
    memset(ebullets, 0, sizeof(ebullets));
    memset(parts, 0, sizeof(parts));
}

static void start_stage(int stg) {
    player.stage = stg;
    player.stageTimer = 0;
    if (stg == 1) set_msg("SECTOR NEON - LISTO!", 120);
    else if (stg == 2) set_msg("CINTURON ASTEROIDES!", 120);
    else set_msg("NUCLEO DREADNOUGHT!", 120);
}

static void start_new_game(void) {
    int best = player.bestScore;
    memset(&player, 0, sizeof(player));
    player.bestScore = best;
    player.hp = 100;
    player.maxHp = 100;
    player.boost = 100;
    player.maxBoost = 100;
    player.bombs = 3;
    player.laserLv = 1;
    for (int i = 0; i < 4; i++) player.lockTargets[i] = -1;

    clear_entities();
    ui.inTitle = 0;
    ui.gameOver = 0;
    ui.victory = 0;
    ui.shakeTimer = 0;
    ui.bombFlash = 0;
    start_stage(1);
    audio_play_sfx(SFX_UI);
}

static int find_free_enemy(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].alive) return i;
    }
    return -1;
}

static int boss_alive(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].alive && enemies[i].type == EN_BOSS) return 1;
    }
    return 0;
}

static void spawn_enemy(int type, float x, float y, float z) {
    int idx = find_free_enemy();
    if (idx < 0) return;
    Enemy *e = &enemies[idx];
    memset(e, 0, sizeof(*e));
    e->alive = 1;
    e->type = type;
    e->x = x;
    e->y = y;
    e->z = z;
    e->rot = randf(0.0f, 6.28f);

    if (type == EN_FIGHTER) {
        e->hp = e->maxHp = 2 + player.stage;
        e->vx = randf(-0.04f, 0.04f);
        e->vy = randf(-0.02f, 0.02f);
        e->vz = 0.24f + 0.04f * player.stage;
        e->fireCd = 40 + (int)(xorshift32() % 45);
    } else if (type == EN_ASTEROID) {
        e->hp = e->maxHp = 4 + player.stage * 2;
        e->vz = 0.28f + 0.03f * player.stage;
    } else if (type == EN_RING) {
        e->hp = e->maxHp = 9999;
        e->vz = 0.26f;
    } else if (type == EN_BOSS) {
        e->hp = e->maxHp = 45 + player.stage * 25;
        e->x = 0.0f;
        e->y = 0.8f;
        e->z = -46.0f;
        e->vx = 0.08f;
        e->vz = 0.14f;
        e->fireCd = 60;
        set_msg("ALERTA: DREADNOUGHT!", 140);
    }
}

static void fire_player_laser(void) {
    int count = (player.laserLv >= 2) ? 2 : 2;
    float offsets[2] = { -0.75f, 0.75f };
    for (int k = 0; k < count; k++) {
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!bullets[i].alive) {
                bullets[i].alive = 1;
                bullets[i].x = player.x + offsets[k];
                bullets[i].y = player.y - 0.1f;
                bullets[i].z = -1.2f;
                bullets[i].vx = player.vx * 0.18f;
                bullets[i].vy = player.vy * 0.18f;
                bullets[i].vz = -1.55f;
                bullets[i].targetIdx = -1;
                break;
            }
        }
    }
    audio_play_sfx(SFX_LASER);
}

static void fire_homing_missiles(void) {
    int fired = 0;
    for (int k = 0; k < player.lockCount; k++) {
        int tidx = player.lockTargets[k];
        if (tidx < 0 || tidx >= MAX_ENEMIES || !enemies[tidx].alive) continue;
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (!bullets[i].alive) {
                bullets[i].alive = 1;
                bullets[i].x = player.x + (k - 1.5f) * 0.45f;
                bullets[i].y = player.y;
                bullets[i].z = -1.0f;
                bullets[i].vx = (k - 1.5f) * 0.12f;
                bullets[i].vy = 0.14f;
                bullets[i].vz = -1.05f;
                bullets[i].targetIdx = tidx;
                fired++;
                break;
            }
        }
    }
    if (fired > 0) audio_play_sfx(SFX_MISSILE);
    player.lockCount = 0;
    for (int i = 0; i < 4; i++) player.lockTargets[i] = -1;
}

static void trigger_smart_bomb(void) {
    if (player.bombs <= 0) return;
    player.bombs--;
    ui.bombFlash = 20;
    ui.shakeTimer = 18;
    audio_play_sfx(SFX_BOMB);
    set_msg("BOMBA NOVA DETONADA!", 90);

    for (int i = 0; i < MAX_EBULLETS; i++) {
        if (ebullets[i].alive) {
            spawn_particles(ebullets[i].x, ebullets[i].y, ebullets[i].z, 0xFF00FFFFu, 3, 0.2f);
            ebullets[i].alive = 0;
        }
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &enemies[i];
        if (!e->alive || e->type == EN_RING) continue;
        e->hp -= 20;
        e->hitFlash = 8;
        spawn_particles(e->x, e->y, e->z, 0xFF55AAFFu, 10, 0.32f);
        if (e->hp <= 0) {
            int wasBoss = (e->type == EN_BOSS);
            e->alive = 0;
            player.score += wasBoss ? 2500 : 150;
            if (wasBoss) {
                if (player.stage < 3) start_stage(player.stage + 1);
                else {
                    ui.victory = 1;
                    save_best_score();
                }
            }
        }
    }
}

static void damage_player(int dmg) {
    if (player.iframes > 0 || player.rollTimer > 0) return;
    player.hp -= dmg;
    player.iframes = 45;
    player.combo = 0;
    ui.shakeTimer = 14;
    audio_play_sfx(SFX_HURT);
    spawn_particles(player.x, player.y, 0.0f, 0xFF3333FFu, 12, 0.26f);
    if (player.hp <= 0) {
        player.hp = 0;
        ui.gameOver = 1;
        save_best_score();
    }
}

static void update_spawner(int boosting) {
    player.stageTimer += boosting ? 2 : 1;
    int t = player.stageTimer;

    if (!boss_alive()) {
        if (t > 1250) {
            spawn_enemy(EN_BOSS, 0.0f, 0.8f, -48.0f);
            return;
        }
        int interval = (player.stage == 1) ? 34 : (player.stage == 2 ? 28 : 22);
        if ((t % interval) == 0) {
            float rx = randf(-10.0f, 10.0f);
            float ry = randf(-4.5f, 4.5f);
            if ((xorshift32() % 7) == 0) {
                spawn_enemy(EN_RING, rx * 0.7f, ry * 0.7f, -52.0f);
            } else if (player.stage >= 2 && (xorshift32() % 3) == 0) {
                spawn_enemy(EN_ASTEROID, rx, ry, -52.0f);
            } else {
                spawn_enemy(EN_FIGHTER, rx, ry, -52.0f);
            }
        }
    }
}

static void update_game(const SceCtrlData *pad, unsigned int pressed) {
    if (ui.msgTimer > 0) ui.msgTimer--;
    if (ui.shakeTimer > 0) ui.shakeTimer--;
    if (ui.bombFlash > 0) ui.bombFlash--;

    if (pressed & PSP_CTRL_SELECT) {
        ui.sfxOn = !ui.sfxOn;
        set_msg(ui.sfxOn ? "AUDIO ON" : "AUDIO OFF", 60);
    }

    if (ui.inTitle || ui.gameOver || ui.victory) {
        for (int i = 0; i < MAX_STARS; i++) {
            stars[i].z += 0.25f;
            if (stars[i].z > 4.0f) {
                stars[i].z = -65.0f;
                stars[i].x = randf(-24.0f, 24.0f);
                stars[i].y = randf(-14.0f, 14.0f);
            }
        }
        if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) {
            start_new_game();
        }
        return;
    }

    /* Flight movement */
    float ax = ((float)pad->Lx - 128.0f) / 128.0f;
    float ay = ((float)pad->Ly - 128.0f) / 128.0f;
    if (fabsf(ax) < 0.18f) ax = 0.0f;
    if (fabsf(ay) < 0.18f) ay = 0.0f;
    if (pad->Buttons & PSP_CTRL_LEFT)  ax = -1.0f;
    if (pad->Buttons & PSP_CTRL_RIGHT) ax =  1.0f;
    if (pad->Buttons & PSP_CTRL_UP)    ay = -1.0f;
    if (pad->Buttons & PSP_CTRL_DOWN)  ay =  1.0f;

    int boosting = ((pad->Buttons & PSP_CTRL_SQUARE) && player.boost > 4);
    if (boosting) player.boost -= 1;
    else if (player.boost < player.maxBoost) player.boost += 1;

    float spd = boosting ? 0.34f : 0.25f;
    player.vx = player.vx * 0.72f + ax * spd * 0.28f;
    player.vy = player.vy * 0.72f - ay * spd * 0.28f;
    player.x += player.vx * 1.4f;
    player.y += player.vy * 1.4f;
    if (player.x < -10.5f) player.x = -10.5f;
    if (player.x >  10.5f) player.x =  10.5f;
    if (player.y < -5.2f)  player.y = -5.2f;
    if (player.y >  5.5f)  player.y =  5.5f;

    /* Barrel Roll */
    if ((pressed & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) && player.rollTimer <= 0) {
        player.rollTimer = 22;
        audio_play_sfx(SFX_ROLL);
    }
    if (player.rollTimer > 0) {
        player.rollTimer--;
        player.roll += 0.29f;
    } else {
        float targetRoll = -ax * 0.65f;
        player.roll = player.roll * 0.82f + targetRoll * 0.18f;
    }
    float targetPitch = -ay * 0.42f;
    player.pitch = player.pitch * 0.82f + targetPitch * 0.18f;

    if (player.iframes > 0) player.iframes--;
    if (player.comboTimer > 0) {
        player.comboTimer--;
        if (player.comboTimer == 0) player.combo = 0;
    }

    /* Weapons */
    if (player.fireCd > 0) player.fireCd--;
    if ((pad->Buttons & PSP_CTRL_CROSS) && player.fireCd <= 0) {
        fire_player_laser();
        player.fireCd = boosting ? 5 : 7;
    }

    if (pad->Buttons & PSP_CTRL_TRIANGLE) {
        player.locking = 1;
        if (player.lockCount < 4) {
            for (int i = 0; i < MAX_ENEMIES; i++) {
                if (!enemies[i].alive || enemies[i].type == EN_RING) continue;
                if (enemies[i].z > -4.0f || enemies[i].z < -48.0f) continue;
                int already = 0;
                for (int k = 0; k < player.lockCount; k++) {
                    if (player.lockTargets[k] == i) { already = 1; break; }
                }
                if (!already) {
                    player.lockTargets[player.lockCount++] = i;
                    audio_play_sfx(SFX_LOCK);
                    break;
                }
            }
        }
    } else if (player.locking) {
        player.locking = 0;
        fire_homing_missiles();
    }

    if (pressed & PSP_CTRL_CIRCLE) {
        trigger_smart_bomb();
    }

    /* Stars */
    float starSpd = boosting ? 0.85f : 0.38f;
    for (int i = 0; i < MAX_STARS; i++) {
        stars[i].z += starSpd;
        if (stars[i].z > 4.0f) {
            stars[i].z = -65.0f;
            stars[i].x = randf(-24.0f, 24.0f);
            stars[i].y = randf(-14.0f, 14.0f);
        }
    }

    update_spawner(boosting);

    /* Player bullets */
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &bullets[i];
        if (!b->alive) continue;
        if (b->targetIdx >= 0 && b->targetIdx < MAX_ENEMIES && enemies[b->targetIdx].alive) {
            Enemy *t = &enemies[b->targetIdx];
            float dx = t->x - b->x;
            float dy = t->y - b->y;
            float dz = t->z - b->z;
            float dist = sqrtf(dx * dx + dy * dy + dz * dz) + 0.001f;
            b->vx = b->vx * 0.78f + (dx / dist) * 1.15f * 0.22f;
            b->vy = b->vy * 0.78f + (dy / dist) * 1.15f * 0.22f;
            b->vz = b->vz * 0.78f + (dz / dist) * 1.15f * 0.22f;
        }
        b->x += b->vx;
        b->y += b->vy;
        b->z += b->vz;
        if (b->z < -60.0f || b->z > 4.0f) {
            b->alive = 0;
            continue;
        }
        for (int j = 0; j < MAX_ENEMIES; j++) {
            Enemy *e = &enemies[j];
            if (!e->alive || e->type == EN_RING) continue;
            float rad = (e->type == EN_BOSS) ? 3.2f : (e->type == EN_ASTEROID ? 1.55f : 1.35f);
            if (fabsf(b->x - e->x) < rad && fabsf(b->y - e->y) < rad && fabsf(b->z - e->z) < rad + 1.1f) {
                int dmg = (b->targetIdx >= 0) ? 4 : player.laserLv;
                b->alive = 0;
                e->hp -= dmg;
                e->hitFlash = 4;
                spawn_particles(b->x, b->y, b->z, 0xFF00FFFFu, 4, 0.16f);
                if (e->hp <= 0) {
                    int wasBoss = (e->type == EN_BOSS);
                    e->alive = 0;
                    player.combo++;
                    player.comboTimer = 150;
                    player.score += (wasBoss ? 2500 : 100) + player.combo * 15;
                    audio_play_sfx(SFX_EXPLODE);
                    spawn_particles(e->x, e->y, e->z, 0xFF33AAFFu, wasBoss ? 28 : 14, wasBoss ? 0.45f : 0.26f);
                    if (wasBoss) {
                        ui.shakeTimer = 24;
                        if (player.stage < 3) start_stage(player.stage + 1);
                        else {
                            ui.victory = 1;
                            save_best_score();
                        }
                    }
                }
                break;
            }
        }
    }

    /* Enemies */
    float zMul = boosting ? 1.45f : 1.0f;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &enemies[i];
        if (!e->alive) continue;
        if (e->hitFlash > 0) e->hitFlash--;
        e->rot += 0.04f;

        if (e->type == EN_BOSS) {
            if (e->z < -22.0f) e->z += e->vz;
            e->x += e->vx;
            if (e->x > 7.5f || e->x < -7.5f) e->vx = -e->vx;
            e->y = 0.8f + sinf(e->rot * 0.6f) * 2.1f;
            e->fireCd--;
            if (e->fireCd <= 0) {
                e->fireCd = 36 - player.stage * 5;
                for (int k = -1; k <= 1; k++) {
                    for (int b = 0; b < MAX_EBULLETS; b++) {
                        if (!ebullets[b].alive) {
                            ebullets[b].alive = 1;
                            ebullets[b].x = e->x + k * 1.4f;
                            ebullets[b].y = e->y;
                            ebullets[b].z = e->z + 1.5f;
                            float dz = -e->z;
                            ebullets[b].vx = (player.x - ebullets[b].x) / (dz * 1.4f);
                            ebullets[b].vy = (player.y - ebullets[b].y) / (dz * 1.4f);
                            ebullets[b].vz = 0.58f;
                            break;
                        }
                    }
                }
            }
        } else {
            e->x += e->vx;
            e->y += e->vy;
            e->z += e->vz * zMul;

            if (e->type == EN_FIGHTER) {
                e->fireCd--;
                if (e->fireCd <= 0 && e->z > -40.0f && e->z < -8.0f) {
                    e->fireCd = 90;
                    for (int b = 0; b < MAX_EBULLETS; b++) {
                        if (!ebullets[b].alive) {
                            ebullets[b].alive = 1;
                            ebullets[b].x = e->x;
                            ebullets[b].y = e->y;
                            ebullets[b].z = e->z + 0.8f;
                            float dz = fabsf(e->z) + 1.0f;
                            ebullets[b].vx = (player.x - e->x) / (dz * 1.5f);
                            ebullets[b].vy = (player.y - e->y) / (dz * 1.5f);
                            ebullets[b].vz = 0.52f;
                            break;
                        }
                    }
                }
            }

            /* Player collision / Ring pickup */
            if (fabsf(e->z) < 1.35f) {
                float hitRad = (e->type == EN_RING) ? 2.1f : 1.45f;
                if (fabsf(e->x - player.x) < hitRad && fabsf(e->y - player.y) < hitRad) {
                    if (e->type == EN_RING) {
                        e->alive = 0;
                        player.hp = (player.hp + 20 > player.maxHp) ? player.maxHp : player.hp + 20;
                        player.boost = player.maxBoost;
                        if (player.laserLv < 3) player.laserLv++;
                        player.score += 300;
                        audio_play_sfx(SFX_RING);
                        set_msg("ANILLO DE ENERGIA! +HP +LASER", 75);
                        spawn_particles(player.x, player.y, 0.0f, 0xFF00DDFFu, 14, 0.25f);
                    } else {
                        e->alive = 0;
                        damage_player(22);
                        spawn_particles(e->x, e->y, e->z, 0xFF4488FFu, 12, 0.25f);
                    }
                }
            }
            if (e->z > 3.5f) e->alive = 0;
        }
    }

    /* Enemy bullets */
    for (int i = 0; i < MAX_EBULLETS; i++) {
        Bullet *b = &ebullets[i];
        if (!b->alive) continue;
        b->x += b->vx;
        b->y += b->vy;
        b->z += b->vz;
        if (b->z > 3.0f || b->z < -60.0f) {
            b->alive = 0;
            continue;
        }
        if (fabsf(b->z) < 1.2f && fabsf(b->x - player.x) < 1.1f && fabsf(b->y - player.y) < 1.0f) {
            if (player.rollTimer > 0) {
                /* Deflect during Barrel Roll! */
                b->vz = -1.2f;
                b->vx = -b->vx;
                b->vy = -b->vy;
                player.score += 50;
                spawn_particles(b->x, b->y, b->z, 0xFFFFFF00u, 6, 0.22f);
            } else {
                b->alive = 0;
                damage_player(14);
            }
        }
    }

    /* Particles */
    for (int i = 0; i < MAX_PARTS; i++) {
        if (parts[i].life > 0) {
            parts[i].x += parts[i].vx;
            parts[i].y += parts[i].vy;
            parts[i].z += parts[i].vz;
            parts[i].life--;
        }
    }
}

int main(void) {
    setup_callbacks();

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    memset(&player, 0, sizeof(player));
    memset(&ui, 0, sizeof(ui));
    ui.inTitle = 1;
    ui.sfxOn = 1;
    for (int i = 0; i < 4; i++) player.lockTargets[i] = -1;
    load_best_score();
    init_stars();

    render3d_init();
    audio_init();

    SceCtrlData pad;
    unsigned int prevButtons = 0;

    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prevButtons);
        prevButtons = pad.Buttons;

        update_game(&pad, pressed);
        render3d_frame();
    }

    save_best_score();
    audio_shutdown();
    render3d_shutdown();
    sceKernelExitGame();
    return 0;
}
