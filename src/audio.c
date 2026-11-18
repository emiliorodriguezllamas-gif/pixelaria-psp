#include <pspaudio.h>
#include <pspkernel.h>
#include <math.h>
#include <string.h>
#include "game.h"

#define SAMPLE_COUNT 1024
#define SAMPLE_RATE 44100

static int audio_ch = -1;
static int audio_th = -1;
static volatile int audio_run = 0;

static short buf[2][SAMPLE_COUNT * 2];
static volatile int sfx_id = SFX_NONE;
static volatile int sfx_timer = 0;
static float sfx_phase = 0.0f;

static float bgm_time = 0.0f;
static const float bass_seq[8] = { 82.41f, 82.41f, 98.00f, 110.00f, 98.00f, 82.41f, 73.42f, 98.00f };
static const float lead_seq[8] = { 329.63f, 392.00f, 440.00f, 523.25f, 493.88f, 440.00f, 392.00f, 329.63f };

void audio_play_sfx(int id) {
    if (!ui.sfxOn) return;
    sfx_id = id;
    sfx_phase = 0.0f;
    if (id == SFX_GUN) sfx_timer = 2200;
    else if (id == SFX_MISSILE) sfx_timer = 6000;
    else if (id == SFX_EXPLODE) sfx_timer = 11000;
    else if (id == SFX_HURT) sfx_timer = 8500;
    else sfx_timer = 4500;
}

static int audio_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int cur = 0;
    while (audio_run) {
        short *out = buf[cur];
        for (int i = 0; i < SAMPLE_COUNT; i++) {
            float mixL = 0.0f, mixR = 0.0f;
            if (ui.sfxOn) {
                int step = (int)(bgm_time * 4.5f);
                float bf = bass_seq[(step / 2) & 7];
                float lf = lead_seq[step & 7];
                float sub = bgm_time * 4.5f - floorf(bgm_time * 4.5f);
                float bt = bgm_time * bf - floorf(bgm_time * bf);
                float bass = (bt < 0.5f ? 1.0f : -1.0f) * 0.12f * expf(-sub * 2.0f);
                float lt = bgm_time * lf - floorf(bgm_time * lf);
                float lead = (lt < 0.35f ? 1.0f : -1.0f) * 0.06f * expf(-sub * 2.8f);
                mixL += bass + lead * 0.85f;
                mixR += bass + lead * 1.15f;
            }
            if (sfx_timer > 0 && ui.sfxOn) {
                float s = 0.0f;
                float env = (float)sfx_timer / 9000.0f;
                if (env > 1.0f) env = 1.0f;
                if (sfx_id == SFX_GUN) {
                    sfx_phase += 240.0f / SAMPLE_RATE;
                    s = (sinf(sfx_phase * 6.283f) + ((sfx_timer & 1) ? 0.3f : -0.3f)) * 0.2f * env;
                } else if (sfx_id == SFX_EXPLODE || sfx_id == SFX_HURT) {
                    sfx_phase += 85.0f / SAMPLE_RATE;
                    s = (sinf(sfx_phase * 6.283f) + ((sfx_timer & 1) ? 0.5f : -0.5f)) * 0.3f * env;
                } else {
                    sfx_phase += 380.0f / SAMPLE_RATE;
                    s = sinf(sfx_phase * 6.283f) * 0.22f * env;
                }
                mixL += s; mixR += s;
                sfx_timer--;
                if (sfx_timer <= 0) sfx_id = SFX_NONE;
            }
            bgm_time += 1.0f / SAMPLE_RATE;
            int vL = (int)(mixL * 32767.0f), vR = (int)(mixR * 32767.0f);
            if (vL > 32767) vL = 32767; else if (vL < -32767) vL = -32767;
            if (vR > 32767) vR = 32767; else if (vR < -32767) vR = -32767;
            out[i * 2] = (short)vL;
            out[i * 2 + 1] = (short)vR;
        }
        sceAudioOutputPannedBlocking(audio_ch, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, out);
        cur ^= 1;
    }
    sceKernelExitDeleteThread(0);
    return 0;
}

void audio_init(void) {
    memset(buf, 0, sizeof(buf));
    audio_ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, SAMPLE_COUNT, PSP_AUDIO_FORMAT_STEREO);
    if (audio_ch < 0) return;
    audio_run = 1;
    audio_th = sceKernelCreateThread("sa_audio", audio_thread, 0x12, 0x10000, 0, NULL);
    if (audio_th >= 0) sceKernelStartThread(audio_th, 0, NULL);
}

void audio_shutdown(void) {
    audio_run = 0;
    if (audio_th >= 0) sceKernelWaitThreadEnd(audio_th, NULL);
    if (audio_ch >= 0) sceAudioChRelease(audio_ch);
}
