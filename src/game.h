#ifndef GAME_H
#define GAME_H

#include <pspkernel.h>

#define SCR_W 480
#define SCR_H 272
#define MAP_W 24
#define MAP_H 24
#define MAX_ENTS 32

enum WallType {
    W_EMPTY = 0,
    W_WOOD_WALL = 1,
    W_STONE_WALL = 2,
    W_CHURCH_WALL = 3,
    W_GATE_DOOR = 4,
    W_BLOOD_WALL = 5
};

enum EntType {
    ENT_VILLAGER = 0,
    ENT_CHAINSAW = 1,
    ENT_MERCHANT = 2,
    ENT_CRATE = 3,
    ENT_HERB_G = 4,
    ENT_HERB_R = 5,
    ENT_AMMO = 6,
    ENT_PESETAS = 7,
    ENT_BOSS = 8
};

enum WeaponId {
    WPN_KNIFE = 0,
    WPN_PISTOL = 1,
    WPN_SHOTGUN = 2,
    WPN_RIFLE = 3
};

enum SfxId {
    SFX_NONE = 0,
    SFX_PISTOL,
    SFX_SHOTGUN,
    SFX_RIFLE,
    SFX_KNIFE,
    SFX_HEADSHOT,
    SFX_KICK,
    SFX_CHAINSAW,
    SFX_CRATE,
    SFX_PICKUP,
    SFX_RELOAD,
    SFX_HURT,
    SFX_MERCHANT,
    SFX_UI
};

typedef struct {
    float x, y;
    float ang;
    int aimX, aimY; /* Laser dot screen offset from center */
    int aiming;     /* L trigger held */
    int hp, maxHp;
    int pesetas;
    int weapon;
    int hasShotgun, hasRifle;
    int mag[4];
    int maxMag[4];
    int reserve[4];
    int powLv[4];
    int herbsG, herbsR, sprays;
    int armorLv;
    int stage;      /* 1..4 */
    int kills;
    int fireTimer;
    int reloadTimer;
    int kickTimer;
    int damageFlash;
    int iframes;
} Player;

typedef struct {
    float x, y;
    int type;
    int hp, maxHp;
    int alive;
    int stunTimer; /* >0 = kneeling/staggered, ready for Roundhouse Kick! */
    int hitFlash;
    int atkCooldown;
} Entity;

typedef struct {
    int inTitle;
    int titleSel;
    int showHelp;
    int shopOpen;
    int shopSel;
    int invOpen;
    int invSel;
    int sfxOn;
    int shakeTimer;
    int headPopTimer;
    char msg[52];
    int msgTimer;
} UIState;

extern unsigned char worldMap[MAP_W * MAP_H];
extern Entity ents[MAX_ENTS];
extern Player player;
extern UIState ui;

void load_stage(int stg);
void save_game(void);
int load_game(void);

void audio_init(void);
void audio_play_sfx(int id);
void audio_shutdown(void);

void render_init(void);
void render_frame(void);

#endif
