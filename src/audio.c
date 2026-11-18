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
static volatile int sfx_wave = 0; /* 0=square/noise, 1=triangle, 2=sine, 3=noise */
static float sfx_phase = 0.0f;
static unsigned int noise_rng = 2463534242U;

static float bgm_p1 = 0.0f, bgm_p2 = 0.0f;
static int bgm_cnt = 0, bgm_beat = 0;
static float bgm_e1 = 0.0f, bgm_e2 = 0.0f;
static float bgm_f1 = 55.0f, bgm_f2 = 220.0f;

/* Resident Evil 4 style Spanish Phrygian / Dread Minor progression (E2, F2, G2, F2) */
static const float dread_bass[4] = {82.41f, 87.31f, 98.00f, 87.31f};
static const float dread_pluck[8] = {164.81f, 174.61f, 207.65f, 246.94f, 261.63f, 246.94f, 207.65f, 174.61f};

static float next_noise(void) {
    noise_rng ^= noise_rng << 13;
    noise_rng ^= noise_rng >> 17;
    noise_rng ^= noise_rng << 5;
    return ((float)(noise_rng & 0xFFFF) / 32768.0f) - 1.0f;
}

void audio_play_sfx(int id) {
    if (!ui.sfxOn) return;
    switch (id) {
        case SFX_PISTOL:   sfx_freq = 420.0f; sfx_dfreq = -0.045f; sfx_env = 0.42f; sfx_decay = 0.9965f; sfx_wave = 3; break;
        case SFX_SHOTGUN:  sfx_freq = 220.0f; sfx_dfreq = -0.025f; sfx_env = 0.55f; sfx_decay = 0.9982f; sfx_wave = 3; break;
        case SFX_RIFLE:    sfx_freq = 680.0f; sfx_dfreq = -0.050f; sfx_env = 0.48f; sfx_decay = 0.9978f; sfx_wave = 3; break;
        case SFX_KNIFE:    sfx_freq = 900.0f; sfx_dfreq = -0.080f; sfx_env = 0.25f; sfx_decay = 0.9950f; sfx_wave = 3; break;
        case SFX_HEADSHOT: sfx_freq = 780.0f; sfx_dfreq = -0.040f; sfx_env = 0.45f; sfx_decay = 0.9975f; sfx_wave = 0; break;
        case SFX_KICK:     sfx_freq = 160.0f; sfx_dfreq = -0.015f; sfx_env = 0.50f; sfx_decay = 0.9978f; sfx_wave = 1; break;
        case SFX_CHAINSAW: sfx_freq = 115.0f; sfx_dfreq =  0.002f; sfx_env = 0.32f; sfx_decay = 0.9988f; sfx_wave = 0; break;
        case SFX_CRATE:    sfx_freq = 280.0f; sfx_dfreq = -0.022f; sfx_env = 0.35f; sfx_decay = 0.9972f; sfx_wave = 3; break;
        case SFX_PICKUP:   sfx_freq = 587.0f; sfx_dfreq =  0.012f; sfx_env = 0.22f; sfx_decay = 0.9980f; sfx_wave = 2; break;
        case SFX_RELOAD:   sfx_freq = 820.0f; sfx_dfreq =  0.020f; sfx_env = 0.22f; sfx_decay = 0.9955f; sfx_wave = 0; break;
        case SFX_HURT:     sfx_freq = 150.0f; sfx_dfreq = -0.008f; sfx_env = 0.40f; sfx_decay = 0.9982f; sfx_wave = 0; break;
        case SFX_MERCHANT: sfx_freq = 330.0f; sfx_dfreq =  0.004f; sfx_env = 0.25f; sfx_decay = 0.9988f; sfx_wave = 1; break;
        case SFX_UI:       sfx_freq = 640.0f; sfx_dfreq =  0.005f; sfx_env = 0.14f; sfx_decay = 0.9950f; sfx_wave = 1; break;
        default: break;
    }
}

static int audio_thread(SceSize args, void *argp) {
    short buf[SAMPLE_CNT * 2];
    while (audio_running) {
        for (int i = 0; i < SAMPLE_CNT; i++) {
            float sample = 0.0f;
            if (ui.sfxOn) {
                if ((bgm_cnt % 22050) == 0) { /* 0.5s tense pulse */
                    int bar = (bgm_beat / 8) % 4;
                    int b = bgm_beat % 8;
                    if (b == 0 || b == 4) {
                        bgm_f1 = dread_bass[bar];
                        bgm_e1 = 0.12f;
                    }
                    if (b == 2 || b == 5 || b == 7) {
                        bgm_f2 = dread_pluck[(bar + bgm_beat) % 8];
                        bgm_e2 = 0.07f;
                    }
                    bgm_beat++;
                }
                bgm_cnt++;

                if (bgm_e1 > 0.0005f) {
                    bgm_p1 += bgm_f1 / 44100.0f;
                    if (bgm_p1 >= 1.0f) bgm_p1 -= 1.0f;
                    float tri = 4.0f * fabsf(bgm_p1 - 0.5f) - 1.0f;
                    sample += tri * bgm_e1;
                    bgm_e1 *= 0.99988f;
                }
                if (bgm_e2 > 0.0005f) {
                    bgm_p2 += bgm_f2 / 44100.0f;
                    if (bgm_p2 >= 1.0f) bgm_p2 -= 1.0f;
                    sample += sinf(bgm_p2 * PI2) * bgm_e2;
                    bgm_e2 *= 0.99982f;
                }
            }

            if (sfx_env > 0.001f) {
                sfx_phase += sfx_freq / 44100.0f;
                if (sfx_phase >= 1.0f) sfx_phase -= 1.0f;
                float w = 0.0f;
                if (sfx_wave == 0) w = (sfx_phase < 0.5f) ? 1.0f : -1.0f;
                else if (sfx_wave == 1) w = 4.0f * fabsf(sfx_phase - 0.5f) - 1.0f;
                else if (sfx_wave == 2) w = sinf(sfx_phase * PI2);
                else w = next_noise() * 0.7f + ((sfx_phase < 0.5f) ? 0.3f : -0.3f);
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
        audio_thid = sceKernelCreateThread("bv4_audio", audio_thread, 0x12, 0x10000, 0, NULL);
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
