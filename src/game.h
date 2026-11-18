#ifndef GAME_H
#define GAME_H

#include <pspkernel.h>

#define SCR_W 480
#define SCR_H 272
#define MAX_RIVALS 3
#define MAX_BOXES 8

enum ItemType {
    ITEM_NONE = 0,
    ITEM_TURBO,
    ITEM_ROCKET,
    ITEM_SHIELD
};

enum SfxId {
    SFX_NONE = 0,
    SFX_TURBO,
    SFX_ITEM,
    SFX_HIT,
    SFX_LAP,
    SFX_UI
};

typedef struct {
    float x, z;
    float speed;
    float steer;
    int item;
    int turboTimer;
    int shieldTimer;
    int lap;
    int score;
    int bestScore;
} Kart;

typedef struct {
    float x, z;
    float speed;
    unsigned int color;
    int spinTimer;
} RivalKart;

typedef struct {
    float x, z;
    int active;
    int respawn;
} ItemBox;

typedef struct {
    int inTitle;
    int finished;
    int sfxOn;
    int shakeTimer;
    char msg[48];
    int msgTimer;
} UIState;

extern Kart player;
extern RivalKart rivals[MAX_RIVALS];
extern ItemBox boxes[MAX_BOXES];
extern UIState ui;

void render3d_init(void);
void render3d_frame(void);
void render3d_shutdown(void);

void audio_init(void);
void audio_play_sfx(int id);
void audio_shutdown(void);

#endif
