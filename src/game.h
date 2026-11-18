#ifndef GAME_H
#define GAME_H

#include <pspkernel.h>

#define SCR_W 480
#define SCR_H 272
#define MAX_ENEMIES 24
#define MAX_BULLETS 32
#define MAX_EBULLETS 24
#define MAX_PARTS 48
#define MAX_STARS 64

enum EnemyType {
    EN_FIGHTER = 0,
    EN_ASTEROID = 1,
    EN_RING = 2,
    EN_BOSS = 3
};

enum SfxId {
    SFX_NONE = 0,
    SFX_LASER,
    SFX_LOCK,
    SFX_MISSILE,
    SFX_ROLL,
    SFX_EXPLODE,
    SFX_BOMB,
    SFX_RING,
    SFX_HURT,
    SFX_UI
};

typedef struct {
    float x, y;
    float vx, vy;
    float roll, pitch;
    int rollTimer;
    int hp, maxHp;
    int boost, maxBoost;
    int bombs;
    int laserLv;
    int score, bestScore;
    int combo, comboTimer;
    int stage;      /* 1..3 */
    int stageTimer;
    int fireCd;
    int iframes;
    int lockTargets[4];
    int lockCount;
    int locking;
} Player;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    float rot;
    int type;
    int hp, maxHp;
    int alive;
    int hitFlash;
    int fireCd;
} Enemy;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    int targetIdx; /* >=0 for homing missile */
    int alive;
} Bullet;

typedef struct {
    float x, y, z;
    float vx, vy, vz;
    unsigned int color;
    float size;
    int life;
} Particle;

typedef struct {
    float x, y, z;
} Star;

typedef struct {
    int inTitle;
    int gameOver;
    int victory;
    int sfxOn;
    int shakeTimer;
    int bombFlash;
    char msg[48];
    int msgTimer;
} UIState;

extern Player player;
extern Enemy enemies[MAX_ENEMIES];
extern Bullet bullets[MAX_BULLETS];
extern Bullet ebullets[MAX_EBULLETS];
extern Particle parts[MAX_PARTS];
extern Star stars[MAX_STARS];
extern UIState ui;

void audio_init(void);
void audio_play_sfx(int id);
void audio_shutdown(void);

void render3d_init(void);
void render3d_frame(void);
void render3d_shutdown(void);

#endif
