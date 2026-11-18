#ifndef GAME_H
#define GAME_H

#include <pspkernel.h>

#define SCR_W 480
#define SCR_H 272
#define MAX_TRAFFIC 16
#define MAX_SPARKS 32
#define TRACK_SEGS 240

enum SfxId {
    SFX_NONE = 0,
    SFX_NITRO,
    SFX_DRIFT,
    SFX_CRASH,
    SFX_LAP,
    SFX_UI
};

typedef struct {
    float x;         /* lateral offset [-1.0 .. 1.0] */
    float z;         /* track progress */
    float speed;     /* km/h-ish [0 .. 320] */
    float steer;     /* visual steer angle */
    float drift;     /* drift intensity */
    float nitro;     /* [0 .. 100] */
    int nitroActive;
    int lap;         /* 1 .. 3 */
    int score;
    int bestScore;
    int timeFrames;
    int crashTimer;
} Car;

typedef struct {
    float x;
    float z;
    float speed;
    unsigned int color;
    int alive;
} TrafficCar;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    unsigned int color;
    int life;
} Spark;

typedef struct {
    int inTitle;
    int finished;
    int sfxOn;
    int shakeTimer;
    char msg[48];
    int msgTimer;
} UIState;

extern Car player;
extern TrafficCar traffic[MAX_TRAFFIC];
extern Spark sparks[MAX_SPARKS];
extern UIState ui;

void render3d_init(void);
void render3d_frame(void);
void render3d_shutdown(void);

void audio_init(void);
void audio_play_sfx(int id);
void audio_shutdown(void);

#endif
