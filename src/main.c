#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "game.h"

PSP_MODULE_INFO("MechArenaZero", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

Mech player;
EnemyMech enemies[MAX_ENEMIES];
Bullet bullets[MAX_BULLETS];
Particle parts[MAX_PARTS];
UIState ui;

static int running = 1;
static unsigned int rng_state = 0xDEADBEEFu;

static unsigned int xorshift32(void) {
    unsigned int x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng_state = x;
    return x;
}

static float randf(float lo, float hi) {
    return lo + ((float)(xorshift32() & 0xFFFF) / 65535.0f) * (hi - lo);
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

static void load_best(void) {
    FILE *f = fopen("ma_save.dat", "rb");
    if (f) {
        int v = 0;
        if (fread(&v, sizeof(int), 1, f) == 1 && v > 0 && v < 99999999) player.bestScore = v;
        fclose(f);
    }
}

static void save_best(void) {
    if (player.score > player.bestScore) {
        player.bestScore = player.score;
        FILE *f = fopen("ma_save.dat", "wb");
        if (f) { fwrite(&player.bestScore, sizeof(int), 1, f); fclose(f); }
    }
}

static void set_msg(const char *s, int frames) {
    snprintf(ui.msg, sizeof(ui.msg), "%s", s);
    ui.msgTimer = frames;
}

static void spawn_parts(float x, float y, float z, unsigned int col, int cnt) {
    int n = 0;
    for (int i = 0; i < MAX_PARTS && n < cnt; i++) {
        if (parts[i].life <= 0) {
            parts[i].x = x; parts[i].y = y; parts[i].z = z;
            parts[i].vx = randf(-0.22f, 0.22f);
            parts[i].vy = randf(0.05f, 0.26f);
            parts[i].vz = randf(-0.22f, 0.22f);
            parts[i].color = col;
            parts[i].life = 16 + (int)(xorshift32() % 14);
            n++;
        }
    }
}

static void spawn_wave(int w) {
    player.wave = w;
    int count = 2 + w;
    if (count > MAX_ENEMIES) count = MAX_ENEMIES;
    int isBossWave = ((w % 3) == 0);
    for (int i = 0; i < count; i++) {
        EnemyMech *e = &enemies[i];
        memset(e, 0, sizeof(*e));
        e->alive = 1;
        float ang = randf(0.0f, 6.283f);
        float dist = randf(14.0f, 23.0f);
        e->x = cosf(ang) * dist;
        e->z = sinf(ang) * dist;
        e->isBoss = (isBossWave && i == 0);
        e->hp = e->maxHp = e->isBoss ? (24 + w * 4) : (5 + w * 2);
        e->fireCd = 45 + (int)(xorshift32() % 50);
    }
    set_msg(isBossWave ? "OLEADA BOSS MECHA!" : "NUEVA OLEADA!", 100);
}

static void start_game(void) {
    int best = player.bestScore;
    memset(&player, 0, sizeof(player));
    player.bestScore = best;
    player.hp = player.maxHp = 100;
    player.energy = player.maxEnergy = 100;
    memset(enemies, 0, sizeof(enemies));
    memset(bullets, 0, sizeof(bullets));
    memset(parts, 0, sizeof(parts));
    ui.inTitle = 0;
    ui.gameOver = 0;
    spawn_wave(1);
    audio_play_sfx(SFX_UI);
}

static void fire_bullet(float x, float y, float z, float vx, float vz, int fromPlayer, int homing, int targetIdx) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].alive) {
            bullets[i].alive = 1;
            bullets[i].x = x; bullets[i].y = y; bullets[i].z = z;
            bullets[i].vx = vx; bullets[i].vy = 0.0f; bullets[i].vz = vz;
            bullets[i].fromPlayer = fromPlayer;
            bullets[i].homing = homing;
            bullets[i].targetIdx = targetIdx;
            bullets[i].life = 110;
            break;
        }
    }
}

static void update_game(const SceCtrlData *pad, unsigned int pressed) {
    if (ui.msgTimer > 0) ui.msgTimer--;
    if (ui.shakeTimer > 0) ui.shakeTimer--;
    if (pressed & PSP_CTRL_SELECT) {
        ui.sfxOn = !ui.sfxOn;
        set_msg(ui.sfxOn ? "AUDIO ON" : "AUDIO OFF", 60);
    }

    if (ui.inTitle || ui.gameOver) {
        player.yaw += 0.01f;
        if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        return;
    }

    if (pad->Buttons & PSP_CTRL_LTRIGGER) player.yaw += 0.055f;
    if (pad->Buttons & PSP_CTRL_RTRIGGER) player.yaw -= 0.055f;

    float ax = ((float)pad->Lx - 128.0f) / 128.0f;
    float ay = ((float)pad->Ly - 128.0f) / 128.0f;
    if (fabsf(ax) < 0.18f) ax = 0.0f;
    if (fabsf(ay) < 0.18f) ay = 0.0f;
    if (pad->Buttons & PSP_CTRL_LEFT)  ax = -1.0f;
    if (pad->Buttons & PSP_CTRL_RIGHT) ax =  1.0f;
    if (pad->Buttons & PSP_CTRL_UP)    ay = -1.0f;
    if (pad->Buttons & PSP_CTRL_DOWN)  ay =  1.0f;

    if ((pressed & PSP_CTRL_SQUARE) && player.energy >= 25 && player.dashTimer <= 0) {
        player.energy -= 25;
        player.dashTimer = 14;
        audio_play_sfx(SFX_DASH);
    }
    if (player.dashTimer > 0) player.dashTimer--;

    player.shieldOn = ((pad->Buttons & PSP_CTRL_CIRCLE) && player.energy > 2);
    if (player.shieldOn) player.energy -= 1;
    else if (player.energy < player.maxEnergy) player.energy += 1;

    float spd = (player.dashTimer > 0) ? 0.58f : 0.24f;
    float fwdX = -sinf(player.yaw), fwdZ = -cosf(player.yaw);
    float rightX = cosf(player.yaw), rightZ = -sinf(player.yaw);
    player.x += (fwdX * (-ay) + rightX * ax) * spd;
    player.z += (fwdZ * (-ay) + rightZ * ax) * spd;
    if (player.x < -25.5f) player.x = -25.5f;
    if (player.x >  25.5f) player.x =  25.5f;
    if (player.z < -25.5f) player.z = -25.5f;
    if (player.z >  25.5f) player.z =  25.5f;

    if (player.iframes > 0) player.iframes--;
    if (player.fireCd > 0) player.fireCd--;
    if (player.missileCd > 0) player.missileCd--;

    if ((pad->Buttons & PSP_CTRL_CROSS) && player.fireCd <= 0) {
        player.fireCd = 9;
        fire_bullet(player.x + rightX * 0.9f, 1.55f, player.z + rightZ * 0.9f, fwdX * 0.85f, fwdZ * 0.85f, 1, 0, -1);
        fire_bullet(player.x - rightX * 0.9f, 1.55f, player.z - rightZ * 0.9f, fwdX * 0.85f, fwdZ * 0.85f, 1, 0, -1);
        audio_play_sfx(SFX_LASER);
    }

    if ((pressed & PSP_CTRL_TRIANGLE) && player.missileCd <= 0 && player.energy >= 20) {
        int tgt = -1;
        float bestD = 99999.0f;
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (!enemies[i].alive) continue;
            float dx = enemies[i].x - player.x, dz = enemies[i].z - player.z;
            float d = dx * dx + dz * dz;
            if (d < bestD) { bestD = d; tgt = i; }
        }
        if (tgt >= 0) {
            player.energy -= 20;
            player.missileCd = 35;
            fire_bullet(player.x, 2.2f, player.z, fwdX * 0.6f, fwdZ * 0.6f, 1, 1, tgt);
            audio_play_sfx(SFX_MISSILE);
        }
    }

    /* Update Enemies */
    int aliveCount = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        EnemyMech *e = &enemies[i];
        if (!e->alive) continue;
        aliveCount++;
        if (e->hitFlash > 0) e->hitFlash--;
        float dx = player.x - e->x, dz = player.z - e->z;
        float dist = sqrtf(dx * dx + dz * dz) + 0.001f;
        e->yaw = atan2f(-dx, -dz);
        if (dist > 7.5f) {
            float es = e->isBoss ? 0.09f : 0.12f;
            e->x += (dx / dist) * es;
            e->z += (dz / dist) * es;
        }
        e->fireCd--;
        if (e->fireCd <= 0) {
            e->fireCd = e->isBoss ? 36 : 75;
            fire_bullet(e->x, 1.5f, e->z, (dx / dist) * 0.48f, (dz / dist) * 0.48f, 0, 0, -1);
        }
    }
    if (aliveCount == 0) {
        player.hp = (player.hp + 25 > player.maxHp) ? player.maxHp : player.hp + 25;
        spawn_wave(player.wave + 1);
    }

    /* Update Bullets */
    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &bullets[i];
        if (!b->alive) continue;
        if (b->homing && b->targetIdx >= 0 && enemies[b->targetIdx].alive) {
            EnemyMech *t = &enemies[b->targetIdx];
            float dx = t->x - b->x, dz = t->z - b->z;
            float d = sqrtf(dx * dx + dz * dz) + 0.001f;
            b->vx = b->vx * 0.8f + (dx / d) * 0.75f * 0.2f;
            b->vz = b->vz * 0.8f + (dz / d) * 0.75f * 0.2f;
        }
        b->x += b->vx; b->z += b->vz;
        b->life--;
        if (b->life <= 0 || fabsf(b->x) > 28.0f || fabsf(b->z) > 28.0f) {
            b->alive = 0;
            continue;
        }
        if (b->fromPlayer) {
            for (int j = 0; j < MAX_ENEMIES; j++) {
                EnemyMech *e = &enemies[j];
                if (!e->alive) continue;
                float r = e->isBoss ? 2.0f : 1.3f;
                if (fabsf(b->x - e->x) < r && fabsf(b->z - e->z) < r) {
                    b->alive = 0;
                    e->hp -= b->homing ? 4 : 1;
                    e->hitFlash = 4;
                    spawn_parts(b->x, 1.5f, b->z, 0xFF00FFFFu, 4);
                    if (e->hp <= 0) {
                        e->alive = 0;
                        player.score += e->isBoss ? 1000 : 150;
                        audio_play_sfx(SFX_EXPLODE);
                        spawn_parts(e->x, 1.5f, e->z, 0xFF3388FFu, 16);
                    }
                    break;
                }
            }
        } else {
            if (fabsf(b->x - player.x) < 1.2f && fabsf(b->z - player.z) < 1.2f) {
                b->alive = 0;
                if (player.shieldOn) {
                    spawn_parts(b->x, 1.5f, b->z, 0xFFFFFF00u, 5);
                } else if (player.iframes <= 0) {
                    player.hp -= 12;
                    player.iframes = 28;
                    ui.shakeTimer = 10;
                    audio_play_sfx(SFX_HURT);
                    spawn_parts(player.x, 1.5f, player.z, 0xFF3333FFu, 8);
                    if (player.hp <= 0) {
                        player.hp = 0;
                        ui.gameOver = 1;
                        save_best();
                    }
                }
            }
        }
    }

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
    load_best();

    render3d_init();
    audio_init();

    SceCtrlData pad;
    unsigned int prev = 0;
    while (running) {
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & (~prev);
        prev = pad.Buttons;
        update_game(&pad, pressed);
        render3d_frame();
    }

    save_best();
    audio_shutdown();
    render3d_shutdown();
    sceKernelExitGame();
    return 0;
}
