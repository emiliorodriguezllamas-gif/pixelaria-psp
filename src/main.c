#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "game.h"

PSP_MODULE_INFO("TunnelRushX", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

Runner player;
Obstacle obs[MAX_OBS];
UIState ui;

static int running = 1;
static unsigned int rng_state = 0x87654321u;

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
    FILE *f = fopen("tr_save.dat", "rb");
    if (f) {
        int v = 0;
        if (fread(&v, sizeof(int), 1, f) == 1 && v > 0 && v < 99999999) player.bestScore = v;
        fclose(f);
    }
}

static void save_best(void) {
    if (player.score > player.bestScore) {
        player.bestScore = player.score;
        FILE *f = fopen("tr_save.dat", "wb");
        if (f) { fwrite(&player.bestScore, sizeof(int), 1, f); fclose(f); }
    }
}

static void start_run(void) {
    int best = player.bestScore;
    memset(&player, 0, sizeof(player));
    player.bestScore = best;
    player.speed = 0.45f;
    unsigned int cols[4] = { 0xFF0055FFu, 0xFFFF33AAu, 0xFF00EEFFu, 0xFFFFAA00u };
    for (int i = 0; i < MAX_OBS; i++) {
        obs[i].active = 1;
        obs[i].passed = 0;
        obs[i].z = 24.0f + i * 11.0f;
        obs[i].angle = randf(0.0f, 6.283f);
        obs[i].arc = 0.65f;
        obs[i].color = cols[i & 3];
    }
    ui.inTitle = 0;
    ui.gameOver = 0;
    audio_play_sfx(SFX_UI);
}

static float angle_diff(float a, float b) {
    float d = fmodf(a - b + 3.14159f, 6.28318f);
    if (d < 0.0f) d += 6.28318f;
    return fabsf(d - 3.14159f);
}

static void update_game(const SceCtrlData *pad, unsigned int pressed) {
    if (ui.msgTimer > 0) ui.msgTimer--;
    if (ui.shakeTimer > 0) ui.shakeTimer--;
    if (pressed & PSP_CTRL_SELECT) ui.sfxOn = !ui.sfxOn;

    if (ui.inTitle || ui.gameOver) {
        player.z += 0.35f;
        player.angle += 0.012f;
        if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_run();
        return;
    }

    float ax = ((float)pad->Lx - 128.0f) / 128.0f;
    if (fabsf(ax) < 0.16f) ax = 0.0f;
    if (pad->Buttons & (PSP_CTRL_LEFT | PSP_CTRL_LTRIGGER))  ax = -1.0f;
    if (pad->Buttons & (PSP_CTRL_RIGHT | PSP_CTRL_RTRIGGER)) ax =  1.0f;

    player.angle += ax * 0.085f;
    int turbo = (pad->Buttons & PSP_CTRL_CROSS) ? 1 : 0;
    player.speed += 0.00025f;
    if (player.speed > 1.25f) player.speed = 1.25f;

    float dz = player.speed * (turbo ? 1.45f : 1.0f);
    player.z += dz;
    player.score += turbo ? 3 : 1;

    for (int i = 0; i < MAX_OBS; i++) {
        if (!obs[i].active) continue;
        obs[i].angle += ((i & 1) ? 0.015f : -0.015f);
        float d = obs[i].z - player.z;
        if (d < 0.8f && d > -0.8f) {
            if (angle_diff(player.angle, obs[i].angle) < obs[i].arc) {
                ui.gameOver = 1;
                ui.shakeTimer = 18;
                save_best();
                audio_play_sfx(SFX_CRASH);
                return;
            }
        }
        if (d < -2.0f) {
            obs[i].z += MAX_OBS * 11.0f;
            obs[i].angle = randf(0.0f, 6.283f);
            player.score += 50;
            audio_play_sfx(SFX_PASS);
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
