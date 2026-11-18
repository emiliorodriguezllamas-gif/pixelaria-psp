#ifndef GAME_H
#define GAME_H

#include <pspkernel.h>

#define SCR_W 480
#define SCR_H 272
#define WORLD_SIZE 64
#define TILE_PX 20
#define VIEW_W 16
#define VIEW_H 10
#define MAX_MOBS 16
#define MAX_POPS 8

enum TileType {
    T_WATER = 0,
    T_GRASS = 1,
    T_FOREST = 2,
    T_SAND = 3,
    T_SNOW = 4,
    T_TREE = 5,
    T_PINE = 6,
    T_ROCK = 7,
    T_IRON = 8,
    T_CACTUS = 9,
    T_BERRY = 10,
    T_RUIN = 11,
    T_CHEST = 12,
    T_WALL = 13,
    T_FIRE = 14,
    T_DUNGEON_DOOR = 15,
    T_DFLOOR = 16,
    T_DWALL = 17,
    T_EXIT = 18,
    T_STAIRS_DOWN = 19
};

enum MobType {
    MOB_SLIME = 0,
    MOB_SKELETON = 1,
    MOB_BOSS = 2
};

enum BiomeType {
    B_MEADOW = 0,
    B_FOREST = 1,
    B_DESERT = 2,
    B_TUNDRA = 3
};

enum SfxId {
    SFX_NONE = 0,
    SFX_STEP,
    SFX_SPLASH,
    SFX_CHOP,
    SFX_MINE,
    SFX_HIT,
    SFX_PICKUP,
    SFX_DRINK,
    SFX_EAT,
    SFX_BUILD,
    SFX_CRAFT,
    SFX_LEVELUP,
    SFX_HURT,
    SFX_BLOCKED,
    SFX_UI,
    SFX_SLASH,
    SFX_BONE_HIT,
    SFX_SKEL_ATK,
    SFX_DOOR
};

typedef struct {
    int x, y;
    int fx, fy;
    float hp, hunger, thirst, stamina;
    int wet;
    int warm;
    int slot;
    int ironTools;
    int swordTier; /* 0=Madera(Estocada), 1=Hierro(Barrido), 2=Acero(Distancia), 3=Real(Area) */
    int swordPct;  /* 0..100% weapon mastery */
    int hasBoat;
    int inBoat;
    int wood, stone, iron, bones;
    int armorFrags, armorPieces;
    int campfires, walls, food;
    int potions[7]; /* 7 potion types from chests */
    int selPotion;  /* active potion type 0..6 */
    int inDungeon;
    int dungeonFloor; /* 1..10 */
    int savedX, savedY;
    int iframes;
} Player;

typedef struct {
    int lv;
    int xp;
} Skill;

typedef struct {
    Skill tala;
    Skill mineria;
    Skill combate;
    Skill supervivencia;
    Skill construccion;
} Skills;

typedef struct {
    int steps;
    int trees;
    int ores;
    int slimes;
    int crafted;
    int chests;
} Stats;

typedef struct {
    int x, y, hp, maxHp, alive, type, bossId;
} Mob;

typedef struct {
    int wx, wy;
    char text[24];
    unsigned int color;
    int timer;
} Popup;

typedef struct {
    int x, y, dx, dy, life, dmg, active;
} Projectile;

typedef struct {
    int day;
    int minute;
    int season;
    int weather; /* 0=Sol, 1=Lluvia, 2=Nieve */
    int ticks;
} GameTime;

typedef struct {
    int inTitle;
    int titleSel;
    int showHelp;
    int craftOpen;
    int statsOpen;
    int menuSel;
    int sfxOn;
    int shakeTimer;
    int damageFlash;
    int lungeTimer;
    int walkTimer;
    int walkFrame;
    int actionType; /* 0=none, 1=break/hit, 2=build */
    int slashTimer;
    int spinTimer;
    int gatherX, gatherY, gatherTimer, gatherMax, gatherKind;
    char msg[48];
} UIState;

#define DUNG_W 32
#define DUNG_H 20

extern unsigned char world[WORLD_SIZE * WORLD_SIZE];
extern unsigned char biomes[WORLD_SIZE * WORLD_SIZE];
extern unsigned char dungeon[DUNG_W * DUNG_H];
extern Mob mobs[MAX_MOBS];
extern Mob dungMobs[MAX_MOBS];
extern Projectile proj;
extern Popup pops[MAX_POPS];
extern Player player;
extern Skills skills;
extern Stats stats;
extern GameTime gameTime;
extern UIState ui;

int maxHp(void);
int totalLevel(void);
int nextXp(int lv);

void audio_init(void);
void audio_play_sfx(int id);
void audio_shutdown(void);

void render_init(void);
void render_frame(void);

#endif
