#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "game.h"

PSP_MODULE_INFO("PolyKart64", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

Kart player;
RivalKart rivals[MAX_RIVALS];
ItemBox boxes[MAX_BOXES];
UIState ui;

static int running = 1;
static unsigned int rng_state = 0x13572468u;

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
    FILE *f = fopen("pk_save.dat", "rb");
    if (f) {
        int v = 0;
        if (fread(&v, sizeof(int), 1, f) == 1 && v > 0 && v < 99999999) player.bestScore = v;
        fclose(f);
    }
}

static void save_best(void) {
    if (player.score > player.bestScore) {
        player.bestScore = player.score;
        FILE *f = fopen("pk_save.dat", "wb");
        if (f) { fwrite(&player.bestScore, sizeof(int), 1, f); fclose(f); }
    }
}

static void set_msg(const char *s, int frames) {
    snprintf(ui.msg, sizeof(ui.msg), "%s", s);
    ui.msgTimer = frames;
}

static void start_race(void) {
    int best = player.bestScore;
    memset(&player, 0, sizeof(player));
    player.bestScore = best;
    player.lap = 1;

    unsigned int rcols[3] = { 0xFFFF7722u, 0xFF33DD44u, 0xFFEE44CCu };
    for (int i = 0; i < MAX_RIVALS; i++) {
        rivals[i].x = -0.6f + i * 0.6f;
        rivals[i].z = 12.0f + i * 8.0f;
        rivals[i].speed = 155.0f + i * 12.0f;
        rivals[i].color = rcols[i];
        rivals[i].spinTimer = 0;
    }
    for (int i = 0; i < MAX_BOXES; i++) {
        boxes[i].active = 1;
        boxes[i].x = randf(-0.75f, 0.75f);
        boxes[i].z = 30.0f + i * 28.0f;
    }
    ui.inTitle = 0;
    ui.finished = 0;
    set_msg("VUELTA 1 - GO!", 90);
    audio_play_sfx(SFX_UI);
}

static void update_game(const SceCtrlData *pad, unsigned int pressed) {
    if (ui.msgTimer > 0) ui.msgTimer--;
    if (ui.shakeTimer > 0) ui.shakeTimer--;
    if (pressed & PSP_CTRL_SELECT) {
        ui.sfxOn = !ui.sfxOn;
        set_msg(ui.sfxOn ? "AUDIO ON" : "AUDIO OFF", 60);
    }

    if (ui.inTitle || ui.finished) {
        player.z += 0.3f;
        if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_race();
        return;
    }

    float ax = ((float)pad->Lx - 128.0f) / 128.0f;
    if (fabsf(ax) < 0.16f) ax = 0.0f;
    if (pad->Buttons & PSP_CTRL_LEFT)  ax = -1.0f;
    if (pad->Buttons & PSP_CTRL_RIGHT) ax =  1.0f;

    int accel = (pad->Buttons & PSP_CTRL_CROSS) ? 1 : 0;
    int brake = (pad->Buttons & PSP_CTRL_CIRCLE) ? 1 : 0;
    int drift = (pad->Buttons & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) ? 1 : 0;

    if ((pressed & PSP_CTRL_SQUARE) && player.item != ITEM_NONE) {
        if (player.item == ITEM_TURBO) {
            player.turboTimer = 90;
            audio_play_sfx(SFX_TURBO);
            set_msg("TURBO ACTIVADO!", 60);
        } else if (player.item == ITEM_SHIELD) {
            player.shieldTimer = 180;
            audio_play_sfx(SFX_ITEM);
            set_msg("ESCUDO ACTIVADO!", 60);
        } else if (player.item == ITEM_ROCKET) {
            int bestR = -1;
            float bestD = 9999.0f;
            for (int i = 0; i < MAX_RIVALS; i++) {
                float d = rivals[i].z - player.z;
                if (d > 0.0f && d < bestD) { bestD = d; bestR = i; }
            }
            if (bestR >= 0) {
                rivals[bestR].spinTimer = 70;
                player.score += 250;
                audio_play_sfx(SFX_HIT);
                set_msg("IMPACTO DE COHETE!", 65);
            }
        }
        player.item = ITEM_NONE;
    }

    if (player.turboTimer > 0) player.turboTimer--;
    if (player.shieldTimer > 0) player.shieldTimer--;

    float maxSpd = (player.turboTimer > 0) ? 265.0f : 195.0f;
    if (accel) player.speed += (player.turboTimer > 0) ? 3.2f : 1.7f;
    else if (brake) player.speed -= 2.8f;
    else player.speed -= 0.7f;
    if (player.speed < 0.0f) player.speed = 0.0f;
    if (player.speed > maxSpd) player.speed = maxSpd;

    player.x += ax * (drift ? 0.040f : 0.026f) * (player.speed / 160.0f);
    if (player.x < -1.0f) { player.x = -1.0f; player.speed *= 0.96f; }
    if (player.x >  1.0f) { player.x =  1.0f; player.speed *= 0.96f; }
    player.steer = -ax * (drift ? 0.45f : 0.25f);

    float dz = player.speed * 0.0045f;
    player.z += dz;
    player.score += (int)(dz * 3.0f);

    int newLap = 1 + (int)(player.z / 280.0f);
    if (newLap > player.lap) {
        player.lap = newLap;
        if (player.lap > 3) {
            ui.finished = 1;
            save_best();
            audio_play_sfx(SFX_LAP);
            return;
        }
        audio_play_sfx(SFX_LAP);
        set_msg(player.lap == 2 ? "VUELTA 2!" : "ULTIMA VUELTA!", 90);
    }

    for (int i = 0; i < MAX_BOXES; i++) {
        if (boxes[i].z < player.z - 5.0f) {
            boxes[i].z = player.z + randf(45.0f, 95.0f);
            boxes[i].x = randf(-0.75f, 0.75f);
            boxes[i].active = 1;
        }
        if (boxes[i].active && fabsf(boxes[i].z - player.z) < 2.2f && fabsf(boxes[i].x - player.x) < 0.35f) {
            boxes[i].active = 0;
            player.item = 1 + (int)(xorshift32() % 3);
            player.score += 100;
            audio_play_sfx(SFX_ITEM);
            set_msg("OBJETO RECOGIDO!", 55);
        }
    }

    for (int i = 0; i < MAX_RIVALS; i++) {
        if (rivals[i].spinTimer > 0) {
            rivals[i].spinTimer--;
            rivals[i].z += rivals[i].speed * 0.0015f;
        } else {
            rivals[i].z += rivals[i].speed * 0.0040f;
        }
        if (fabsf(rivals[i].z - player.z) < 2.0f && fabsf(rivals[i].x - player.x) < 0.32f) {
            if (player.shieldTimer > 0) {
                rivals[i].spinTimer = 50;
                player.score += 150;
                audio_play_sfx(SFX_HIT);
            } else {
                player.speed *= 0.65f;
                ui.shakeTimer = 10;
                audio_play_sfx(SFX_HIT);
            }
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
