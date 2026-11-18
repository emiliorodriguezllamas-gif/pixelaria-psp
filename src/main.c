#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "game.h"

PSP_MODULE_INFO("CyberDriftPSP", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

Car player;
TrafficCar traffic[MAX_TRAFFIC];
Spark sparks[MAX_SPARKS];
UIState ui;

static int running = 1;
static unsigned int rng_state = 0xCAFEBABEu;

static unsigned int xorshift32(void) {
    unsigned int x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
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
    FILE *f = fopen("cd_save.dat", "rb");
    if (f) {
        int v = 0;
        if (fread(&v, sizeof(int), 1, f) == 1 && v > 0 && v < 99999999) player.bestScore = v;
        fclose(f);
    }
}

static void save_best(void) {
    if (player.score > player.bestScore) {
        player.bestScore = player.score;
        FILE *f = fopen("cd_save.dat", "wb");
        if (f) { fwrite(&player.bestScore, sizeof(int), 1, f); fclose(f); }
    }
}

static void set_msg(const char *s, int frames) {
    snprintf(ui.msg, sizeof(ui.msg), "%s", s);
    ui.msgTimer = frames;
}

static void spawn_sparks(float x, float y, float z, unsigned int col, int cnt) {
    int n = 0;
    for (int i = 0; i < MAX_SPARKS && n < cnt; i++) {
        if (sparks[i].life <= 0) {
            sparks[i].x = x; sparks[i].y = y; sparks[i].z = z;
            sparks[i].vx = randf(-0.18f, 0.18f);
            sparks[i].vy = randf(0.05f, 0.22f);
            sparks[i].vz = randf(-0.1f, 0.2f);
            sparks[i].color = col;
            sparks[i].life = 14 + (int)(xorshift32() % 12);
            n++;
        }
    }
}

static void start_race(void) {
    int best = player.bestScore;
    memset(&player, 0, sizeof(player));
    player.bestScore = best;
    player.nitro = 100.0f;
    player.lap = 1;
    memset(traffic, 0, sizeof(traffic));
    memset(sparks, 0, sizeof(sparks));
    for (int i = 0; i < 10; i++) {
        traffic[i].alive = 1;
        traffic[i].x = randf(-0.85f, 0.85f);
        traffic[i].z = 25.0f + i * 24.0f;
        traffic[i].speed = randf(95.0f, 155.0f);
        traffic[i].color = (i & 1) ? 0xFF3355FFu : 0xFF00CCFFu;
    }
    ui.inTitle = 0;
    ui.finished = 0;
    set_msg("VUELTA 1 - A TOPE!", 100);
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
        player.z += 0.35f;
        if (pressed & (PSP_CTRL_START | PSP_CTRL_CROSS)) start_race();
        return;
    }

    float ax = ((float)pad->Lx - 128.0f) / 128.0f;
    if (fabsf(ax) < 0.16f) ax = 0.0f;
    if (pad->Buttons & PSP_CTRL_LEFT)  ax = -1.0f;
    if (pad->Buttons & PSP_CTRL_RIGHT) ax =  1.0f;

    int accel = (pad->Buttons & PSP_CTRL_CROSS) ? 1 : 0;
    int brake = (pad->Buttons & PSP_CTRL_CIRCLE) ? 1 : 0;
    int drifting = (pad->Buttons & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER)) ? 1 : 0;

    player.nitroActive = ((pad->Buttons & PSP_CTRL_SQUARE) && player.nitro > 1.0f && accel);
    if (player.nitroActive) {
        player.nitro -= 0.7f;
        if (player.nitro < 0.0f) player.nitro = 0.0f;
    } else if (player.nitro < 100.0f) {
        player.nitro += drifting ? 0.35f : 0.10f;
    }

    float maxSpd = player.nitroActive ? 310.0f : 235.0f;
    if (accel) player.speed += player.nitroActive ? 2.8f : 1.6f;
    else if (brake) player.speed -= 3.2f;
    else player.speed -= 0.8f;

    if (player.speed < 0.0f) player.speed = 0.0f;
    if (player.speed > maxSpd) player.speed = maxSpd;

    float steerMul = drifting ? 0.042f : 0.026f;
    player.x += ax * steerMul * (player.speed / 180.0f);
    if (player.x < -1.05f) { player.x = -1.05f; player.speed *= 0.96f; }
    if (player.x >  1.05f) { player.x =  1.05f; player.speed *= 0.96f; }
    player.steer = -ax * (drifting ? 0.48f : 0.25f);

    if (drifting && fabsf(ax) > 0.3f && player.speed > 100.0f) {
        player.score += 3;
        spawn_sparks(player.x * 4.5f, 0.1f, -0.4f, 0xFF00EEFFu, 2);
    }

    float dz = player.speed * 0.0045f;
    player.z += dz;
    player.score += (int)(dz * 4.0f);
    player.timeFrames++;

    int newLap = 1 + (int)(player.z / 320.0f);
    if (newLap > player.lap) {
        player.lap = newLap;
        if (player.lap > 3) {
            ui.finished = 1;
            save_best();
            audio_play_sfx(SFX_LAP);
            return;
        }
        audio_play_sfx(SFX_LAP);
        set_msg(player.lap == 2 ? "VUELTA 2!" : "ULTIMA VUELTA!", 100);
    }

    for (int i = 0; i < MAX_TRAFFIC; i++) {
        if (!traffic[i].alive) continue;
        traffic[i].z += traffic[i].speed * 0.0032f;
        if (traffic[i].z < player.z - 6.0f) {
            traffic[i].z = player.z + randf(55.0f, 110.0f);
            traffic[i].x = randf(-0.85f, 0.85f);
        }
        if (fabsf(traffic[i].z - player.z) < 2.2f && fabsf(traffic[i].x - player.x) < 0.32f) {
            player.speed *= 0.45f;
            traffic[i].z += 12.0f;
            ui.shakeTimer = 14;
            audio_play_sfx(SFX_CRASH);
            spawn_sparks(player.x * 4.5f, 0.3f, -1.5f, 0xFF3355FFu, 10);
            set_msg("COLISION!", 45);
        }
    }

    for (int i = 0; i < MAX_SPARKS; i++) {
        if (sparks[i].life > 0) {
            sparks[i].x += sparks[i].vx;
            sparks[i].y += sparks[i].vy;
            sparks[i].z += sparks[i].vz;
            sparks[i].life--;
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
