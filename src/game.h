#ifndef GAME_H
#define GAME_H

#include <pspkernel.h>

#define SCR_W 480
#define SCR_H 272
#define MAX_ENEMIES 16
#define MAX_BULLETS 40
#define MAX_PARTS 32

enum SfxId {
    SFX_NONE = 0,
    SFX_GUN,
    SFX_MISSILE,
    SFX_ROLL,
    SFX_EXPLODE,
    SFX_HURT,
    SFX_UI
};

typedef struct {
    float x, y, z;
    float roll, pitch;
    int rollTimer;
    int hp, maxHp;
    int fireCd;
    int missileCd;
    int iframes;
    int wave;
    int score;
    int bestScore;
} Plane;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    int hp, maxHp;
    int isBoss;
    int fireCd;
    int hitFlash;
    int alive;
} EnemyPlane;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    int fromPlayer;
    int isMissile;
    int alive;
} Bullet;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    unsigned int color;
    int life;
} Particle;

typedef struct {
    int inTitle;
    int gameOver;
    int sfxOn;
    int shakeTimer;
    char msg[48];
    int msgTimer;
} UIState;

extern Plane player;
extern EnemyPlane enemies[MAX_ENEMIES];
extern Bullet bullets[MAX_BULLETS];
extern Particle parts[MAX_PARTS];
extern UIState ui;

void render3d_init(void);
void render3d_frame(void);
void render3d_shutdown(void);

void audio_init(void);
void audio_play_sfx(int id);
void audio_shutdown(void);

#endif
