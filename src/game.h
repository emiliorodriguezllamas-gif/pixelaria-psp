#ifndef GAME_H
#define GAME_H

#include <pspkernel.h>

#define SCR_W 480
#define SCR_H 272
#define MAX_OBS 14

enum SfxId {
    SFX_NONE = 0,
    SFX_PASS,
    SFX_CRASH,
    SFX_UI
};

typedef struct {
    float angle;     /* player angle around tunnel [0 .. 2pi] */
    float speed;
    float z;
    int score;
    int bestScore;
} Runner;

typedef struct {
    float z;
    float angle;     /* center angle of obstacle */
    float arc;       /* angular half-width */
    unsigned int color;
    int passed;
    int active;
} Obstacle;

typedef struct {
    int inTitle;
    int gameOver;
    int sfxOn;
    int shakeTimer;
    char msg[48];
    int msgTimer;
} UIState;

extern Runner player;
extern Obstacle obs[MAX_OBS];
extern UIState ui;

void render3d_init(void);
void render3d_frame(void);
void render3d_shutdown(void);

void audio_init(void);
void audio_play_sfx(int id);
void audio_shutdown(void);

#endif
