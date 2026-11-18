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
static const float bass_seq[8] = { 65.41f, 65.41f, 77.78f, 87.31f, 98.00f, 87.31f, 77.78f, 58.27f };
static const float lead_seq[8] = { 261.63f, 311.13f, 392.00f, 523.25f, 466.16f, 392.00f, 311.13f, 261.63f };

void audio_play_sfx(int id) {
    if (!ui.sfxOn) return;
    sfx_id = id;
    sfx_phase = 0.0f;
    if (id == SFX_NITRO) sfx_timer = 14000;
    else if (id == SFX_DRIFT) sfx_timer = 4000;
    else if (id == SFX_CRASH) sfx_timer = 12000;
    else if (id == SFX_LAP) sfx_timer = 16000;
    else sfx_timer = 5000;
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
                float bass = (2.0f * bt - 1.0f) * 0.14f * expf(-sub * 2.0f);
                float lt = bgm_time * lf - floorf(bgm_time * lf);
                float lead = (lt < 0.35f ? 1.0f : -1.0f) * 0.06f * expf(-sub * 3.0f);

                /* Engine hum proportional to speed */
                float engF = 55.0f + player.speed * 0.45f;
                float et = bgm_time * engF - floorf(bgm_time * engF);
                float eng = (2.0f * et - 1.0f) * (ui.inTitle ? 0.0f : 0.05f);

                mixL += bass + lead * 0.8f + eng;
                mixR += bass + lead * 1.1f + eng;
            }

            if (sfx_timer > 0 && ui.sfxOn) {
                float s = 0.0f;
                float env = (float)sfx_timer / 14000.0f;
                if (env > 1.0f) env = 1.0f;
                if (sfx_id == SFX_NITRO) {
                    sfx_phase += 180.0f / SAMPLE_RATE;
                    s = sinf(sfx_phase * 6.283f) * 0.25f * env;
                } else if (sfx_id == SFX_CRASH) {
                    sfx_phase += 95.0f / SAMPLE_RATE;
                    s = (sinf(sfx_phase * 6.283f) + ((sfx_timer & 1) ? 0.5f : -0.5f)) * 0.3f * env;
                } else if (sfx_id == SFX_LAP) {
                    float f = (sfx_timer > 8000) ? 523.25f : 783.99f;
                    sfx_phase += f / SAMPLE_RATE;
                    s = sinf(sfx_phase * 6.283f) * 0.28f * env;
                } else {
                    sfx_phase += 440.0f / SAMPLE_RATE;
                    s = sinf(sfx_phase * 6.283f) * 0.2f * env;
                }
                mixL += s;
                mixR += s;
                sfx_timer--;
                if (sfx_timer <= 0) sfx_id = SFX_NONE;
            }

            bgm_time += 1.0f / SAMPLE_RATE;
            int vL = (int)(mixL * 32767.0f);
            int vR = (int)(mixR * 32767.0f);
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
    audio_th = sceKernelCreateThread("cd_audio", audio_thread, 0x12, 0x10000, 0, NULL);
    if (audio_th >= 0) sceKernelStartThread(audio_th, 0, NULL);
}

void audio_shutdown(void) {
    audio_run = 0;
    if (audio_th >= 0) sceKernelWaitThreadEnd(audio_th, NULL);
    if (audio_ch >= 0) sceAudioChRelease(audio_ch);
}
