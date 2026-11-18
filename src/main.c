#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "game.h"

PSP_MODULE_INFO("SkyAce1945", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

Plane player;
EnemyPlane enemies[MAX_ENEMIES];
Bullet bullets[MAX_BULLETS];
Particle parts[MAX_PARTS];
UIState ui;

static int running = 1;
static unsigned int rng_state = 0x24681357u;

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
    FILE *f = fopen("sa_save.dat", "rb");
    if (f) {
        int v = 0;
        if (fread(&v, sizeof(int), 1, f) == 1 && v > 0 && v < 99999999) player.bestScore = v;
        fclose(f);
    }
}

static void save_best(void) {
    if (player.score > player.bestScore) {
        player.bestScore = player.score;
        FILE *f = fopen("sa_save.dat", "wb");
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
            parts[i].vx = randf(-0.2f, 0.2f);
            parts[i].vy = randf(-0.2f, 0.2f);
            parts[i].vz = randf(-0.2f, 0.2f);
            parts[i].color = col;
            parts[i].life = 15 + (int)(xorshift32() % 12);
            n++;
        }
    }
}

static void spawn_wave(int w) {
    player.wave = w;
    int count = 3 + w;
    if (count > MAX_ENEMIES) count = MAX_ENEMIES;
    int isBoss = ((w % 3) == 0);
    for (int i = 0; i < count; i++) {
        EnemyPlane *e = &enemies[i];
        memset(e, 0, sizeof(*e));
        e->alive = 1;
        e->isBoss = (isBoss && i == 0);
        e->x = e->isBoss ? 0.0f : randf(-9.0f, 9.0f);
        e->y = e->isBoss ? 1.5f : randf(-3.5f, 4.5f);
        e->z = -38.0f - i * 4.5f;
        e->vx = e->isBoss ? 0.08f : randf(-0.04f, 0.04f);
        e->vz = e->isBoss ? 0.10f : (0.18f + 0.02f * w);
        e->hp = e->maxHp = e->isBoss ? (22 + w * 4) : (2 + w);
        e->fireCd = 35 + (int)(xorshift32() % 45);
    }
    set_msg(isBoss ? "BOMBARDERO GIGANTE!" : "ESCUADRON ENEMIGO!", 95);
}

static void start_game(void) {
    int best = player.bestScore;
    memset(&player, 0, sizeof(player));
    player.bestScore = best;
    player.hp = player.maxHp = 100;
    memset(enemies, 0, sizeof(enemies));
    memset(bullets, 0, sizeof(bullets));
    memset(parts, 0, sizeof(parts));
    ui.inTitle = 0;
    ui.gameOver = 0;
    spawn_wave(1);
    audio_play_sfx(SFX_UI);
}

static void fire_bullet(float x, float y, float z, float vx, float vy, float vz, int fromPlayer, int isMissile) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].alive) {
            bullets[i].alive = 1;
            bullets[i].x = x; bullets[i].y = y; bullets[i].z = z;
            bullets[i].vx = vx; bullets[i].vy = vy; bullets[i].vz = vz;
            bullets[i].fromPlayer = fromPlayer;
            bullets[i].isMissile = isMissile;
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
        player.z += 0.35f;
        if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_game();
        return;
    }

    float ax = ((float)pad->Lx - 128.0f) / 128.0f;
    float ay = ((float)pad->Ly - 128.0f) / 128.0f;
    if (fabsf(ax) < 0.18f) ax = 0.0f;
    if (fabsf(ay) < 0.18f) ay = 0.0f;
    if (pad->Buttons & PSP_CTRL_LEFT)  ax = -1.0f;
    if (pad->Buttons & PSP_CTRL_RIGHT) ax =  1.0f;
    if (pad->Buttons & PSP_CTRL_UP)    ay = -1.0f;
    if (pad->Buttons & PSP_CTRL_DOWN)  ay =  1.0f;

    int boost = (pad->Buttons & PSP_CTRL_SQUARE) ? 1 : 0;
    player.z += boost ? 0.65f : 0.35f;
    player.x += ax * 0.24f;
    player.y -= ay * 0.20f;
    if (player.x < -10.0f) player.x = -10.0f;
    if (player.x >  10.0f) player.x =  10.0f;
    if (player.y < -3.8f)  player.y = -3.8f;
    if (player.y >  4.8f)  player.y =  4.8f;

    if ((pressed & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) && player.rollTimer <= 0) {
        player.rollTimer = 20;
        audio_play_sfx(SFX_ROLL);
    }
    if (player.rollTimer > 0) {
        player.rollTimer--;
        player.roll += 0.32f;
    } else {
        player.roll = player.roll * 0.8f + (-ax * 0.6f) * 0.2f;
    }
    player.pitch = player.pitch * 0.8f + (-ay * 0.35f) * 0.2f;

    if (player.iframes > 0) player.iframes--;
    if (player.fireCd > 0) player.fireCd--;
    if (player.missileCd > 0) player.missileCd--;

    if ((pad->Buttons & PSP_CTRL_CROSS) && player.fireCd <= 0) {
        player.fireCd = 6;
        fire_bullet(player.x - 0.55f, player.y, -1.0f, 0.0f, 0.0f, -1.4f, 1, 0);
        fire_bullet(player.x + 0.55f, player.y, -1.0f, 0.0f, 0.0f, -1.4f, 1, 0);
        audio_play_sfx(SFX_GUN);
    }
    if ((pressed & PSP_CTRL_TRIANGLE) && player.missileCd <= 0) {
        player.missileCd = 32;
        fire_bullet(player.x, player.y - 0.2f, -1.0f, 0.0f, 0.0f, -1.1f, 1, 1);
        audio_play_sfx(SFX_MISSILE);
    }

    int alive = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        EnemyPlane *e = &enemies[i];
        if (!e->alive) continue;
        alive++;
        if (e->hitFlash > 0) e->hitFlash--;
        if (e->isBoss) {
            if (e->z < -20.0f) e->z += e->vz;
            e->x += e->vx;
            if (e->x > 7.0f || e->x < -7.0f) e->vx = -e->vx;
        } else {
            e->x += e->vx;
            e->z += e->vz * (boost ? 1.4f : 1.0f);
            if (e->z > 3.0f) e->z = -44.0f;
        }
        e->fireCd--;
        if (e->fireCd <= 0 && e->z > -38.0f && e->z < -6.0f) {
            e->fireCd = e->isBoss ? 32 : 80;
            float dz = fabsf(e->z) + 1.0f;
            fire_bullet(e->x, e->y, e->z + 1.0f, (player.x - e->x) / (dz * 1.4f), (player.y - e->y) / (dz * 1.4f), 0.55f, 0, 0);
        }
    }
    if (alive == 0) {
        player.hp = (player.hp + 20 > player.maxHp) ? player.maxHp : player.hp + 20;
        spawn_wave(player.wave + 1);
    }

    for (int i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &bullets[i];
        if (!b->alive) continue;
        b->x += b->vx; b->y += b->vy; b->z += b->vz;
        if (b->z < -55.0f || b->z > 4.0f) { b->alive = 0; continue; }

        if (b->fromPlayer) {
            for (int j = 0; j < MAX_ENEMIES; j++) {
                EnemyPlane *e = &enemies[j];
                if (!e->alive) continue;
                float r = e->isBoss ? 2.6f : 1.3f;
                if (fabsf(b->x - e->x) < r && fabsf(b->y - e->y) < r && fabsf(b->z - e->z) < r + 0.8f) {
                    b->alive = 0;
                    e->hp -= b->isMissile ? 4 : 1;
                    e->hitFlash = 4;
                    spawn_parts(b->x, b->y, b->z, 0xFF00FFFFu, 4);
                    if (e->hp <= 0) {
                        e->alive = 0;
                        player.score += e->isBoss ? 1200 : 120;
                        audio_play_sfx(SFX_EXPLODE);
                        spawn_parts(e->x, e->y, e->z, 0xFF3388FFu, 14);
                    }
                    break;
                }
            }
        } else {
            if (fabsf(b->z) < 1.1f && fabsf(b->x - player.x) < 1.1f && fabsf(b->y - player.y) < 1.0f) {
                b->alive = 0;
                if (player.rollTimer <= 0 && player.iframes <= 0) {
                    player.hp -= 15;
                    player.iframes = 35;
                    ui.shakeTimer = 10;
                    audio_play_sfx(SFX_HURT);
                    spawn_parts(player.x, player.y, 0.0f, 0xFF3333FFu, 8);
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
