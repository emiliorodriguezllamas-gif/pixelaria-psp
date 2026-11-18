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
static volatile int sfx_wave = 0; /* 0=square, 1=triangle, 2=sine */
static float sfx_phase = 0.0f;

static float bgm_phase1 = 0.0f;
static float bgm_phase2 = 0.0f;
static int bgm_sample_cnt = 0;
static int bgm_beat = 0;

static const float day_bass[4] = {130.81f, 110.00f, 87.31f, 98.00f};
static const float day_bell[8] = {523.25f, 659.25f, 587.33f, 440.00f, 783.99f, 659.25f, 523.25f, 392.00f};
static const float night_bass[4] = {110.00f, 87.31f, 73.42f, 82.41f};
static const float night_bell[8] = {440.00f, 523.25f, 392.00f, 329.63f, 493.88f, 440.00f, 349.23f, 329.63f};
/* Spooky Cemetery / Dungeon theme (D minor / diminished) */
static const float dung_bass[4]  = {73.42f, 58.27f, 55.00f, 49.00f}; /* D2, Bb1, A1, G1 */
static const float dung_bell[8]  = {293.66f, 349.23f, 277.18f, 220.00f, 311.13f, 293.66f, 233.08f, 220.00f};

static float bgm_env1 = 0.0f;
static float bgm_env2 = 0.0f;
static float bgm_f1 = 130.81f;
static float bgm_f2 = 523.25f;

void audio_play_sfx(int id) {
    if (!ui.sfxOn) return;
    switch (id) {
        case SFX_STEP:    sfx_freq = 150.0f; sfx_dfreq = -0.003f; sfx_env = 0.14f; sfx_decay = 0.996f; sfx_wave = 1; break;
        case SFX_SPLASH:  sfx_freq = 260.0f; sfx_dfreq =  0.004f; sfx_env = 0.18f; sfx_decay = 0.998f; sfx_wave = 2; break;
        case SFX_CHOP:    sfx_freq = 190.0f; sfx_dfreq = -0.005f; sfx_env = 0.28f; sfx_decay = 0.997f; sfx_wave = 0; break;
        case SFX_MINE:    sfx_freq = 880.0f; sfx_dfreq = -0.012f; sfx_env = 0.24f; sfx_decay = 0.9975f; sfx_wave = 1; break;
        case SFX_HIT:     sfx_freq = 260.0f; sfx_dfreq = -0.008f; sfx_env = 0.30f; sfx_decay = 0.997f; sfx_wave = 0; break;
        case SFX_PICKUP:  sfx_freq = 523.0f; sfx_dfreq =  0.009f; sfx_env = 0.22f; sfx_decay = 0.998f; sfx_wave = 0; break;
        case SFX_DRINK:   sfx_freq = 320.0f; sfx_dfreq =  0.012f; sfx_env = 0.22f; sfx_decay = 0.998f; sfx_wave = 2; break;
        case SFX_EAT:     sfx_freq = 360.0f; sfx_dfreq =  0.006f; sfx_env = 0.20f; sfx_decay = 0.998f; sfx_wave = 1; break;
        case SFX_BUILD:   sfx_freq = 180.0f; sfx_dfreq =  0.003f; sfx_env = 0.22f; sfx_decay = 0.9975f; sfx_wave = 0; break;
        case SFX_CRAFT:   sfx_freq = 523.0f; sfx_dfreq =  0.008f; sfx_env = 0.25f; sfx_decay = 0.9988f; sfx_wave = 0; break;
        case SFX_LEVELUP: sfx_freq = 587.0f; sfx_dfreq =  0.010f; sfx_env = 0.30f; sfx_decay = 0.9992f; sfx_wave = 1; break;
        case SFX_HURT:    sfx_freq = 190.0f; sfx_dfreq = -0.004f; sfx_env = 0.32f; sfx_decay = 0.9985f; sfx_wave = 0; break;
        case SFX_BLOCKED: sfx_freq =  95.0f; sfx_dfreq = -0.002f; sfx_env = 0.16f; sfx_decay = 0.996f; sfx_wave = 0; break;
        case SFX_UI:      sfx_freq = 640.0f; sfx_dfreq =  0.003f; sfx_env = 0.14f; sfx_decay = 0.995f; sfx_wave = 1; break;
        case SFX_SLASH:   sfx_freq = 720.0f; sfx_dfreq = -0.045f; sfx_env = 0.24f; sfx_decay = 0.9965f; sfx_wave = 1; break;
        case SFX_BONE_HIT:sfx_freq = 980.0f; sfx_dfreq = -0.030f; sfx_env = 0.28f; sfx_decay = 0.996f; sfx_wave = 0; break;
        case SFX_SKEL_ATK:sfx_freq = 280.0f; sfx_dfreq = -0.015f; sfx_env = 0.28f; sfx_decay = 0.9975f; sfx_wave = 0; break;
        case SFX_DOOR:    sfx_freq = 110.0f; sfx_dfreq = -0.002f; sfx_env = 0.30f; sfx_decay = 0.9990f; sfx_wave = 1; break;
        default: break;
    }
}

static int audio_thread(SceSize args, void *argp) {
    short buf[SAMPLE_CNT * 2];
    int i;
    while (audio_running) {
        for (i = 0; i < SAMPLE_CNT; i++) {
            float sample = 0.0f;

            /* Tranquil survival ambient music */
            if (ui.sfxOn) {
                if ((bgm_sample_cnt % 31752) == 0) { /* ~0.72s beat */
                    int hr = gameTime.minute / 60;
                    int isNight = (hr >= 20 || hr < 5);
                    int bar = (bgm_beat / 8) % 4;
                    int b = bgm_beat % 8;
                    if (b == 0) {
                        bgm_f1 = player.inDungeon ? dung_bass[bar] : (isNight ? night_bass[bar] : day_bass[bar]);
                        bgm_env1 = player.inDungeon ? 0.11f : 0.08f;
                    }
                    if (b == 3 || (b == 6 && (bgm_beat % 16 == 6)) || (player.inDungeon && b == 4)) {
                        bgm_f2 = player.inDungeon ? dung_bell[(bar + bgm_beat) % 8] : (isNight ? night_bell[(bar + bgm_beat) % 8] : day_bell[(bar + bgm_beat) % 8]);
                        bgm_env2 = player.inDungeon ? 0.06f : (isNight ? 0.05f : 0.07f);
                    }
                    bgm_beat++;
                }
                bgm_sample_cnt++;

                if (bgm_env1 > 0.0005f) {
                    bgm_phase1 += bgm_f1 / 44100.0f;
                    if (bgm_phase1 >= 1.0f) bgm_phase1 -= 1.0f;
                    float tri = 4.0f * fabsf(bgm_phase1 - 0.5f) - 1.0f;
                    sample += tri * bgm_env1;
                    bgm_env1 *= 0.99992f;
                }
                if (bgm_env2 > 0.0005f) {
                    bgm_phase2 += bgm_f2 / 44100.0f;
                    if (bgm_phase2 >= 1.0f) bgm_phase2 -= 1.0f;
                    sample += sinf(bgm_phase2 * PI2) * bgm_env2;
                    bgm_env2 *= 0.99986f;
                }
            }

            /* 8-bit SFX */
            if (sfx_env > 0.001f) {
                sfx_phase += sfx_freq / 44100.0f;
                if (sfx_phase >= 1.0f) sfx_phase -= 1.0f;
                float w = 0.0f;
                if (sfx_wave == 0) w = (sfx_phase < 0.5f) ? 1.0f : -1.0f;
                else if (sfx_wave == 1) w = 4.0f * fabsf(sfx_phase - 0.5f) - 1.0f;
                else w = sinf(sfx_phase * PI2);
                sample += w * sfx_env;
                sfx_freq += sfx_dfreq;
                if (sfx_freq < 30.0f) sfx_freq = 30.0f;
                sfx_env *= sfx_decay;
            }

            if (sample > 0.9f) sample = 0.9f;
            if (sample < -0.9f) sample = -0.9f;
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
        audio_thid = sceKernelCreateThread("audio_thread", audio_thread, 0x12, 0x10000, 0, NULL);
        if (audio_thid >= 0) {
            sceKernelStartThread(audio_thid, 0, NULL);
        }
    }
}

void audio_shutdown(void) {
    audio_running = 0;
    if (audio_thid >= 0) {
        sceKernelWaitThreadEnd(audio_thid, NULL);
        sceKernelDeleteThread(audio_thid);
    }
    if (audio_chan >= 0) {
        sceAudioChRelease(audio_chan);
    }
}
