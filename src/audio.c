#include "game.h"
#include <pspaudio.h>
#include <pspthreadman.h>
#include <math.h>

#define SAMPLE_CNT 1024
#define PI2 6.2831853f

static int audio_chan = -1;
static int audio_thid = -1;
static volatile int audio_running = 0;

static volatile float sfx_freq = 0.0f;
static volatile float sfx_dfreq = 0.0f;
static volatile float sfx_env = 0.0f;
static volatile float sfx_decay = 0.998f;
static volatile int sfx_wave = 0;
static float sfx_phase = 0.0f;
static unsigned int noise_rng = 987654321U;

static float bgm_p1 = 0.0f, bgm_p2 = 0.0f;
static int bgm_cnt = 0, bgm_beat = 0;
static float bgm_e1 = 0.0f, bgm_e2 = 0.0f;
static float bgm_f1 = 110.0f, bgm_f2 = 440.0f;

/* High-energy Outrun / Synthwave Space Combat Bass & Lead */
static const float sw_bass[4] = {110.00f, 87.31f, 98.00f, 82.41f}; /* A2, F2, G2, E2 */
static const float sw_lead[16] = {
    440.00f, 523.25f, 659.25f, 523.25f, 440.00f, 523.25f, 659.25f, 880.00f,
    349.23f, 440.00f, 523.25f, 440.00f, 392.00f, 493.88f, 587.33f, 659.25f
};

static float next_noise(void) {
    noise_rng ^= noise_rng << 13;
    noise_rng ^= noise_rng >> 17;
    noise_rng ^= noise_rng << 5;
    return ((float)(noise_rng & 0xFFFF) / 32768.0f) - 1.0f;
}

void audio_play_sfx(int id) {
    if (!ui.sfxOn) return;
    switch (id) {
        case SFX_LASER:   sfx_freq = 1100.0f; sfx_dfreq = -0.065f; sfx_env = 0.28f; sfx_decay = 0.9958f; sfx_wave = 0; break;
        case SFX_LOCK:    sfx_freq = 1480.0f; sfx_dfreq =  0.015f; sfx_env = 0.22f; sfx_decay = 0.9945f; sfx_wave = 1; break;
        case SFX_MISSILE: sfx_freq =  520.0f; sfx_dfreq =  0.030f; sfx_env = 0.30f; sfx_decay = 0.9978f; sfx_wave = 3; break;
        case SFX_ROLL:    sfx_freq =  340.0f; sfx_dfreq =  0.045f; sfx_env = 0.26f; sfx_decay = 0.9970f; sfx_wave = 2; break;
        case SFX_EXPLODE: sfx_freq =  180.0f; sfx_dfreq = -0.010f; sfx_env = 0.50f; sfx_decay = 0.9985f; sfx_wave = 3; break;
        case SFX_BOMB:    sfx_freq =  260.0f; sfx_dfreq = -0.015f; sfx_env = 0.65f; sfx_decay = 0.9992f; sfx_wave = 3; break;
        case SFX_RING:    sfx_freq =  784.0f; sfx_dfreq =  0.040f; sfx_env = 0.25f; sfx_decay = 0.9975f; sfx_wave = 2; break;
        case SFX_HURT:    sfx_freq =  140.0f; sfx_dfreq = -0.008f; sfx_env = 0.42f; sfx_decay = 0.9980f; sfx_wave = 0; break;
        case SFX_UI:      sfx_freq =  660.0f; sfx_dfreq =  0.010f; sfx_env = 0.16f; sfx_decay = 0.9950f; sfx_wave = 1; break;
        default: break;
    }
}

static int audio_thread(SceSize args, void *argp) {
    short buf[SAMPLE_CNT * 2];
    while (audio_running) {
        for (int i = 0; i < SAMPLE_CNT; i++) {
            float sample = 0.0f;
            if (ui.sfxOn) {
                if ((bgm_cnt % 6615) == 0) { /* 16th note at ~133 BPM */
                    int bar = (bgm_beat / 8) % 4;
                    bgm_f1 = sw_bass[bar];
                    bgm_e1 = ((bgm_beat % 2) == 0) ? 0.13f : 0.08f;
                    bgm_f2 = sw_lead[bgm_beat % 16];
                    bgm_e2 = ((bgm_beat % 2) == 0) ? 0.07f : 0.04f;
                    bgm_beat++;
                }
                bgm_cnt++;

                if (bgm_e1 > 0.0005f) {
                    bgm_p1 += bgm_f1 / 44100.0f;
                    if (bgm_p1 >= 1.0f) bgm_p1 -= 1.0f;
                    float saw = 2.0f * bgm_p1 - 1.0f;
                    sample += saw * bgm_e1;
                    bgm_e1 *= 0.9996f;
                }
                if (bgm_e2 > 0.0005f) {
                    bgm_p2 += bgm_f2 / 44100.0f;
                    if (bgm_p2 >= 1.0f) bgm_p2 -= 1.0f;
                    float sq = (bgm_p2 < 0.35f) ? 1.0f : -1.0f;
                    sample += sq * bgm_e2;
                    bgm_e2 *= 0.9995f;
                }
            }

            if (sfx_env > 0.001f) {
                sfx_phase += sfx_freq / 44100.0f;
                if (sfx_phase >= 1.0f) sfx_phase -= 1.0f;
                float w = 0.0f;
                if (sfx_wave == 0) w = (sfx_phase < 0.5f) ? 1.0f : -1.0f;
                else if (sfx_wave == 1) w = 4.0f * fabsf(sfx_phase - 0.5f) - 1.0f;
                else if (sfx_wave == 2) w = sinf(sfx_phase * PI2);
                else w = next_noise() * 0.75f + ((sfx_phase < 0.5f) ? 0.25f : -0.25f);
                sample += w * sfx_env;
                sfx_freq += sfx_dfreq;
                if (sfx_freq < 35.0f) sfx_freq = 35.0f;
                sfx_env *= sfx_decay;
            }

            if (sample > 0.92f) sample = 0.92f;
            if (sample < -0.92f) sample = -0.92f;
            short s16 = (short)(sample * 28000.0f);
            buf[i * 2] = s16;
            buf[i * 2 + 1] = s16;
        }
        sceAudioOutputBlocking(audio_chan, PSP_AUDIO_VOLUME_MAX, buf);
    }
    return 0;
}

void audio_init(void) {
    audio_chan = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, SAMPLE_CNT, PSP_AUDIO_FORMAT_STEREO);
    if (audio_chan >= 0) {
        audio_running = 1;
        audio_thid = sceKernelCreateThread("nw64_audio", audio_thread, 0x12, 0x10000, 0, NULL);
        if (audio_thid >= 0) sceKernelStartThread(audio_thid, 0, NULL);
    }
}

void audio_shutdown(void) {
    audio_running = 0;
    if (audio_thid >= 0) {
        sceKernelWaitThreadEnd(audio_thid, NULL);
        sceKernelDeleteThread(audio_thid);
    }
    if (audio_chan >= 0) sceAudioChRelease(audio_chan);
}
