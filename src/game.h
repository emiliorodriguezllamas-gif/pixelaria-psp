#ifndef GAME_H
#define GAME_H

#include <pspkernel.h>

#define SCR_W 480
#define SCR_H 272
#define MAX_ENEMIES 12
#define MAX_BULLETS 40
#define MAX_PARTS 40

enum SfxId {
    SFX_NONE = 0,
    SFX_LASER,
    SFX_MISSILE,
    SFX_DASH,
    SFX_SHIELD,
    SFX_EXPLODE,
    SFX_HURT,
    SFX_UI
};

typedef struct {
    float x, y, z;
    float vx, vz;
    float yaw;
    int hp, maxHp;
    int energy, maxEnergy;
    int shieldOn;
    int fireCd;
    int missileCd;
    int dashTimer;
    int iframes;
    int wave;
    int score;
    int bestScore;
} Mech;

typedef struct {
    float x, y, z;
    float vx, vz;
    float yaw;
    int hp, maxHp;
    int isBoss;
    int fireCd;
    int hitFlash;
    int alive;
} EnemyMech;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    int fromPlayer;
    int homing;
    int targetIdx;
    int alive;
    int life;
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

extern Mech player;
extern EnemyMech enemies[MAX_ENEMIES];
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
