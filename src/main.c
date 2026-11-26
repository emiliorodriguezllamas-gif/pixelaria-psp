#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspaudio.h>
#include <psppower.h>
#include <psprtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"
#include "world_sprites.h"
#include "ground_deco.h"

PSP_MODULE_INFO("AetheriaSkyRuins", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define VRAM_DRAW_OFF 0
#define VRAM_DISP_OFF (BUF_W * SCR_H * 4)
#define VRAM_ZBUF_OFF (VRAM_DISP_OFF * 2)

#define ABGR(r, g, b, a) (((unsigned int)(a) << 24) | ((unsigned int)(b) << 16) | ((unsigned int)(g) << 8) | (unsigned int)(r))

static unsigned int __attribute__((aligned(16))) gulist[262144];
static volatile int running = 1;

static int exit_cb(int a1, int a2, void *c) { running = 0; return 0; }
static int cb_th(SceSize a, void *p) {
    int id = sceKernelCreateCallback("Exit", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}

static unsigned int rng_st = 0x24681357u;
static unsigned int xr(void) {
    rng_st ^= rng_st << 13;
    rng_st ^= rng_st >> 17;
    rng_st ^= rng_st << 5;
    return rng_st;
}
static float frand(float lo, float hi) {
    return lo + ((float)(xr() & 0xFFFF) / 65535.0f) * (hi - lo);
}

/* ================= GU VERTEX TYPES ================= */
typedef struct {
    float u, v;
    unsigned int color;
    float x, y, z;
} Vtx3D;

typedef struct {
    float u, v;
    unsigned int color;
    short x, y, z;
} Vtx2D;

typedef struct {
    unsigned int color;
    short x, y, z;
} VtxCol2D;

typedef struct {
    unsigned int color;
    float x, y, z;
} VtxCol3D;

#define VTYPE_3D     (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
#define VTYPE_COL3D  (GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
#define VTYPE_2D     (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D)
#define VTYPE_COL2D  (GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D)

/* ================= PROCEDURAL 64x64 TEXTURES ================= */
static unsigned int __attribute__((aligned(16))) texGrass[64 * 64];
static unsigned int __attribute__((aligned(16))) texStone[64 * 64];
static unsigned int __attribute__((aligned(16))) texWater[64 * 64];
static unsigned int __attribute__((aligned(16))) texObsidian[64 * 64];
static unsigned int __attribute__((aligned(16))) texCrystal[64 * 64];
static unsigned int __attribute__((aligned(16))) texWood[64 * 64];
static unsigned int __attribute__((aligned(16))) texTree[64 * 64];
static unsigned int __attribute__((aligned(16))) texHero[64 * 64];
static unsigned int __attribute__((aligned(16))) texNpc[64 * 64];
static unsigned int __attribute__((aligned(16))) texMob[3][64 * 64];
static unsigned int __attribute__((aligned(16))) texBoss[3][64 * 64];
static unsigned int __attribute__((aligned(16))) texSoftGlow[64 * 64];
static unsigned int __attribute__((aligned(16))) texFont[128 * 64];

static void upscale_spr_to_64(const unsigned int *src, int sw, int sh, unsigned int *dst) {
    for (int y = 0; y < 64; y++) {
        int sy = (y * sh) / 64;
        for (int x = 0; x < 64; x++) {
            int sx = (x * sw) / 64;
            unsigned int c = src[sy * sw + sx];
            if ((c >> 24) < 32) dst[y * 64 + x] = 0;
            else dst[y * 64 + x] = c | 0xFF000000u;
        }
    }
}

static void build_textures(void) {
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            float fx = (float)x / 64.0f, fy = (float)y / 64.0f;
            float n1 = sinf(fx * 6.28318f * 3.0f + cosf(fy * 6.28318f * 2.0f));
            float n2 = cosf(fy * 6.28318f * 5.0f - fx * 6.28318f * 2.0f);
            float h = 0.5f + 0.25f * n1 + 0.25f * n2;

            int gr = (int)(38 + h * 36), gg = (int)(118 + h * 58), gb = (int)(54 + h * 26);
            if (((x * 13 + y * 29) & 63) < 4) { gr += 25; gg += 35; }
            texGrass[y * 64 + x] = ABGR(gr, gg, gb, 255);

            int bx = x % 32, by = y % 32;
            int mortar = (bx < 2 || by < 2);
            int st = mortar ? 62 : (int)(115 + h * 38);
            int moss = (!mortar && ((x + y * 3) & 31) < 3) ? 18 : 0;
            texStone[y * 64 + x] = ABGR(st - moss/2, st + moss, st + 8 - moss/2, 255);

            float wv = 0.5f + 0.5f * sinf((fx * 4.0f + fy * 3.0f) * 6.28318f) * cosf((fy * 4.0f - fx * 2.0f) * 6.28318f);
            texWater[y * 64 + x] = ABGR((int)(24 + wv * 70), (int)(110 + wv * 95), (int)(185 + wv * 65), 215);

            int obM = ((x % 16) < 1 || (y % 32) < 1);
            texObsidian[y * 64 + x] = ABGR(obM ? 55 : (int)(128 + h * 42), obM ? 35 : (int)(74 + h * 28), obM ? 48 : (int)(66 + h * 22), 255);

            float dx = fx - 0.5f, dy = fy - 0.5f;
            float cr = 1.0f - sqrtf(dx * dx + dy * dy) * 1.4f;
            if (cr < 0.0f) cr = 0.0f;
            int rune = (abs(x - 32) < 3 || abs(y - 32) < 3 || abs((x - 32) - (y - 32)) < 2);
            int cR = (int)(45 + cr * 90 + (rune ? 70 : 0));
            int cG = (int)(165 + cr * 85 + (rune ? 40 : 0));
            int cB = (int)(235 + cr * 20);
            if (cR > 255) cR = 255; if (cG > 255) cG = 255; if (cB > 255) cB = 255;
            texCrystal[y * 64 + x] = ABGR(cR, cG, cB, 255);

            int plank = (y % 16) < 2;
            int wd = plank ? 48 : (int)(112 + n1 * 22);
            texWood[y * 64 + x] = ABGR(wd + 26, wd - 4, wd - 30, 255);

            float d2 = (dx * dx + dy * dy) * 4.0f;
            float ga = (d2 < 1.0f) ? (1.0f - d2) * (1.0f - d2) : 0.0f;
            texSoftGlow[y * 64 + x] = ABGR(255, 255, 255, (int)(ga * 255.0f));
        }
    }

    upscale_spr_to_64(spr_tree32, 32, 32, texTree);
    upscale_spr_to_64(spr_bard_48, 48, 48, texHero);
    upscale_spr_to_64(spr_band2_48, 48, 48, texNpc);
    memcpy(texMob[0], mob_slime, 64 * 64 * 4);
    memcpy(texMob[1], mob_goblin, 64 * 64 * 4);
    memcpy(texMob[2], mob_wraith, 64 * 64 * 4);
    memcpy(texBoss[0], boss_golem, 64 * 64 * 4);
    memcpy(texBoss[1], boss_automaton, 64 * 64 * 4);
    memcpy(texBoss[2], boss_dragon, 64 * 64 * 4);

    memset(texFont, 0, sizeof(texFont));
    for (int ch = 0; ch < 96; ch++) {
        int gx = (ch % 16) * 8;
        int gy = (ch / 16) * 8;
        const unsigned char *glyph = font5x7[ch];
        for (int ci = 0; ci < 5; ci++) {
            unsigned char bits = glyph[ci];
            for (int r = 0; r < 7; r++) {
                if (bits & (1 << r)) {
                    texFont[(gy + r + 1) * 128 + (gx + ci + 1)] = ABGR(10, 12, 24, 220);
                    texFont[(gy + r) * 128 + (gx + ci)] = ABGR(255, 255, 255, 255);
                }
            }
        }
    }
    sceKernelDcacheWritebackAll();
}

/* ================= HARDWARE 2D GPU UI HELPERS ================= */
static void gu_rect2d(int x, int y, int w, int h, unsigned int col) {
    VtxCol2D *v = (VtxCol2D *)sceGuGetMemory(2 * sizeof(VtxCol2D));
    v[0].color = col; v[0].x = (short)x;     v[0].y = (short)y;     v[0].z = 0;
    v[1].color = col; v[1].x = (short)(x+w); v[1].y = (short)(y+h); v[1].z = 0;
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDrawArray(GU_SPRITES, VTYPE_COL2D, 2, 0, v);
}

static void gu_window2d(int x, int y, int w, int h) {
    gu_rect2d(x, y, w, h, ABGR(14, 22, 58, 210));
    gu_rect2d(x, y, w, 1, ABGR(220, 235, 255, 240));
    gu_rect2d(x, y + h - 1, w, 1, ABGR(220, 235, 255, 240));
    gu_rect2d(x, y, 1, h, ABGR(220, 235, 255, 240));
    gu_rect2d(x + w - 1, y, 1, h, ABGR(220, 235, 255, 240));
}

static void gu_text2d(int x, int y, const char *s, unsigned int col, int scale) {
    int len = (int)strlen(s);
    if (len <= 0) return;
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, 128, 64, 128, texFont);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    Vtx2D *v = (Vtx2D *)sceGuGetMemory(len * 2 * sizeof(Vtx2D));
    int cx = x;
    for (int i = 0; i < len; i++) {
        int ch = (unsigned char)s[i] - 32;
        if (ch < 0 || ch >= 96) ch = 31;
        float u0 = (float)((ch % 16) * 8);
        float v0 = (float)((ch / 16) * 8);
        float u1 = u0 + 6.0f;
        float v1 = v0 + 8.0f;
        v[i*2+0].u = u0; v[i*2+0].v = v0; v[i*2+0].color = col; v[i*2+0].x = (short)cx;             v[i*2+0].y = (short)y;             v[i*2+0].z = 0;
        v[i*2+1].u = u1; v[i*2+1].v = v1; v[i*2+1].color = col; v[i*2+1].x = (short)(cx+6*scale); v[i*2+1].y = (short)(y+8*scale); v[i*2+1].z = 0;
        cx += 6 * scale;
    }
    sceGuDrawArray(GU_SPRITES, VTYPE_2D, len * 2, 0, v);
}

/* ================= FAST LUT AMBIENT ADVENTURE AUDIO ================= */
static int ach = -1, ath = -1;
static volatile int arun = 0, sfx_t = 0, sfx_max = 2000, curZoneAudio = 0;
static float sfx_f = 440.0f, sfx_p = 0.0f;
static short abuf[2][1024 * 2];
static float echoL[4096], echoR[4096];
static int echoIdx = 0;

static void play_sfx(float f, int dur) {
    sfx_f = f; sfx_t = dur; sfx_max = dur > 0 ? dur : 1; sfx_p = 0.0f;
}

static const float arpNotes[4][16] = {
    { 261.63f,329.63f,392.00f,523.25f, 392.00f,329.63f,293.66f,349.23f, 440.00f,523.25f,587.33f,659.25f, 523.25f,440.00f,392.00f,329.63f },
    { 220.00f,261.63f,329.63f,440.00f, 392.00f,329.63f,293.66f,261.63f, 220.00f,293.66f,349.23f,440.00f, 392.00f,349.23f,329.63f,246.94f },
    { 196.00f,246.94f,293.66f,392.00f, 349.23f,293.66f,261.63f,220.00f, 196.00f,261.63f,329.63f,392.00f, 440.00f,392.00f,329.63f,246.94f },
    { 164.81f,196.00f,246.94f,329.63f, 392.00f,329.63f,246.94f,196.00f, 174.61f,220.00f,261.63f,349.23f, 329.63f,293.66f,246.94f,196.00f }
};

static int audio_th(SceSize a, void *p) {
    static float audSin[256], audExp[256];
    static int init = 0;
    if (!init) {
        for (int i = 0; i < 256; i++) {
            audSin[i] = sinf(((float)i / 256.0f) * 6.2831853f);
            audExp[i] = expf(-((float)i / 255.0f) * 8.0f);
        }
        init = 1;
    }
    #define FSIN(ph) (audSin[(int)((ph) * 40.743665f) & 255])
    #define FEXP(t)  (audExp[((t) < 0.0f ? 0 : ((t) >= 1.0f ? 255 : (int)((t) * 255.0f)))])

    int bi = 0, step = 0;
    float sub = 0.0f, pPad = 0.0f, pArp = 0.0f;
    const float invSr = 1.0f / 44100.0f;
    const float stepLen = 9800.0f;
    while (arun) {
        short *b = abuf[bi];
        int z = curZoneAudio & 3;
        for (int i = 0; i < 1024; i++) {
            sub += 1.0f;
            if (sub >= stepLen) { sub -= stepLen; step = (step + 1) & 15; }
            float t = sub / stepLen;
            float fA = arpNotes[z][step];
            pArp += 6.2831853f * fA * invSr;
            if (pArp > 6.2831853f) pArp -= 6.2831853f;
            float sArp = (FSIN(pArp) * 0.7f + FSIN(pArp * 2.0f) * 0.2f) * 0.16f * FEXP(t * 0.9f);

            float fP = arpNotes[z][step & 12] * 0.5f;
            pPad += 6.2831853f * fP * invSr;
            if (pPad > 6.2831853f) pPad -= 6.2831853f;
            float sPad = FSIN(pPad) * 0.14f;

            float sfx = 0.0f;
            if (sfx_t > 0) {
                float se = (float)sfx_t / (float)sfx_max;
                sfx_p += 6.2831853f * sfx_f * invSr;
                if (sfx_p > 6.2831853f) sfx_p -= 6.2831853f;
                sfx = FSIN(sfx_p) * 0.25f * se;
                sfx_t--;
            }

            float L = sPad + sArp * 0.85f + sfx;
            float R = sPad + sArp * 1.15f + sfx;
            int rIdx = (echoIdx - 3200) & 4095;
            L += echoR[rIdx] * 0.24f;
            R += echoL[rIdx] * 0.24f;
            echoL[echoIdx] = L; echoR[echoIdx] = R;
            echoIdx = (echoIdx + 1) & 4095;

            int iL = (int)(L * 24000.0f), iR = (int)(R * 24000.0f);
            if (iL > 32000) iL = 32000; if (iL < -32000) iL = -32000;
            if (iR > 32000) iR = 32000; if (iR < -32000) iR = -32000;
            b[i * 2] = (short)iL;
            b[i * 2 + 1] = (short)iR;
        }
        sceAudioOutputBlocking(ach, PSP_AUDIO_VOLUME_MAX, b);
        bi ^= 1;
    }
    return 0;
}

/* ================= 3D WORLD, TOWNS & DUNGEONS ================= */
typedef struct {
    float x, y, z, w, d, h;
    int texType; /* 0=grass, 1=stone, 2=obsidian, 3=wood, 4=crystal */
    int zone;
} Platform;

typedef struct {
    float x, y, z, rad, h;
    int kind; /* 0=pillar, 1=tree, 2=obelisk, 3=chest, 4=crystal_shrine, 5=house, 6=dungeon_portal, 7=exit_portal */
    int active;
    int id;
} Prop3D;

typedef struct {
    float x, y, z, homeX, homeZ;
    int role, npcId; /* 0=Eldon, 1=Kael, 2=Lyra, 3=Orin, 4=Nilo, 5=Vulcan */
    const char *name;
    const char *msg;
} Npc3D;

typedef struct {
    float x, y, z, homeX, homeY, homeZ;
    int hp, maxHp, kind, alive, flash, cd, isBoss, bossId;
    int state, timer, respawnT; /* 0=chase, 1=windup(telegraph), 2=recovery */
    float atkX, atkZ, atkRad;
} Enemy3D;

typedef struct {
    float x, y, z, vx, vy, vz, life, maxLife, size;
    unsigned int col;
} Particle3D;

#define MAX_PLATS 160
#define MAX_PROPS 2600
#define MAX_ENEMIES 120
#define MAX_NPCS 12
#define MAX_PARTS 140

static Platform plats[MAX_PLATS];
static int nPlats = 0;
static Prop3D props[MAX_PROPS];
static int nProps = 0;
static Enemy3D enemies[MAX_ENEMIES];
static int nEnemies = 0;
static Npc3D npcs[MAX_NPCS];
static int nNpcs = 0;
static Particle3D parts[MAX_PARTS];

/* Persistent Adventure State */
static int chestOpened[16];
static int obeliskLit[12];
static int dngCleared[3];
static int clueFound[6];
static int cluesCount = 0, trueEnding = 0;
static int qState[4] = { 0, 0, 0, 0 }; /* 0:Eldon->Nilo, 1:Kael->Vulcan, 2:Lyra, 3:Orin */
static int qKillsForest = 0, qKillsLake = 0, qKillsCanyon = 0;

static int get_npc_marker(int nid) {
    /* 1=Yellow ! (New), 2=Silver ? (Active), 3=Gold ? (Turn-in Ready) */
    if (nid == 0) return (qState[0] == 0) ? 1 : 0; /* Eldon */
    if (nid == 4) { /* Nilo */
        if (qState[0] == 1 || qState[0] == 3) return 3;
        if (qState[0] == 2) return 2;
    }
    if (nid == 1) return (qState[1] == 0) ? 1 : 0; /* Kael */
    if (nid == 5) { /* Vulcan */
        if (qState[1] == 1 || qState[1] == 3) return 3;
        if (qState[1] == 2) return 2;
    }
    if (nid == 2) { /* Lyra */
        if (qState[2] == 0) return 1;
        if (qState[2] == 1) return 2;
        if (qState[2] == 2) return 3;
    }
    if (nid == 3) { /* Orin */
        if (qState[3] == 0) return 1;
        if (qState[3] == 1) return (cluesCount >= 6 || (qState[0] >= 4 && qState[1] >= 4 && qState[2] >= 3)) ? 3 : 2;
    }
    return 0;
}

static int storyOpen = 0;
static char storyTitle[48] = "", storyL1[72] = "", storyL2[72] = "", storyL3[72] = "";

static const char *clueTitles[6] = {
    "PISTA 1/6: TABLILLA DEL ALBA",
    "PISTA 2/6: DIARIO DEL GUARDABOSQUES",
    "PISTA 3/6: INSCRIPCION SUMERGIDA",
    "PISTA 4/6: CARTA DEL LAGO",
    "PISTA 5/6: CRONICA DE OBSIDIANA",
    "PISTA 6/6: VOLUNTAD DE VAELEN"
};
static const char *clueText[6][3] = {
    { "Hace mil anos, Aetheria era un solo continente...", "Sostenido por tres Corazones de Cristal en los Templos.", "Sigue los 4 caminos para descubrir las demas tablillas." },
    { "Vi al Arconte Vaelen cruzar la Torre de Vigia Oeste.", "Me dijo: 'El Dragon no es un monstruo...", "Es el Guardian que sostiene el cielo sobre nosotros.'" },
    { "La grieta no vino de las fronteras del Sur.", "Alguien rompio los Sellos Astrales desde dentro", "intentando robar el poder de la inmortalidad." },
    { "En Aldea Lacustre oyeron un latido bajo el agua.", "La corrupcion del Dragon no nace de la maldad,", "sino de una herida oscura que nadie ha curado." },
    { "Vaelen cruzo el Desfiladero hasta la Forja Carmesi", "para reescribir el pacto antiguo del cielo...", "Pero la Sombra devoro su mente y silencio el mundo." },
    { "Si lees esto, Portador del Wisp, escuchame:", "NO mates al Dragon en la Ciudadela Astral.", "Con las 3 Llaves, pulsa /\\ ante el para PURIFICARLO!" }
};

static void show_story(const char *t, const char *l1, const char *l2, const char *l3) {
    snprintf(storyTitle, sizeof(storyTitle), "%s", t);
    snprintf(storyL1, sizeof(storyL1), "%s", l1);
    snprintf(storyL2, sizeof(storyL2), "%s", l2);
    snprintf(storyL3, sizeof(storyL3), "%s", l3);
    storyOpen = 1;
}

static float heroX = 0.0f, heroY = 0.10f, heroZ = 6.0f, heroYaw = 3.14159f;
static float saveOvX = 0.0f, saveOvY = 0.10f, saveOvZ = 6.0f;
static float camYaw = 0.0f;
static int heroFlip = 0, heroHp = 100, heroMaxHp = 100, heroAtk = 12, forgeCount = 0;
static int forge_cost(void) { return 40 + 35 * forgeCount + 15 * forgeCount * forgeCount; }
static float heroStam = 100.0f, heroMaxStam = 100.0f;
static int stamDelay = 0, heavyT = 0;

/* Guitar Hero Critical Special ("Sinfonia Critica" every ~3 combats) */
typedef struct { float y, spd; int lane, active; } GhNote;
#define GH_MAX 10
#define GH_HIT_Y 208
static GhNote ghNotes[GH_MAX];
static int furorCharge = 40; /* 0..100: +34 per kill, +8 per hit */
static int ghActive = 0, ghSpawned = 0, ghSpawnT = 0, ghHits = 0;
static float ghMult = 1.5f;
static char ghJudge[32] = "";
static int ghJudgeT = 0;
static int heroGold = 25, heroCoins = 30, heroPotions = 2, heroFood = 2, heroElixirs = 1, heroLv = 1, heroXp = 0;
static float heroHunger = 100.0f;
static int heroRelics = 0, runesLit = 0, keysFound = 0;
static int dashT = 0, atkT = 0, pulseT = 0, invulnT = 0, heroFlash = 0;
static float dashDx = 0.0f, dashDz = 0.0f;
static int worldMode = 0; /* 0=Overworld, 1=Dungeon */
static int curDungeon = 0, dngRunes = 0, hasSave = 0;
static int shopOpen = 0, shopCursor = 0, hungerTick = 0;

typedef struct {
    unsigned int magic;
    int lv, xp, gold, coins, potions, food, elixirs, atk, maxHp, forge, furor, clues, runes, keys, relics, ending;
    float hunger;
    int chests[16], obelisks[12], dngs[3], clueArr[6], qSt[4], qF, qL, qC;
} SaveData;

static void save_game(void) {
    SaveData sd;
    sd.magic = 0xAE7E2026u;
    sd.lv = heroLv; sd.xp = heroXp; sd.gold = heroGold; sd.coins = heroCoins;
    sd.potions = heroPotions; sd.food = heroFood; sd.elixirs = heroElixirs;
    sd.hunger = heroHunger;
    sd.atk = heroAtk; sd.maxHp = heroMaxHp; sd.forge = forgeCount; sd.furor = furorCharge;
    sd.clues = cluesCount; sd.runes = runesLit; sd.keys = keysFound; sd.relics = heroRelics;
    sd.ending = trueEnding;
    memcpy(sd.chests, chestOpened, sizeof(chestOpened));
    memcpy(sd.obelisks, obeliskLit, sizeof(obeliskLit));
    memcpy(sd.dngs, dngCleared, sizeof(dngCleared));
    memcpy(sd.clueArr, clueFound, sizeof(clueFound));
    memcpy(sd.qSt, qState, sizeof(qState));
    sd.qF = qKillsForest; sd.qL = qKillsLake; sd.qC = qKillsCanyon;
    SceUID fd = sceIoOpen("ms0:/PSP/GAME/AetheriaSkyRuins/save.bin", PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) { sceIoWrite(fd, &sd, sizeof(sd)); sceIoClose(fd); hasSave = 1; }
}

static int load_game(void) {
    SaveData sd;
    SceUID fd = sceIoOpen("ms0:/PSP/GAME/AetheriaSkyRuins/save.bin", PSP_O_RDONLY, 0);
    if (fd < 0) return 0;
    int rd = sceIoRead(fd, &sd, sizeof(sd));
    sceIoClose(fd);
    if (rd != (int)sizeof(sd) || sd.magic != 0xAE7E2026u) return 0;
    heroLv = sd.lv; heroXp = sd.xp; heroGold = sd.gold; heroCoins = sd.coins;
    heroPotions = sd.potions; heroFood = sd.food; heroElixirs = sd.elixirs;
    heroHunger = sd.hunger;
    heroAtk = sd.atk; heroMaxHp = sd.maxHp; heroHp = heroMaxHp; forgeCount = sd.forge;
    furorCharge = sd.furor; cluesCount = sd.clues; runesLit = sd.runes; keysFound = sd.keys;
    heroRelics = sd.relics; trueEnding = sd.ending;
    memcpy(chestOpened, sd.chests, sizeof(chestOpened));
    memcpy(obeliskLit, sd.obelisks, sizeof(obeliskLit));
    memcpy(dngCleared, sd.dngs, sizeof(dngCleared));
    memcpy(clueFound, sd.clueArr, sizeof(clueFound));
    memcpy(qState, sd.qSt, sizeof(qState));
    qKillsForest = sd.qF; qKillsLake = sd.qL; qKillsCanyon = sd.qC;
    hasSave = 1;
    return 1;
}
static char bannerMsg[64] = "", dialogMsg[96] = "";
static int bannerT = 0, dialogT = 0;
static int inTitle = 1;

static void add_plat(float x, float y, float z, float w, float d, float h, int tex, int zone) {
    if (nPlats >= MAX_PLATS) return;
    plats[nPlats++] = (Platform){ x, y, z, w, d, h, tex, zone };
}

static void add_prop(float x, float y, float z, float rad, float h, int kind, int id) {
    if (nProps >= MAX_PROPS) return;
    int act = 0;
    if (kind == 2 && id >= 0 && id < 12) act = obeliskLit[id];
    if (kind == 3 && id >= 0 && id < 16) act = chestOpened[id];
    if (kind == 8 && id >= 0 && id < 6)  act = clueFound[id];
    props[nProps++] = (Prop3D){ x, y, z, rad, h, kind, act, id };
}

static void add_npc(float x, float y, float z, int role, int nid, const char *name, const char *msg) {
    if (nNpcs >= MAX_NPCS) return;
    npcs[nNpcs++] = (Npc3D){ x, y, z, x, z, role, nid, name, msg };
}

static void add_enemy(float x, float y, float z, int kind, int isBoss, int bossId) {
    if (nEnemies >= MAX_ENEMIES) return;
    static const int mobHpTbl[3]  = { 85, 210, 460 };
    static const int bossHpTbl[3] = { 650, 1250, 2600 };
    int hp = isBoss ? bossHpTbl[bossId % 3] : mobHpTbl[kind % 3];
    enemies[nEnemies++] = (Enemy3D){ x, y, z, x, y, z, hp, hp, kind, 1, 0, 0, isBoss, bossId, 0, 0, 0, x, z, isBoss ? 3.2f : 2.0f };
}

static void spawn_part(float x, float y, float z, float vx, float vy, float vz, float life, float sz, unsigned int col) {
    for (int i = 0; i < MAX_PARTS; i++) {
        if (parts[i].life <= 0.0f) {
            parts[i] = (Particle3D){ x, y, z, vx, vy, vz, life, life, sz, col };
            return;
        }
    }
}

static void add_camp(float cx, float cy, float cz, int tier, int count, int hasHouse, int chestId) {
    if (hasHouse) add_prop(cx, cy, cz, 2.0f, 2.8f, 5, 9);
    else add_prop(cx, cy, cz, 0.65f, 2.2f, 0, 0); /* Campfire / Ruin marker */
    add_prop(cx, cy, cz, 2.6f, 0.0f, 10, hasHouse ? 1 : 2); /* Camp ground decal */
    add_prop(cx - 3.8f, cy, cz - 3.2f, 0.55f, 2.6f, 0, 0);
    add_prop(cx + 3.8f, cy, cz + 3.2f, 0.55f, 2.6f, 0, 0);
    if (chestId >= 0) add_prop(cx + 2.2f, cy, cz - 2.0f, 0.6f, 0.9f, 3, chestId);
    for (int i = 0; i < count; i++) {
        float ang = ((float)i / (float)count) * 6.28318f + 0.4f;
        float rad = 4.8f + (float)(i & 1) * 1.6f;
        int k = (tier + (i == 0 && tier > 0 ? 0 : 0)) % 3;
        add_enemy(cx + cosf(ang) * rad, cy, cz + sinf(ang) * rad, k, 0, 0);
    }
}

static void build_overworld(void) {
    nPlats = 0; nProps = 0; nEnemies = 0; nNpcs = 0;
    worldMode = 0;

    /* 1. GRANDES LLANURAS CENTRALES (x=-220..+220, z=-220..+220, 440x440 continuas!) */
    add_plat(0.0f, 0.00f, 0.0f, 440.0f, 440.0f, 4.0f, 0, 0);
    add_plat(0.0f, 0.10f, 0.0f,  34.0f,  34.0f, 0.3f, 1, 0); /* Pueblo del Alba */
    add_plat(0.0f, 0.25f, 0.0f,  10.0f,  10.0f, 0.25f, 4, 0); /* Plaza del Cristal */
    add_prop(0.0f, 0.25f, 0.0f, 0.7f, 2.5f, 4, 0); /* Sanctuary Crystal (SAVE) */
    add_prop(0.0f, 0.25f, 0.0f, 3.2f, 0.0f, 10, 3);
    for (int i = 0; i < 8; i++) {
        float ang = (float)i * 0.7854f;
        add_prop(cosf(ang) * 11.5f, 0.10f, sinf(ang) * 11.5f, 1.5f, 0.0f, 10, 1);
    }
    add_prop(-11.0f, 0.10f, -9.5f, 2.2f, 3.0f, 5, 0);
    add_prop( 11.0f, 0.10f, -9.5f, 2.2f, 3.0f, 5, 1);
    add_prop(-11.0f, 0.10f,  9.5f, 2.2f, 3.0f, 5, 2);
    add_prop( 11.0f, 0.10f,  9.5f, 2.2f, 3.0f, 5, 3);
    add_prop(-13.5f, 0.10f,  0.0f, 1.1f, 3.2f, 1, 0);
    add_prop( 13.5f, 0.10f,  0.0f, 1.1f, 3.2f, 1, 0);
    add_prop( 12.5f, 0.10f, 12.5f, 0.6f, 0.9f, 3, 0);
    add_prop(-6.0f,  0.10f,  6.0f, 0.55f, 1.8f, 8, 0);
    add_npc(-3.4f, 0.25f,  2.8f, 0, 0, "Alcalde Eldon", "ALCALDE: Encuentra las 3 Llaves!");
    add_npc(-7.0f, 0.10f, -7.0f, 1, 1, "Herrero Kael",  "KAEL: Forjar Espada!");
    add_npc( 7.0f, 0.10f, -7.0f, 2, 2, "Alquimista Lyra","LYRA: Tienda de Supervivencia!");
    add_npc( 3.4f, 0.25f,  2.8f, 3, 3, "Sabio Orin",    "ORIN: Ilumina los Obeliscos!");

    /* Town Gate Lanterns & Waymarkers along the 4 open-world trails */
    add_prop(-16.0f, 0.10f, -4.5f, 0.4f, 1.8f, 11, 0); add_prop(-16.0f, 0.10f,  4.5f, 0.4f, 1.8f, 11, 0);
    add_prop( 16.0f, 0.10f, -4.5f, 0.4f, 1.8f, 11, 0); add_prop( 16.0f, 0.10f,  4.5f, 0.4f, 1.8f, 11, 0);
    add_prop(-4.5f, 0.10f, -16.0f, 0.4f, 1.8f, 11, 0); add_prop( 4.5f, 0.10f, -16.0f, 0.4f, 1.8f, 11, 0);
    add_prop(-4.5f, 0.10f,  16.0f, 0.4f, 1.8f, 11, 0); add_prop( 4.5f, 0.10f,  16.0f, 0.4f, 1.8f, 11, 0);
    for (int i = 1; i <= 4; i++) {
        float d = 40.0f + (float)i * 40.0f;
        add_prop(-d, 0.0f, -8.0f, 0.4f, 1.8f, 11, 0); add_prop( d, 0.0f,  8.0f, 0.4f, 1.8f, 11, 2);
        add_prop(-8.0f, 0.0f, -d, 0.4f, 1.8f, 11, 1); add_prop( 8.0f, 0.0f,  d, 0.4f, 1.8f, 11, 0);
    }

    /* Rolling Hills, Tree Groves & Wildflower Meadows across the 440x440 Central Plains */
    add_plat(-110.0f, 0.35f, -110.0f, 64.0f, 64.0f, 3.0f, 0, 0);
    add_plat( 110.0f, 0.35f, -110.0f, 64.0f, 64.0f, 3.0f, 0, 0);
    add_plat(-110.0f, 0.35f,  110.0f, 64.0f, 64.0f, 3.0f, 0, 0);
    add_plat( 110.0f, 0.35f,  110.0f, 64.0f, 64.0f, 3.0f, 0, 0);
    add_plat(-180.0f, 0.35f,    0.0f, 48.0f, 48.0f, 3.0f, 0, 1); /* Torre de Vigia Oeste */
    add_prop(-165.0f, 0.35f,   16.0f, 1.2f, 3.6f, 1, 0);
    add_camp(-182.0f, 0.35f,  -10.0f, 1, 3, 1, 12);
    add_plat(   0.0f, 0.35f,  180.0f, 48.0f, 48.0f, 3.0f, 0, 0); /* Torre del Sur */
    add_camp(   0.0f, 0.35f,  180.0f, 1, 4, 1, 15);

    for (int i = 0; i < 260; i++) {
        float px = -210.0f + (float)((i * 67 + 17) % 420);
        float pz = -210.0f + (float)((i * 113 + 31) % 420);
        if (fabsf(px) < 38.0f && fabsf(pz) < 38.0f) px += (px >= 0 ? 52.0f : -52.0f);
        if (fabsf(px + 110.0f) < 34.0f && fabsf(pz + 110.0f) < 34.0f) continue;
        if (fabsf(px - 110.0f) < 34.0f && fabsf(pz + 110.0f) < 34.0f) continue;
        if (fabsf(px + 110.0f) < 34.0f && fabsf(pz - 110.0f) < 34.0f) continue;
        if (fabsf(px - 110.0f) < 34.0f && fabsf(pz - 110.0f) < 34.0f) continue;
        if (fabsf(px + 180.0f) < 26.0f && fabsf(pz) < 26.0f) continue;
        if (fabsf(px) < 26.0f && fabsf(pz - 180.0f) < 26.0f) continue;
        int m = i % 5;
        if (m < 2) add_prop(px, 0.0f, pz, 1.2f, 3.3f + (float)(i % 3) * 0.5f, 1, 0); /* Tree */
        else if (m == 2) add_prop(px, 0.0f, pz, 0.9f, 1.45f, 12, 0); /* Bush */
        else if (m == 3) add_prop(px, 0.0f, pz, 0.7f, 0.85f, 14, 0); /* Grass tuft */
        else add_prop(px, 0.0f, pz, 0.55f + (float)(i % 3) * 0.15f, 0.75f + (float)(i % 2) * 0.4f, 13, 0); /* Rock */
        if ((i % 2) == 0) add_prop(px + 1.8f, 0.0f, pz + 1.4f, 1.8f, 0.0f, 10, (i % 4 == 0) ? 2 : 0);
    }
    add_camp(-110.0f, 0.35f, -110.0f, 0, 3, 0, -1);
    add_camp( 110.0f, 0.35f, -110.0f, 0, 3, 0, -1);
    add_camp(-110.0f, 0.35f,  110.0f, 0, 3, 0, -1);
    add_camp( 110.0f, 0.35f,  110.0f, 1, 3, 0, -1);
    add_camp( -65.0f, 0.00f,  -45.0f, 0, 2, 0, -1);
    add_camp(  65.0f, 0.00f,   45.0f, 0, 2, 0, -1);

    /* 2. GRAN BOSQUE ESMERALDA (West: x=-560..-220, z=-220..+220, 340x440) */
    add_plat(-390.0f, 0.00f, 0.0f, 340.0f, 440.0f, 4.0f, 0, 1);
    for (int i = 0; i < 340; i++) {
        float tx = -228.0f - (float)((i * 53 + 11) % 324);
        float tz = -212.0f + (float)((i * 89 + 23) % 424);
        if (fabsf(tx + 390.0f) < 12.0f && fabsf(tz) < 12.0f) tz += 18.0f;
        if (fabsf(tx + 468.0f) < 14.0f && fabsf(tz) < 14.0f) tz += 18.0f;
        int m = i % 6;
        if (m < 3) add_prop(tx, 0.0f, tz, 1.25f, 3.6f + (float)(i % 3) * 0.55f, 1, 0); /* Tree */
        else if (m == 3) add_prop(tx, 0.0f, tz, 0.9f, 1.55f, 12, 0); /* Bush */
        else if (m == 4) add_prop(tx, 0.0f, tz, 0.7f, 0.85f, 14, 0); /* Grass */
        else add_prop(tx, 0.0f, tz, 0.6f, 0.9f, 13, 0); /* Mossy Rock */
        if ((i % 2) == 0) add_prop(tx + 1.8f, 0.0f, tz + 1.6f, 1.9f, 0.0f, 10, (i % 4 == 0) ? 2 : 0);
    }
    add_prop(-390.0f, 0.0f,   0.0f, 0.7f, 3.2f, 2, 0); /* Obelisk 0 */
    add_prop(-390.0f, 0.0f,   0.0f, 3.2f, 0.0f, 10, 3);
    add_prop(-455.0f, 0.0f,  90.0f, 0.6f, 0.9f, 3, 1); /* Chest 1 */
    add_prop(-455.0f, 0.0f, -90.0f, 0.6f, 0.9f, 3, 2); /* Chest 2 */
    add_prop(-345.0f, 0.0f,  45.0f, 0.55f, 1.8f, 8, 1); /* Clue 1 */
    add_prop(-468.0f, 0.0f,   0.0f, 1.2f, 3.6f, 6, 0); /* DUNGEON 0 PORTAL */
    add_prop(-462.0f, 0.0f,  -5.0f, 0.4f, 1.8f, 11, 1);
    add_prop(-462.0f, 0.0f,   5.0f, 0.4f, 1.8f, 11, 1);
    add_camp(-320.0f, 0.0f, -110.0f, 1, 4, 1, -1);
    add_camp(-340.0f, 0.0f,  120.0f, 1, 4, 1, -1);
    add_camp(-440.0f, 0.0f,  -65.0f, 1, 4, 0, -1);
    add_camp(-495.0f, 0.0f,   55.0f, 1, 4, 0, -1);

    /* 3. TIERRAS DEL NORTE & LAGO DE LOS ECOS (North: x=-220..+220, z=-480..-220, 440x260) */
    add_plat(0.0f, 0.35f, -350.0f, 440.0f, 260.0f, 4.5f, 1, 1);
    add_prop(-22.0f, 0.35f, -305.0f, 2.0f, 2.8f, 5, 4);
    add_prop( 22.0f, 0.35f, -305.0f, 2.0f, 2.8f, 5, 5);
    add_prop(  0.0f, 0.35f, -315.0f, 0.7f, 2.5f, 4, 1); /* Lake Crystal */
    add_prop(  0.0f, 0.35f, -315.0f, 3.0f, 0.0f, 10, 3);
    add_prop(-12.0f, 0.35f, -285.0f, 0.4f, 1.8f, 11, 1);
    add_prop( 12.0f, 0.35f, -285.0f, 0.4f, 1.8f, 11, 1);
    add_prop( 14.0f, 0.35f, -298.0f, 1.0f, 3.0f, 9, 0); /* Paid Return Portal */
    add_prop(-68.0f, 0.35f, -375.0f, 0.7f, 3.0f, 2, 1); /* Obelisk 1 */
    add_prop(-68.0f, 0.35f, -375.0f, 2.6f, 0.0f, 10, 3);
    add_prop( 82.0f, 0.35f, -380.0f, 0.6f, 0.9f, 3, 3); /* Chest 3 */
    add_prop( 26.0f, 0.35f, -335.0f, 0.55f, 1.8f, 8, 3); /* Clue 3 */
    add_prop(  0.0f, 0.35f, -410.0f, 1.2f, 3.6f, 6, 1); /* DUNGEON 1 PORTAL */
    add_npc(-14.0f, 0.35f, -298.0f, 2, 4, "Mercader Nilo", "NILO: Tienda de Supervivencia!");
    for (int i = 0; i < 120; i++) {
        float lx = -210.0f + (float)((i * 71 + 13) % 420);
        float lz = -470.0f + (float)((i * 59 + 19) % 235);
        if (fabsf(lx) < 42.0f && fabsf(lz + 330.0f) < 65.0f) lx += (lx >= 0 ? 60.0f : -60.0f);
        int m = i % 4;
        if (m == 0) add_prop(lx, 0.35f, lz, 1.2f, 3.7f, 1, 0);
        else if (m == 1) add_prop(lx, 0.35f, lz, 0.9f, 1.45f, 12, 0);
        else if (m == 2) add_prop(lx, 0.35f, lz, 0.7f, 0.85f, 14, 0);
        else add_prop(lx, 0.35f, lz, 0.65f, 0.95f, 13, 0);
        if ((i % 3) == 0) add_prop(lx + 1.8f, 0.35f, lz + 1.5f, 1.8f, 0.0f, 10, 0);
    }
    add_camp(-115.0f, 0.35f, -345.0f, 1, 4, 1, 13);
    add_camp( 115.0f, 0.35f, -355.0f, 2, 4, 0, -1);

    /* 4. CANON CARMESI & TIERRAS ROJAS (East: x=+220..+560, z=-220..+220, 340x440) */
    add_plat(390.0f, 0.35f, 0.0f, 340.0f, 440.0f, 4.5f, 2, 2);
    add_prop(325.0f, 0.35f, -22.0f, 2.0f, 2.8f, 5, 6); /* Mining Outpost */
    add_prop(336.0f, 0.35f, -12.0f, 1.0f, 3.0f, 9, 1); /* Paid Return Portal */
    add_prop(390.0f, 0.35f, -82.0f, 0.7f, 3.2f, 2, 2); /* Obelisk 2 */
    add_prop(390.0f, 0.35f, -82.0f, 2.8f, 0.0f, 10, 3);
    add_prop(465.0f, 0.35f,  95.0f, 0.6f, 0.9f, 3, 4); /* Chest 4 */
    add_prop(468.0f, 0.35f,   0.0f, 1.2f, 3.6f, 6, 2); /* DUNGEON 2 PORTAL */
    add_prop(462.0f, 0.35f,  -5.0f, 0.4f, 1.8f, 11, 2);
    add_prop(462.0f, 0.35f,   5.0f, 0.4f, 1.8f, 11, 2);
    add_npc(325.0f, 0.35f, -12.0f, 1, 5, "Forjador Vulcan", "VULCAN: Forjar Espada!");
    for (int i = 0; i < 95; i++) {
        float rx = 240.0f + (float)((i * 53 + 7) % 300);
        float rz = -205.0f + (float)((i * 89 + 13) % 410);
        if (fabsf(rx - 325.0f) < 30.0f && fabsf(rz + 12.0f) < 30.0f) continue;
        if ((i % 4) == 0) add_prop(rx, 0.35f, rz, 0.75f, 4.0f, 0, 0);
        else add_prop(rx, 0.35f, rz, 0.7f + (float)(i % 3) * 0.2f, 0.9f + (float)(i % 2) * 0.5f, 13, 0);
    }
    add_camp(395.0f, 0.35f, -120.0f, 2, 4, 1, 14);
    add_camp(410.0f, 0.35f,  120.0f, 2, 4, 1, -1);
    add_camp(480.0f, 0.35f,  -55.0f, 2, 4, 0, -1);

    /* 5. ESTEPAS DEL SUR & BASTION ANTIGUO (South: x=-220..+220, z=+220..+480, 440x260) */
    add_plat(0.0f, 0.00f, 350.0f, 440.0f, 260.0f, 4.0f, 0, 0);
    add_plat(0.0f, 0.35f, 375.0f, 130.0f, 110.0f, 3.5f, 1, 2); /* BASTION DEL SUR */
    add_prop(0.0f, 0.35f, 375.0f, 0.7f, 3.4f, 2, 5); /* Obelisk 5 */
    add_prop(0.0f, 0.35f, 375.0f, 3.2f, 0.0f, 10, 3);
    add_prop(24.0f, 0.35f, 340.0f, 1.0f, 3.0f, 9, 2); /* Paid Return Portal */
    for (int i = 0; i < 140; i++) {
        float sx = -210.0f + (float)((i * 61 + 9) % 420);
        float sz =  230.0f + (float)((i * 47 + 17) % 235);
        if (fabsf(sx) < 70.0f && fabsf(sz - 375.0f) < 60.0f) continue;
        int m = i % 4;
        if (m == 0) add_prop(sx, 0.0f, sz, 1.2f, 3.6f, 1, 0);
        else if (m == 1) add_prop(sx, 0.0f, sz, 0.9f, 1.45f, 12, 0);
        else if (m == 2) add_prop(sx, 0.0f, sz, 0.7f, 0.85f, 14, 0);
        else add_prop(sx, 0.0f, sz, 0.65f, 0.9f, 13, 0);
        if ((i % 2) == 0) add_prop(sx + 1.8f, 0.0f, sz + 1.5f, 1.8f, 0.0f, 10, (i & 1) ? 0 : 2);
    }
    add_camp(-120.0f, 0.00f, 350.0f, 1, 4, 1, -1);
    add_camp( 120.0f, 0.00f, 350.0f, 2, 4, 1, -1);

    /* 6. CUATRO REGIONES DIAGONALES (Cero vacio en todo el continente!) */
    add_plat(-390.0f, 0.35f, -350.0f, 340.0f, 260.0f, 4.5f, 0, 1); /* Bosque Brumoso NW */
    for (int i = 0; i < 130; i++) {
        float bx = -230.0f - (float)((i * 43 + 5) % 315);
        float bz = -230.0f - (float)((i * 61 + 11) % 235);
        int m = i % 4;
        if (m < 2) add_prop(bx, 0.35f, bz, 1.25f, 3.9f, 1, 0);
        else if (m == 2) add_prop(bx, 0.35f, bz, 0.9f, 1.55f, 12, 0);
        else add_prop(bx, 0.35f, bz, 0.65f, 0.85f, 14, 0);
        if ((i % 2) == 0) add_prop(bx + 1.8f, 0.35f, bz + 1.6f, 1.8f, 0.0f, 10, 2);
    }
    add_camp(-380.0f, 0.35f, -340.0f, 1, 4, 1, -1);
    add_plat( 390.0f, 0.35f, -350.0f, 340.0f, 260.0f, 4.5f, 2, 2); /* Paramos de Obsidiana NE */
    for (int i = 0; i < 45; i++) {
        float nx = 240.0f + (float)((i * 47) % 300);
        float nz = -240.0f - (float)((i * 53) % 220);
        add_prop(nx, 0.35f, nz, 0.7f, 1.1f, 13, 0);
    }
    add_camp( 380.0f, 0.35f, -340.0f, 2, 4, 0, -1);
    add_plat(-390.0f, 0.00f,  350.0f, 340.0f, 260.0f, 4.0f, 0, 0); /* Praderas SW */
    for (int i = 0; i < 120; i++) {
        float wx = -230.0f - (float)((i * 59 + 7) % 315);
        float wz =  230.0f + (float)((i * 47 + 13) % 235);
        int m = i % 4;
        if (m == 0) add_prop(wx, 0.0f, wz, 1.2f, 3.7f, 1, 0);
        else if (m == 1) add_prop(wx, 0.0f, wz, 0.9f, 1.45f, 12, 0);
        else if (m == 2) add_prop(wx, 0.0f, wz, 0.7f, 0.85f, 14, 0);
        else add_prop(wx, 0.0f, wz, 0.6f, 0.9f, 13, 0);
        if ((i % 2) == 0) add_prop(wx + 1.8f, 0.0f, wz + 1.6f, 1.8f, 0.0f, 10, 0);
    }
    add_camp(-380.0f, 0.0f, 340.0f, 1, 4, 1, -1);
    add_plat( 390.0f, 0.35f,  350.0f, 340.0f, 260.0f, 4.5f, 2, 2); /* Tierras Aridas SE */
    add_camp( 380.0f, 0.35f, 340.0f, 2, 4, 0, -1);

    /* 7. CIUDADELA ASTRAL (Lejano Norte: z=-590..-480) */
    add_plat(0.0f, 0.75f, -535.0f, 160.0f, 110.0f, 5.0f, 4, 3);
    add_prop(-28.0f, 0.75f, -505.0f, 0.7f, 4.4f, 2, 3); /* Obelisk 3 */
    add_prop( 28.0f, 0.75f, -505.0f, 0.7f, 4.4f, 2, 4); /* Obelisk 4 */
    add_prop(-28.0f, 0.75f, -560.0f, 0.7f, 4.4f, 0, 0);
    add_prop( 28.0f, 0.75f, -560.0f, 0.7f, 4.4f, 0, 0);
    add_prop(  0.0f, 0.75f, -575.0f, 0.7f, 2.8f, 4, 2); /* Astral Crystal */
    add_prop(  0.0f, 0.75f, -540.0f, 4.2f, 0.0f, 10, 3); /* Astral Dragon Sigil */
    add_prop(-28.0f, 0.75f, -505.0f, 2.4f, 0.0f, 10, 3);
    add_prop( 28.0f, 0.75f, -505.0f, 2.4f, 0.0f, 10, 3);
    add_prop(  0.0f, 0.75f, -500.0f, 0.6f, 0.9f, 3, 5); /* Chest 5 */
    add_enemy( 0.0f, 0.75f, -540.0f, 2, 1, 2); /* Final Sky Dragon Boss */
}

static void build_dungeon(int dId) {
    nPlats = 0; nProps = 0; nEnemies = 0; nNpcs = 0;
    worldMode = 1;
    curDungeon = dId;
    dngRunes = 0;
    int tex = (dId == 0) ? 1 : ((dId == 1) ? 4 : 2);

    /* Entrance Hall -> Central Chamber -> Boss Sanctum */
    add_plat(0.0f, 0.0f,   0.0f, 12.0f, 12.0f, 2.0f, tex, dId + 1);
    add_plat(0.0f, 0.0f, -12.0f,  5.0f, 12.0f, 2.0f, tex, dId + 1);
    add_plat(0.0f, 0.0f, -26.0f, 18.0f, 16.0f, 2.0f, tex, dId + 1);
    add_plat(0.0f, 0.0f, -40.0f,  5.0f, 12.0f, 2.0f, tex, dId + 1);
    add_plat(0.0f, 0.2f, -56.0f, 20.0f, 20.0f, 2.2f, 4,   3);

    add_prop( 0.0f, 0.0f,  3.5f, 1.0f, 3.0f, 7, 0); /* Exit Portal */
    add_prop( 0.0f, 0.0f, -26.0f, 3.5f, 0.0f, 10, 3); /* Dungeon Rune Sigil */
    add_prop( 0.0f, 0.2f, -57.0f, 3.8f, 0.0f, 10, 3); /* Boss Arena Sigil */
    add_prop(-6.5f, 0.0f, -26.0f, 0.7f, 3.0f, 2, 6 + dId * 2);     /* Dungeon Rune 1 */
    add_prop( 6.5f, 0.0f, -26.0f, 0.7f, 3.0f, 2, 6 + dId * 2 + 1); /* Dungeon Rune 2 */
    add_prop(-7.5f, 0.0f, -31.0f, 0.6f, 0.9f, 3, 6 + dId * 2);     /* Dungeon Chest 1 */
    add_prop( 7.5f, 0.0f, -21.0f, 0.6f, 0.9f, 3, 6 + dId * 2 + 1); /* Dungeon Chest 2 */
    if (dId == 0) add_prop(0.0f, 0.0f, -18.0f, 0.55f, 1.8f, 8, 2); /* Clue 2 */
    if (dId == 1) add_prop(0.0f, 0.0f, -18.0f, 0.55f, 1.8f, 8, 4); /* Clue 4 */
    if (dId == 2) add_prop(0.0f, 0.0f, -18.0f, 0.55f, 1.8f, 8, 5); /* Clue 5 */

    add_prop(-7.0f, 0.2f, -50.0f, 0.6f, 4.0f, 0, 0);
    add_prop( 7.0f, 0.2f, -50.0f, 0.6f, 4.0f, 0, 0);
    add_prop(-7.0f, 0.2f, -62.0f, 0.6f, 4.0f, 0, 0);
    add_prop( 7.0f, 0.2f, -62.0f, 0.6f, 4.0f, 0, 0);

    add_enemy(-3.5f, 0.0f, -24.0f, dId % 3, 0, 0);
    add_enemy( 3.5f, 0.0f, -28.0f, (dId + 1) % 3, 0, 0);
    if (!dngCleared[dId]) {
        add_enemy(0.0f, 0.2f, -57.0f, dId, 1, dId); /* Dungeon Boss */
    } else {
        add_prop(0.0f, 0.2f, -57.0f, 1.0f, 3.0f, 7, 1); /* Shortcut Exit */
    }

    heroX = 0.0f; heroY = 0.0f; heroZ = 0.5f;
}

static int get_current_zone(void) {
    if (worldMode == 1) return (curDungeon + 1) & 3;
    if (heroZ < -420.0f) return 3;
    if (heroX > 140.0f) return 2;
    if (heroZ < -140.0f) return 1;
    if (heroX < -140.0f) return 1;
    return 0;
}

static int is_walkable(float nx, float nz, float *outY) {
    int found = 0;
    float bestY = -100.0f;
    for (int i = 0; i < nPlats; i++) {
        Platform *p = &plats[i];
        if (nx >= p->x - p->w * 0.5f - 0.15f && nx <= p->x + p->w * 0.5f + 0.15f &&
            nz >= p->z - p->d * 0.5f - 0.15f && nz <= p->z + p->d * 0.5f + 0.15f) {
            if (!found || p->y > bestY) { bestY = p->y; found = 1; }
        }
    }
    if (found) *outY = bestY;
    return found;
}

static unsigned int lerp_abgr(unsigned int a, unsigned int b, float t) {
    int ar = a & 0xFF, ag = (a >> 8) & 0xFF, ab = (a >> 16) & 0xFF;
    int br = b & 0xFF, bg = (b >> 8) & 0xFF, bb = (b >> 16) & 0xFF;
    int rr = ar + (int)((br - ar) * t);
    int rg = ag + (int)((bg - ag) * t);
    int rb = ab + (int)((bb - ab) * t);
    return ABGR(rr, rg, rb, 255);
}

static const void *curTex = NULL;
static float curCamCos = 1.0f, curCamSin = 0.0f;

static void bind_tex64(const unsigned int *tex, int wrapRepeat, int linear) {
    sceGuEnable(GU_TEXTURE_2D);
    if (curTex == tex) return;
    curTex = tex;
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, 64, 64, 64, tex);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(linear ? GU_LINEAR : GU_NEAREST, linear ? GU_LINEAR : GU_NEAREST);
    sceGuTexWrap(wrapRepeat ? GU_REPEAT : GU_CLAMP, wrapRepeat ? GU_REPEAT : GU_CLAMP);
}

static unsigned int shade_vtx(float vx, float vy, float vz, unsigned int baseCol) {
    int br = baseCol & 0xFF, bg = (baseCol >> 8) & 0xFF, bb = (baseCol >> 16) & 0xFF;
    float dx = vx - heroX, dz = vz - heroZ;
    float d2 = dx * dx + dz * dz;
    int addR = 0, addG = 0, addB = 0;
    if (d2 < 36.0f) {
        float w = (36.0f - d2) * 0.027778f;
        addR += (int)(w * 54.0f); addG += (int)(w * 48.0f); addB += (int)(w * 30.0f);
    }
    if (pulseT > 0 && d2 < 256.0f) {
        float pr = (float)(28 - pulseT) * 0.45f;
        float pd = fabsf(sqrtf(d2) - pr);
        if (pd < 2.0f) { int pw = (int)((2.0f - pd) * 60.0f); addG += pw; addB += pw; }
    }
    int r = br + addR; if (r > 255) r = 255;
    int g = bg + addG; if (g > 255) g = 255;
    int b = bb + addB; if (b > 255) b = 255;
    return ABGR(r, g, b, (baseCol >> 24) & 0xFF);
}

static void draw_box3d(float cx, float cy, float cz, float w, float h, float d, const unsigned int *tex, unsigned int col, float uvScale) {
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST);
    bind_tex64(tex, 1, 0);

    float hw = w * 0.5f, hd = d * 0.5f, y0 = cy - h, y1 = cy;
    unsigned int cSide = ABGR(((col & 0xFF) * 7) / 10, (((col >> 8) & 0xFF) * 7) / 10, (((col >> 16) & 0xFF) * 7) / 10, 255);
    unsigned int cDark = ABGR(((col & 0xFF) * 5) / 10, (((col >> 8) & 0xFF) * 5) / 10, (((col >> 16) & 0xFF) * 5) / 10, 255);

    int nx = (w > 12.0f) ? ((int)(w / 12.0f) + 1) : 1;
    int nz = (d > 12.0f) ? ((int)(d / 12.0f) + 1) : 1;
    float stepX = w / (float)nx, stepZ = d / (float)nz;
    float xStart = cx - hw, zStart = cz - hd;
    float uH = h * uvScale;

    int ix0 = (int)((heroX - 62.0f - xStart) / stepX); if (ix0 < 0) ix0 = 0;
    int ix1 = (int)((heroX + 62.0f - xStart) / stepX); if (ix1 >= nx) ix1 = nx - 1;
    int iz0 = (int)((heroZ - 62.0f - zStart) / stepZ); if (iz0 < 0) iz0 = 0;
    int iz1 = (int)((heroZ + 62.0f - zStart) / stepZ); if (iz1 >= nz) iz1 = nz - 1;
    if (ix0 > ix1 || iz0 > iz1) {
        sceGuEnable(GU_BLEND);
        sceGuEnable(GU_ALPHA_TEST);
        return;
    }

    int visX = ix1 - ix0 + 1, visZ = iz1 - iz0 + 1;
    int maxTris = visX * visZ * 2 + (visX + visZ) * 4;
    Vtx3D *v = (Vtx3D *)sceGuGetMemory(maxTris * 3 * sizeof(Vtx3D));
    int k = 0;

    for (int iz = iz0; iz <= iz1; iz++) {
        float tz0 = zStart + (float)iz * stepZ, tz1 = tz0 + stepZ;
        float v0 = (float)iz * stepZ * uvScale, v1 = (float)(iz + 1) * stepZ * uvScale;
        for (int ix = ix0; ix <= ix1; ix++) {
            float tx0 = xStart + (float)ix * stepX, tx1 = tx0 + stepX;
            float u0 = (float)ix * stepX * uvScale, u1 = (float)(ix + 1) * stepX * uvScale;
            unsigned int c00 = shade_vtx(tx0, y1, tz0, col);
            unsigned int c10 = shade_vtx(tx1, y1, tz0, col);
            unsigned int c11 = shade_vtx(tx1, y1, tz1, col);
            unsigned int c01 = shade_vtx(tx0, y1, tz1, col);
            v[k++] = (Vtx3D){ u0, v0, c00, tx0, y1, tz0 };
            v[k++] = (Vtx3D){ u1, v0, c10, tx1, y1, tz0 };
            v[k++] = (Vtx3D){ u1, v1, c11, tx1, y1, tz1 };
            v[k++] = (Vtx3D){ u0, v0, c00, tx0, y1, tz0 };
            v[k++] = (Vtx3D){ u1, v1, c11, tx1, y1, tz1 };
            v[k++] = (Vtx3D){ u0, v1, c01, tx0, y1, tz1 };

            if (iz == nz - 1) {
                v[k++] = (Vtx3D){ u0, 0, cSide, tx0, y1, tz1 }; v[k++] = (Vtx3D){ u1, 0, cSide, tx1, y1, tz1 }; v[k++] = (Vtx3D){ u1, uH, cDark, tx1, y0, tz1 };
                v[k++] = (Vtx3D){ u0, 0, cSide, tx0, y1, tz1 }; v[k++] = (Vtx3D){ u1, uH, cDark, tx1, y0, tz1 }; v[k++] = (Vtx3D){ u0, uH, cDark, tx0, y0, tz1 };
            }
            if (iz == 0) {
                v[k++] = (Vtx3D){ u1, 0, cSide, tx1, y1, tz0 }; v[k++] = (Vtx3D){ u0, 0, cSide, tx0, y1, tz0 }; v[k++] = (Vtx3D){ u0, uH, cDark, tx0, y0, tz0 };
                v[k++] = (Vtx3D){ u1, 0, cSide, tx1, y1, tz0 }; v[k++] = (Vtx3D){ u0, uH, cDark, tx0, y0, tz0 }; v[k++] = (Vtx3D){ u1, uH, cDark, tx1, y0, tz0 };
            }
            if (ix == 0) {
                v[k++] = (Vtx3D){ v0, 0, cSide, tx0, y1, tz0 }; v[k++] = (Vtx3D){ v1, 0, cSide, tx0, y1, tz1 }; v[k++] = (Vtx3D){ v1, uH, cDark, tx0, y0, tz1 };
                v[k++] = (Vtx3D){ v0, 0, cSide, tx0, y1, tz0 }; v[k++] = (Vtx3D){ v1, uH, cDark, tx0, y0, tz1 }; v[k++] = (Vtx3D){ v0, uH, cDark, tx0, y0, tz0 };
            }
            if (ix == nx - 1) {
                v[k++] = (Vtx3D){ v1, 0, cSide, tx1, y1, tz1 }; v[k++] = (Vtx3D){ v0, 0, cSide, tx1, y1, tz0 }; v[k++] = (Vtx3D){ v0, uH, cDark, tx1, y0, tz0 };
                v[k++] = (Vtx3D){ v1, 0, cSide, tx1, y1, tz1 }; v[k++] = (Vtx3D){ v0, uH, cDark, tx1, y0, tz0 }; v[k++] = (Vtx3D){ v1, uH, cDark, tx1, y0, tz1 };
            }
        }
    }
    if (k > 0) sceGumDrawArray(GU_TRIANGLES, VTYPE_3D, k, 0, v);
    sceGuEnable(GU_BLEND);
    sceGuEnable(GU_ALPHA_TEST);
}

static void draw_house3d(float cx, float cy, float cz) {
    /* Walls + Roof + Warm Door */
    draw_box3d(cx, cy + 2.2f, cz, 3.4f, 2.2f, 3.0f, texStone, ABGR(240, 232, 215, 255), 0.5f);
    draw_box3d(cx, cy + 3.1f, cz, 3.9f, 0.9f, 3.5f, texObsidian, ABGR(220, 120, 95, 255), 0.5f);
    draw_box3d(cx, cy + 3.7f, cz, 2.4f, 0.6f, 2.1f, texObsidian, ABGR(240, 140, 105, 255), 0.5f);
    draw_box3d(cx, cy + 1.4f, cz + 1.52f, 0.9f, 1.4f, 0.1f, texWood, ABGR(255, 215, 130, 255), 1.0f);
}

static void draw_billboard(float x, float y, float z, float w, float h, const unsigned int *tex, unsigned int col, int flipX, float camAngle) {
    (void)camAngle;
    bind_tex64(tex, 0, 0);

    float rx = curCamCos * (w * 0.5f), rz = -curCamSin * (w * 0.5f);
    float u0 = flipX ? 1.0f : 0.0f, u1 = flipX ? 0.0f : 1.0f;
    Vtx3D *v = (Vtx3D *)sceGuGetMemory(6 * sizeof(Vtx3D));
    v[0] = (Vtx3D){ u0, 0.0f, col, x - rx, y + h, z - rz };
    v[1] = (Vtx3D){ u1, 0.0f, col, x + rx, y + h, z + rz };
    v[2] = (Vtx3D){ u1, 1.0f, col, x + rx, y,     z + rz };
    v[3] = (Vtx3D){ u0, 0.0f, col, x - rx, y + h, z - rz };
    v[4] = (Vtx3D){ u1, 1.0f, col, x + rx, y,     z + rz };
    v[5] = (Vtx3D){ u0, 1.0f, col, x - rx, y,     z - rz };
    sceGumDrawArray(GU_TRIANGLES, VTYPE_3D, 6, 0, v);
}

static void draw_ground_disc(float x, float y, float z, float r, unsigned int col) {
    bind_tex64(texSoftGlow, 0, 1);

    Vtx3D *v = (Vtx3D *)sceGuGetMemory(6 * sizeof(Vtx3D));
    v[0] = (Vtx3D){ 0, 0, col, x - r, y + 0.03f, z - r };
    v[1] = (Vtx3D){ 1, 0, col, x + r, y + 0.03f, z - r };
    v[2] = (Vtx3D){ 1, 1, col, x + r, y + 0.03f, z + r };
    v[3] = (Vtx3D){ 0, 0, col, x - r, y + 0.03f, z - r };
    v[4] = (Vtx3D){ 1, 1, col, x + r, y + 0.03f, z + r };
    v[5] = (Vtx3D){ 0, 1, col, x - r, y + 0.03f, z + r };
    sceGumDrawArray(GU_TRIANGLES, VTYPE_3D, 6, 0, v);
}

static void draw_ground_decal(float x, float y, float z, float r, int decoId) {
    const unsigned int *dtex = texDeco[decoId & 3];
    sceGuEnable(GU_TEXTURE_2D);
    if (curTex != dtex) {
        curTex = dtex;
        sceGuTexMode(GU_PSM_8888, 0, 0, 0);
        sceGuTexImage(0, 32, 32, 32, dtex);
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
        sceGuTexFilter(GU_NEAREST, GU_NEAREST);
        sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    }

    sceGuDepthMask(GU_TRUE);
    Vtx3D *v = (Vtx3D *)sceGuGetMemory(6 * sizeof(Vtx3D));
    unsigned int col = ABGR(255, 255, 255, 245);
    v[0] = (Vtx3D){ 0, 0, col, x - r, y + 0.035f, z - r };
    v[1] = (Vtx3D){ 1, 0, col, x + r, y + 0.035f, z - r };
    v[2] = (Vtx3D){ 1, 1, col, x + r, y + 0.035f, z + r };
    v[3] = (Vtx3D){ 0, 0, col, x - r, y + 0.035f, z - r };
    v[4] = (Vtx3D){ 1, 1, col, x + r, y + 0.035f, z + r };
    v[5] = (Vtx3D){ 0, 1, col, x - r, y + 0.035f, z + r };
    sceGumDrawArray(GU_TRIANGLES, VTYPE_3D, 6, 0, v);
    sceGuDepthMask(GU_FALSE);
}

int main(void) {
    scePowerSetClockFrequency(333, 333, 166);
    int th = sceKernelCreateThread("cb", cb_th, 0x11, 0xFA0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    build_textures();
    load_game();
    build_overworld();

    sceGuInit();
    sceGuStart(GU_DIRECT, gulist);
    sceGuDrawBuffer(GU_PSM_8888, (void *)VRAM_DRAW_OFF, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)VRAM_DISP_OFF, BUF_W);
    sceGuDepthBuffer((void *)VRAM_ZBUF_OFF, BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuFrontFace(GU_CW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuAlphaFunc(GU_GREATER, 0x18, 0xFF);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    ach = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1024, PSP_AUDIO_FORMAT_STEREO);
    if (ach >= 0) {
        arun = 1;
        ath = sceKernelCreateThread("ae_aud", audio_th, 0x12, 0x10000, 0, NULL);
        if (ath >= 0) sceKernelStartThread(ath, 0, NULL);
    }

    SceCtrlData pad, oldPad;
    memset(&oldPad, 0, sizeof(oldPad));
    int animFrame = 0, curFps = 60, fpsFrames = 0;
    SceInt64 fpsLastT = sceKernelGetSystemTimeWide();
    int prevZone = -1;

    while (running) {
        SceInt64 frameStart = sceKernelGetSystemTimeWide();
        sceCtrlPeekBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~oldPad.Buttons;
        oldPad = pad;
        animFrame++;

        if (inTitle) {
            camYaw += 0.004f;
            if (pressed & PSP_CTRL_CROSS) {
                inTitle = 0;
                play_sfx(880.0f, 1800);
                if (!hasSave) {
                    show_story("EL SILENCIO DE LA BOVEDA",
                        "Hace 3 dias la Gran Campana del Cielo callo.",
                        "Las islas se hunden y el Arconte Vaelen desaparecio.",
                        "Investiga las 6 Pistas (O) y busca las 3 Llaves.");
                } else {
                    snprintf(bannerMsg, sizeof(bannerMsg), "PARTIDA CARGADA - PUEBLO DEL ALBA (NV.%d)", heroLv);
                    bannerT = 140;
                }
            } else if (pressed & PSP_CTRL_START) {
                inTitle = 0;
                heroLv = 1; heroXp = 0; heroGold = 25; heroCoins = 30;
                heroPotions = 2; heroFood = 2; heroElixirs = 1; heroHunger = 100.0f;
                shopOpen = 0; hungerTick = 0;
                heroAtk = 12; heroMaxHp = 100; heroHp = 100; forgeCount = 0;
                furorCharge = 40; cluesCount = 0; runesLit = 0; keysFound = 0;
                heroRelics = 0; trueEnding = 0;
                memset(chestOpened, 0, sizeof(chestOpened));
                memset(obeliskLit, 0, sizeof(obeliskLit));
                memset(dngCleared, 0, sizeof(dngCleared));
                memset(clueFound, 0, sizeof(clueFound));
                memset(qState, 0, sizeof(qState));
                qKillsForest = 0; qKillsLake = 0; qKillsCanyon = 0;
                build_overworld();
                play_sfx(880.0f, 1800);
                show_story("EL SILENCIO DE LA BOVEDA",
                    "Hace 3 dias la Gran Campana del Cielo callo.",
                    "Las islas se hunden y el Arconte Vaelen desaparecio.",
                    "Solo puedes guardar en el Cristal de Pueblo del Alba!");
            }
        } else if (storyOpen) {
            if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_CIRCLE | PSP_CTRL_SELECT)) {
                storyOpen = 0;
                play_sfx(520.0f, 600);
            }
        } else if (shopOpen) {
            if (pressed & PSP_CTRL_UP) { shopCursor = (shopCursor + 3) & 3; play_sfx(520.0f, 400); }
            if (pressed & PSP_CTRL_DOWN) { shopCursor = (shopCursor + 1) & 3; play_sfx(520.0f, 400); }
            if (pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_START)) { shopOpen = 0; play_sfx(420.0f, 500); }
            if (pressed & PSP_CTRL_CROSS) {
                static const int sPrices[4] = { 10, 18, 22, 35 };
                int p = sPrices[shopCursor];
                if (heroCoins >= p) {
                    heroCoins -= p;
                    if (shopCursor == 0) { heroFood++; snprintf(dialogMsg, sizeof(dialogMsg), "COMPRADO: RACION DE VIAJE (Tienes %d)", heroFood); }
                    else if (shopCursor == 1) { heroPotions++; snprintf(dialogMsg, sizeof(dialogMsg), "COMPRADO: POCION CURATIVA (Tienes %d)", heroPotions); }
                    else if (shopCursor == 2) { heroElixirs++; snprintf(dialogMsg, sizeof(dialogMsg), "COMPRADO: ELIXIR ESTAMINA (Tienes %d)", heroElixirs); }
                    else {
                        heroFood += 2; heroPotions++; heroHunger += 25.0f;
                        if (heroHunger > 100.0f) heroHunger = 100.0f;
                        snprintf(dialogMsg, sizeof(dialogMsg), "COMPRADO: PACK SUPERVIVENCIA (+2 Raciones, +1 Pocion)");
                    }
                    dialogT = 120;
                    play_sfx(960.0f, 1300);
                } else {
                    snprintf(dialogMsg, sizeof(dialogMsg), "MONEDAS INSUFICIENTES! Necesitas %d Monedas.", p);
                    dialogT = 100;
                    play_sfx(180.0f, 800);
                }
            }
        } else if (ghActive) {
            /* GUITAR HERO CRITICAL SOLO: Power up massive AoE damage! */
            if (ghJudgeT > 0) ghJudgeT--;
            if (ghSpawned < 8) {
                ghSpawnT--;
                if (ghSpawnT <= 0) {
                    ghSpawnT = 18;
                    for (int i = 0; i < GH_MAX; i++) {
                        if (!ghNotes[i].active) {
                            ghNotes[i].active = 1;
                            ghNotes[i].lane = (int)(xr() & 3);
                            ghNotes[i].y = 54.0f;
                            ghNotes[i].spd = 3.2f;
                            ghSpawned++;
                            break;
                        }
                    }
                }
            }
            static const unsigned int ghBtns[4] = { PSP_CTRL_SQUARE, PSP_CTRL_TRIANGLE, PSP_CTRL_CIRCLE, PSP_CTRL_CROSS };
            static const float ghFreqs[4] = { 293.66f, 349.23f, 440.00f, 587.33f };
            for (int l = 0; l < 4; l++) {
                if (pressed & ghBtns[l]) {
                    int best = -1; float bestD = 999.0f;
                    for (int i = 0; i < GH_MAX; i++) {
                        if (ghNotes[i].active && ghNotes[i].lane == l) {
                            float d = fabsf(ghNotes[i].y - (float)GH_HIT_Y);
                            if (d < bestD) { bestD = d; best = i; }
                        }
                    }
                    if (best >= 0 && bestD <= 26.0f) {
                        ghNotes[best].active = 0;
                        ghHits++;
                        int perf = (bestD <= 12.0f);
                        ghMult += perf ? 0.45f : 0.30f;
                        snprintf(ghJudge, sizeof(ghJudge), perf ? "CRITICO! x%.1f" : "BIEN! x%.1f", ghMult);
                        ghJudgeT = 24;
                        play_sfx(ghFreqs[l], 1600);
                        spawn_part(heroX + frand(-1.5f, 1.5f), heroY + 1.2f, heroZ + frand(-1.5f, 1.5f),
                                   0, 0.08f, 0, 26.0f, 0.5f, ABGR(255, 235, 90, 255));
                    } else {
                        snprintf(ghJudge, sizeof(ghJudge), "FALLO!");
                        ghJudgeT = 20;
                        play_sfx(140.0f, 700);
                    }
                }
            }
            int anyLeft = 0;
            for (int i = 0; i < GH_MAX; i++) {
                if (!ghNotes[i].active) continue;
                anyLeft = 1;
                ghNotes[i].y += ghNotes[i].spd;
                if (ghNotes[i].y > GH_HIT_Y + 26.0f) {
                    ghNotes[i].active = 0;
                    snprintf(ghJudge, sizeof(ghJudge), "MISS");
                    ghJudgeT = 18;
                }
            }
            if (ghSpawned >= 8 && !anyLeft) {
                /* Unleash JUICIO SINFONICO massive critical hit! */
                ghActive = 0;
                int totalDmg = (int)((float)heroAtk * ghMult);
                int hitsLanded = 0;
                for (int i = 0; i < nEnemies; i++) {
                    if (!enemies[i].alive) continue;
                    float dx = enemies[i].x - heroX, dz = enemies[i].z - heroZ;
                    if (dx * dx + dz * dz < 100.0f) {
                        enemies[i].hp -= totalDmg;
                        enemies[i].flash = 16;
                        enemies[i].state = 2; enemies[i].timer = 50;
                        hitsLanded++;
                        if (enemies[i].hp <= 0) {
                            enemies[i].alive = 0;
                            furorCharge += 34; if (furorCharge > 100) furorCharge = 100;
                            if (enemies[i].x < -250.0f) { qKillsForest++; if (qState[2] == 1 && qKillsForest >= 2 && obeliskLit[0]) qState[2] = 2; }
                            if (enemies[i].z < -250.0f && enemies[i].x > -250.0f) { qKillsLake++; if (qState[0] == 2 && qKillsLake >= 2) qState[0] = 3; }
                            if (enemies[i].x > 250.0f) { qKillsCanyon++; if (qState[1] == 2 && qKillsCanyon >= 2) qState[1] = 3; }
                            heroGold += enemies[i].isBoss ? 50 : 18;
                            heroCoins += enemies[i].isBoss ? 40 : 12;
                            heroXp += enemies[i].isBoss ? 80 : 25;
                            if (enemies[i].isBoss && worldMode == 1 && !dngCleared[curDungeon]) {
                                dngCleared[curDungeon] = 1; keysFound++;
                                add_prop(0.0f, 0.2f, -57.0f, 1.0f, 3.0f, 7, 1);
                            }
                        }
                    }
                }
                for (int s = 0; s < 36; s++) {
                    float ang = ((float)s / 36.0f) * 6.28318f;
                    spawn_part(heroX + cosf(ang) * 2.0f, heroY + 0.9f, heroZ + sinf(ang) * 2.0f,
                               cosf(ang) * 0.14f, 0.06f, sinf(ang) * 0.14f, 32.0f, 0.55f, ABGR(255, 225, 85, 255));
                }
                heroHp += heroMaxHp / 4; if (heroHp > heroMaxHp) heroHp = heroMaxHp;
                snprintf(dialogMsg, sizeof(dialogMsg), "JUICIO SINFONICO x%.1f! %d DANO CRITICO MASIVO!", ghMult, totalDmg);
                dialogT = 160;
                play_sfx(1046.5f, 3000);
            }
        } else {
            int zone = get_current_zone();
            curZoneAudio = zone;
            int reg = zone;
            if (worldMode == 0) {
                if (heroZ > 150.0f) reg = 4;
                else if (heroX < -140.0f) reg = 5;
            }
            if (worldMode == 0 && reg != prevZone) {
                prevZone = reg;
                static const char *rNames[6] = {
                    "PUEBLO DEL ALBA - LLANURAS CENTRALES",
                    "CAMINO DEL NORTE - LAGO DE LOS ECOS",
                    "CANON CARMESI - TIERRAS DEL ESTE",
                    "CIUDADELA ASTRAL - CIMA DEL NORTE",
                    "FRONTERA SUR - BASTION ANTIGUO",
                    "CAMINO DEL OESTE - BOSQUE ESMERALDA"
                };
                snprintf(bannerMsg, sizeof(bannerMsg), "%s", rNames[reg]);
                bannerT = 140;
            }
            if (bannerT > 0) bannerT--;
            if (dialogT > 0) dialogT--;
            if (atkT > 0) atkT--;
            if (heavyT > 0) heavyT--;
            if (pulseT > 0) pulseT--;
            if (invulnT > 0) invulnT--;
            if (heroFlash > 0) heroFlash--;
            if (stamDelay > 0) stamDelay--;
            else if (heroStam < heroMaxStam) {
                float stamRegen = (heroHunger < 25.0f) ? 0.40f : 0.85f;
                heroStam += stamRegen;
                if (heroStam > heroMaxStam) heroStam = heroMaxStam;
            }
            /* Survival hunger drain + auto-ration + starvation */
            heroHunger -= (dashT > 0 || atkT > 0 || heavyT > 0) ? 0.025f : 0.014f;
            if (heroHunger <= 25.0f && heroFood > 0) {
                heroFood--;
                heroHunger += 50.0f; if (heroHunger > 100.0f) heroHunger = 100.0f;
                heroHp += 20; if (heroHp > heroMaxHp) heroHp = heroMaxHp;
                snprintf(dialogMsg, sizeof(dialogMsg), "RACION AUTOMATICA! +50%% Comida +20 HP (Quedan %d)", heroFood);
                dialogT = 110;
                play_sfx(780.0f, 900);
            }
            if (heroHunger < 0.0f) heroHunger = 0.0f;
            if (heroHunger <= 0.0f) {
                hungerTick++;
                if (hungerTick >= 120) {
                    hungerTick = 0;
                    if (heroHp > 1) heroHp--;
                }
            } else {
                hungerTick = 0;
            }

            /* L + R together: Activate Guitar Hero Critical Special when Furor = 100% */
            if ((pad.Buttons & PSP_CTRL_LTRIGGER) && (pad.Buttons & PSP_CTRL_RTRIGGER) && furorCharge >= 100 && !ghActive) {
                furorCharge = 0;
                ghActive = 1;
                ghSpawned = 0;
                ghSpawnT = 6;
                ghHits = 0;
                ghMult = 1.5f;
                ghJudge[0] = 0; ghJudgeT = 0;
                memset(ghNotes, 0, sizeof(ghNotes));
                play_sfx(880.0f, 1500);
            }
            if ((pad.Buttons & PSP_CTRL_LTRIGGER) && !(pad.Buttons & PSP_CTRL_RTRIGGER)) camYaw -= 0.035f;
            if ((pad.Buttons & PSP_CTRL_RTRIGGER) && !(pad.Buttons & PSP_CTRL_LTRIGGER)) camYaw += 0.035f;

            float ax = 0.0f, ay = 0.0f;
            if (pad.Buttons & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT | PSP_CTRL_UP | PSP_CTRL_DOWN)) {
                if (pad.Buttons & PSP_CTRL_LEFT)  ax = -1.0f;
                if (pad.Buttons & PSP_CTRL_RIGHT) ax =  1.0f;
                if (pad.Buttons & PSP_CTRL_UP)    ay = -1.0f;
                if (pad.Buttons & PSP_CTRL_DOWN)  ay =  1.0f;
            } else {
                ax = ((float)pad.Lx - 128.0f) / 128.0f;
                ay = ((float)pad.Ly - 128.0f) / 128.0f;
                if (fabsf(ax) < 0.34f) ax = 0.0f;
                else ax = (ax > 0.0f ? 1.0f : -1.0f) * ((fabsf(ax) - 0.34f) / 0.66f);
                if (fabsf(ay) < 0.34f) ay = 0.0f;
                else ay = (ay > 0.0f ? 1.0f : -1.0f) * ((fabsf(ay) - 0.34f) / 0.66f);
            }
            float mag = sqrtf(ax * ax + ay * ay);
            if (mag > 1.0f) { ax /= mag; ay /= mag; }

            float spd = 0.17f;
            float fwdX = -sinf(camYaw), fwdZ = -cosf(camYaw);
            float rgtX =  cosf(camYaw), rgtZ = -sinf(camYaw);
            float mvX = (rgtX * ax + fwdX * (-ay));
            float mvZ = (rgtZ * ax + fwdZ * (-ay));

            if (dashT > 0) {
                dashT--;
                mvX = dashDx * 0.30f; mvZ = dashDz * 0.30f;
                if ((animFrame & 1) == 0)
                    spawn_part(heroX, heroY + 0.6f, heroZ, 0, 0.02f, 0, 14.0f, 0.45f, ABGR(130, 235, 255, 210));
            } else {
                mvX *= spd; mvZ *= spd;
                if (ax < -0.1f) heroFlip = 1;
                else if (ax > 0.1f) heroFlip = 0;
            }

            if (fabsf(mvX) > 0.001f || fabsf(mvZ) > 0.001f) {
                if (dashT == 0) heroYaw = atan2f(mvX, mvZ);
                float ny = -10.0f;
                if (is_walkable(heroX + mvX, heroZ, &ny)) { heroX += mvX; heroY = ny; }
                ny = -10.0f;
                if (is_walkable(heroX, heroZ + mvZ, &ny)) { heroZ += mvZ; heroY = ny; }
            }

            /* SELECT: Mission & Investigation Journal */
            if (pressed & PSP_CTRL_SELECT) {
                char jl1[72], jl2[72], jl3[72];
                snprintf(jl1, sizeof(jl1), "Pistas descubiertas: %d/6  |  Llaves Astrales: %d/3", cluesCount, keysFound);
                if (trueEnding) {
                    snprintf(jl2, sizeof(jl2), "MISION CUMPLIDA: El Dragon ha sido purificado!");
                    snprintf(jl3, sizeof(jl3), "Aetheria vuelve a elevarse libre por los cielos.");
                } else if (keysFound < 3) {
                    snprintf(jl2, sizeof(jl2), "Recorre los caminos del Oeste, Norte y Este.");
                    snprintf(jl3, sizeof(jl3), "Lleva raciones; explora campamentos y Tablillas.");
                } else if (cluesCount >= 6) {
                    snprintf(jl2, sizeof(jl2), "VERDAD REVELADA: Sube el Ascenso Alpino (Norte).");
                    snprintf(jl3, sizeof(jl3), "Pulsa /\\ ante el Dragon para PURIFICARLO!");
                } else {
                    snprintf(jl2, sizeof(jl2), "Tienes las 3 Llaves! Sube a la Ciudadela (Norte).");
                    snprintf(jl3, sizeof(jl3), "O busca las pistas restantes (%d/6) antes de luchar.", cluesCount);
                }
                show_story("DIARIO DE INVESTIGACION", jl1, jl2, jl3);
                play_sfx(680.0f, 900);
            }

            /* START: Use Elixir / Ration / Potion */
            if (pressed & PSP_CTRL_START) {
                if (heroStam < 30.0f && heroElixirs > 0) {
                    heroElixirs--;
                    heroStam = heroMaxStam;
                    heroHp += 40; if (heroHp > heroMaxHp) heroHp = heroMaxHp;
                    snprintf(dialogMsg, sizeof(dialogMsg), "ELIXIR USADO! Estamina 100%% +40 HP (Quedan %d)", heroElixirs);
                    dialogT = 100; play_sfx(920.0f, 1200);
                } else if (heroHunger < 75.0f && heroFood > 0 && heroHp >= (heroMaxHp * 85) / 100) {
                    heroFood--;
                    heroHunger += 50.0f; if (heroHunger > 100.0f) heroHunger = 100.0f;
                    heroHp += 20; if (heroHp > heroMaxHp) heroHp = heroMaxHp;
                    snprintf(dialogMsg, sizeof(dialogMsg), "RACION COMIDA! +50%% Comida +20 HP (Quedan %d)", heroFood);
                    dialogT = 100; play_sfx(780.0f, 1000);
                } else if (heroPotions > 0 && heroHp < heroMaxHp) {
                    heroPotions--;
                    heroHp += 60; if (heroHp > heroMaxHp) heroHp = heroMaxHp;
                    snprintf(dialogMsg, sizeof(dialogMsg), "POCION USADA! +60 HP (Quedan %d)", heroPotions);
                    dialogT = 100; play_sfx(860.0f, 1200);
                } else if (heroFood > 0 && heroHunger < 95.0f) {
                    heroFood--;
                    heroHunger += 50.0f; if (heroHunger > 100.0f) heroHunger = 100.0f;
                    heroHp += 20; if (heroHp > heroMaxHp) heroHp = heroMaxHp;
                    snprintf(dialogMsg, sizeof(dialogMsg), "RACION COMIDA! +50%% Comida +20 HP (Quedan %d)", heroFood);
                    dialogT = 100; play_sfx(780.0f, 1000);
                } else if (heroElixirs > 0 && (heroStam < heroMaxStam || heroHp < heroMaxHp)) {
                    heroElixirs--;
                    heroStam = heroMaxStam;
                    heroHp += 40; if (heroHp > heroMaxHp) heroHp = heroMaxHp;
                    snprintf(dialogMsg, sizeof(dialogMsg), "ELIXIR USADO! Estamina 100%% +40 HP (Quedan %d)", heroElixirs);
                    dialogT = 100; play_sfx(920.0f, 1200);
                }
            }

            /* Square: Souls Dodge Roll (22 Stamina, 16 i-frames) */
            if ((pressed & PSP_CTRL_SQUARE) && dashT == 0 && heroStam >= 22.0f) {
                heroStam -= 22.0f; stamDelay = 24;
                float dl = sqrtf(mvX * mvX + mvZ * mvZ);
                if (dl > 0.01f) { dashDx = mvX / dl; dashDz = mvZ / dl; }
                else { dashDx = sinf(heroYaw); dashDz = cosf(heroYaw); }
                dashT = 12; invulnT = 16;
                play_sfx(540.0f, 900);
            }

            /* Cross: Directional Light-Blade Slash (18 Stamina, hits where hero aims) */
            if ((pressed & PSP_CTRL_CROSS) && atkT == 0 && heavyT == 0 && heroStam >= 18.0f) {
                heroStam -= 18.0f; stamDelay = 20;
                atkT = 14;
                play_sfx(740.0f, 1000);
                float hitX = heroX + sinf(heroYaw) * 1.85f;
                float hitZ = heroZ + cosf(heroYaw) * 1.85f;
                for (int p = -3; p <= 3; p++) {
                    float ang = heroYaw + (float)p * 0.25f;
                    spawn_part(heroX + sinf(ang) * 1.85f, heroY + 0.75f, heroZ + cosf(ang) * 1.85f,
                               sinf(ang) * 0.05f, 0.02f, cosf(ang) * 0.05f, 14.0f, 0.42f, ABGR(255, 240, 120, 240));
                }
                for (int i = 0; i < nEnemies; i++) {
                    if (!enemies[i].alive) continue;
                    float dx = enemies[i].x - hitX, dz = enemies[i].z - hitZ;
                    float d = sqrtf(dx * dx + dz * dz);
                    float effRad = enemies[i].isBoss ? 2.8f : 2.1f;
                    if (d < effRad) {
                        enemies[i].hp -= heroAtk;
                        enemies[i].flash = 8;
                        furorCharge += 8; if (furorCharge > 100) furorCharge = 100;
                        float kdx = enemies[i].x - heroX, kdz = enemies[i].z - heroZ;
                        float kd = sqrtf(kdx * kdx + kdz * kdz);
                        if (kd > 0.05f) {
                            float kx = enemies[i].x + (kdx / kd) * 0.6f, kz = enemies[i].z + (kdz / kd) * 0.6f, ky = enemies[i].y;
                            if (is_walkable(kx, kz, &ky)) { enemies[i].x = kx; enemies[i].z = kz; enemies[i].y = ky; }
                        }
                        play_sfx(920.0f, 1200);
                        for (int s = 0; s < 8; s++)
                            spawn_part(enemies[i].x, enemies[i].y + 0.8f, enemies[i].z,
                                       frand(-0.08f, 0.08f), frand(0.04f, 0.12f), frand(-0.08f, 0.08f),
                                       22.0f, 0.35f, ABGR(255, 120, 80, 255));
                        if (enemies[i].hp <= 0) {
                            enemies[i].alive = 0;
                            if (!enemies[i].isBoss) enemies[i].respawnT = 600;
                            furorCharge += 34; if (furorCharge > 100) furorCharge = 100;
                            if (enemies[i].x < -250.0f) { qKillsForest++; if (qState[2] == 1 && qKillsForest >= 2 && obeliskLit[0]) qState[2] = 2; }
                            if (enemies[i].z < -250.0f && enemies[i].x > -250.0f) { qKillsLake++; if (qState[0] == 2 && qKillsLake >= 2) qState[0] = 3; }
                            if (enemies[i].x > 250.0f) { qKillsCanyon++; if (qState[1] == 2 && qKillsCanyon >= 2) qState[1] = 3; }
                            static const int mobGold[3]  = { 10, 22, 40 };
                            static const int mobCoins[3] = {  6, 12, 22 };
                            static const int mobXp[3]    = { 14, 30, 55 };
                            int goldDrop = enemies[i].isBoss ? (80 + enemies[i].bossId * 50) : mobGold[enemies[i].kind % 3];
                            int coinDrop = enemies[i].isBoss ? 40 : mobCoins[enemies[i].kind % 3];
                            int xpGain = enemies[i].isBoss ? (160 + enemies[i].bossId * 120) : mobXp[enemies[i].kind % 3];
                            heroGold += goldDrop;
                            heroCoins += coinDrop;
                            heroXp += xpGain;
                            if (!enemies[i].isBoss && (xr() % 5) == 0) { heroPotions++; }
                            int xpNeed = 80 * heroLv * heroLv;
                            if (heroXp >= xpNeed) {
                                heroXp -= xpNeed;
                                heroLv++;
                                heroMaxHp += 12; heroHp = heroMaxHp; heroAtk += 3;
                                snprintf(dialogMsg, sizeof(dialogMsg), "SUBES A NIVEL %d! +12 HP y +3 ATK", heroLv);
                                dialogT = 150;
                            }
                            if (enemies[i].isBoss) {
                                if (worldMode == 1 && !dngCleared[curDungeon]) {
                                    dngCleared[curDungeon] = 1;
                                    keysFound++;
                                    heroMaxHp += 25; heroHp = heroMaxHp; heroAtk += 8;
                                    add_prop(0.0f, 0.2f, -57.0f, 1.0f, 3.0f, 7, 1);
                                    snprintf(dialogMsg, sizeof(dialogMsg), "JEFE VENCIDO! LLAVE ASTRAL %d/3 OBTENIDA!", keysFound);
                                    dialogT = 200;
                                    play_sfx(1040.0f, 3200);
                                } else if (worldMode == 0) {
                                    if (trueEnding) {
                                        show_story("FINAL VERDADERO: AURORA CELESTE",
                                            "El pulso de tu Wisp purifico la corrupcion del Dragon.",
                                            "Vaelen descansa en paz y las islas ascienden al sol.",
                                            "Gracias por jugar a Aetheria!");
                                    } else {
                                        show_story("FINAL TRAGICO: SILENCIO ETERNO",
                                            "El Dragon ha caido bajo el filo de tu espada...",
                                            "Pero sin su latido, las islas descienden lentamente.",
                                            "(Tip: con 3 Llaves y Pista 6, usa /\\ para purificar!)");
                                    }
                                    play_sfx(1040.0f, 3600);
                                }
                            }
                        }
                    }
                }
            }

            /* Triangle: Heavy Resonance Strike (32 Stamina, 2.2x ATK + Stagger + Obelisks + Purify) */
            if ((pressed & PSP_CTRL_TRIANGLE) && pulseT == 0 && heroStam >= 32.0f) {
                heroStam -= 32.0f; stamDelay = 28;
                pulseT = 28; heavyT = 18;
                play_sfx(660.0f, 1800);
                for (int i = 0; i < nEnemies; i++) {
                    if (!enemies[i].alive) continue;
                    float dx = enemies[i].x - heroX, dz = enemies[i].z - heroZ;
                    if (dx * dx + dz * dz < 14.5f) {
                        if (worldMode == 0 && keysFound >= 3 && !trueEnding && enemies[i].isBoss) {
                            trueEnding = 1;
                            enemies[i].alive = 0;
                            for (int s = 0; s < 40; s++)
                                spawn_part(enemies[i].x + frand(-2.5f, 2.5f), enemies[i].y + frand(0.5f, 3.5f), enemies[i].z + frand(-2.5f, 2.5f),
                                           frand(-0.06f, 0.06f), frand(0.04f, 0.14f), frand(-0.06f, 0.06f), 110.0f, 0.48f, ABGR(255, 235, 110, 255));
                            show_story("FINAL VERDADERO: PURIFICACION",
                                "Las 3 Llaves resuenan y rompen la sombra de Vaelen!",
                                "El Dragon Astral despierta sano y eleva Aetheria al cielo.",
                                "Has salvado el mundo comprendiendo la verdad!");
                            play_sfx(1180.0f, 3800);
                        } else {
                            enemies[i].hp -= (heroAtk * 2);
                            enemies[i].flash = 12;
                            if (enemies[i].state == 1) { enemies[i].state = 2; enemies[i].timer = 40; } /* Stagger! */
                            for (int s = 0; s < 10; s++)
                                spawn_part(enemies[i].x, enemies[i].y + 0.8f, enemies[i].z,
                                           frand(-0.10f, 0.10f), frand(0.05f, 0.15f), frand(-0.10f, 0.10f),
                                           24.0f, 0.40f, ABGR(120, 240, 255, 255));
                            if (enemies[i].hp <= 0) { enemies[i].alive = 0; heroGold += 20; heroCoins += 12; heroXp += 35; }
                        }
                    }
                }
                for (int i = 0; i < nProps; i++) {
                    if (props[i].kind == 2 && !props[i].active) {
                        float dx = props[i].x - heroX, dz = props[i].z - heroZ;
                        if (dx * dx + dz * dz < 20.0f) {
                            props[i].active = 1;
                            if (props[i].id >= 0 && props[i].id < 12) obeliskLit[props[i].id] = 1;
                            if (qState[2] == 1 && qKillsForest >= 2 && obeliskLit[0]) qState[2] = 2;
                            runesLit++;
                            heroMaxHp += 12; heroHp = heroMaxHp;
                            snprintf(dialogMsg, sizeof(dialogMsg), "OBELISCO ILUMINADO (%d)! +12 MAX HP", runesLit);
                            dialogT = 140;
                            play_sfx(990.0f, 2000);
                        }
                    }
                }
            }

            /* Circle: Interact (NPCs, Shops, Chests, Crystals, Dungeon Portals) */
            if (pressed & PSP_CTRL_CIRCLE) {
                int acted = 0;
                for (int i = 0; i < nNpcs && !acted; i++) {
                    float dx = npcs[i].x - heroX, dz = npcs[i].z - heroZ;
                    if (dx * dx + dz * dz < 10.0f) {
                        acted = 1;
                        int nid = npcs[i].npcId;
                        if (nid == 0) { /* Alcalde Eldon */
                            if (qState[0] == 0) {
                                qState[0] = 1;
                                show_story("MISION: RUTA DEL NORTE (Eldon)",
                                    "Los campamentos cortaron el Paso de la Garganta...",
                                    "Lleva mi Sello a NILO en Aldea Lacustre (Norte).",
                                    "Compra raciones antes de partir! (+45 Oro, +20 Mon)");
                            } else if (cluesCount >= 4) {
                                show_story("ALCALDE ELDON", "Vimos a Vaelen cruzar las llanuras hacia el Norte...", "Si lo que dicen las tablillas es cierto, el Dragon", "es nuestro protector. Ten cuidado en la Ciudadela!");
                            } else {
                                show_story("ALCALDE ELDON", "El continente tiembla desde que la Campana callo.", "Lleva mi Sello a NILO (Norte) y busca las 3 Llaves.", "El Bastion del Sur tambien guarda reliquias!");
                            }
                            play_sfx(780.0f, 1100);
                        } else if (nid == 4) { /* Mercader Nilo (Aldea Lacustre) */
                            if (qState[0] == 1) {
                                qState[0] = 2; heroGold += 45; heroCoins += 20; heroXp += 40;
                                if (qKillsLake >= 2) qState[0] = 3;
                                show_story("NILO - MISION: LIMPIEZA DEL LAGO",
                                    "Eldon te envia? Gracias! (+45 Oro, +20 Monedas)",
                                    "Los monstruos del Lago robaron nuestras medicinas.",
                                    "Derrota 2 enemigos aqui en el Lago y vuelve conmigo!");
                                play_sfx(960.0f, 1400);
                            } else if (qState[0] == 3) {
                                qState[0] = 4; heroMaxHp += 25; heroHp = heroMaxHp; heroPotions += 2; heroFood += 2;
                                show_story("MISION COMPLETADA: ELIXIR LACUSTRE",
                                    "Has salvado la Aldea! (+25 HP, +2 Poc, +2 Raciones)",
                                    "Escuche un latido triste bajo el templo... Vaelen",
                                    "no nos traiciono por odio. Investiga las ruinas!");
                                play_sfx(1100.0f, 1800);
                            } else {
                                shopOpen = 1; shopCursor = 0;
                                play_sfx(780.0f, 800);
                            }
                        } else if (nid == 1) { /* Herrero Kael */
                            if (qState[1] == 0) {
                                qState[1] = 1;
                                show_story("MISION: LA CARTA DEL APRENDIZ (Kael)",
                                    "Mi maestro VULCAN cruzo el Desfiladero del Este",
                                    "hacia el Canon Carmesi, rodeado de campamentos.",
                                    "Llevale esta carta por el Camino del Este!");
                                play_sfx(780.0f, 1100);
                            } else if (heroGold >= forge_cost()) {
                                heroGold -= forge_cost(); forgeCount++; heroAtk += 4;
                                snprintf(dialogMsg, sizeof(dialogMsg), "KAEL: Espada forjada! (+4 ATK, Total %d, Prox %dg)", heroAtk, forge_cost());
                                play_sfx(1050.0f, 1600);
                            } else {
                                snprintf(dialogMsg, sizeof(dialogMsg), "KAEL: Lleva carta a VULCAN o forja (+4 ATK por %dg).", forge_cost());
                                play_sfx(740.0f, 900);
                            }
                        } else if (nid == 5) { /* Forjador Vulcan (Canon Carmesi) */
                            if (qState[1] == 1) {
                                qState[1] = 2; heroGold += 50; heroXp += 40;
                                if (qKillsCanyon >= 2) qState[1] = 3;
                                show_story("VULCAN - MISION: CORAZON DE LA FORJA",
                                    "Kael aun recuerda nuestras lecciones? (+50 Oro)",
                                    "La sombra corrompio mi yunque. Derrota 2 monstruos",
                                    "del Canon Carmesi y te forjare Acero Estelar!");
                                play_sfx(960.0f, 1400);
                            } else if (qState[1] == 3) {
                                qState[1] = 4; heroAtk += 12;
                                show_story("MISION COMPLETADA: ACERO ESTELAR",
                                    "Toma esta hoja forjada con fuego celeste! (+12 ATK)",
                                    "Vi a Vaelen entrar en la Forja de Obsidiana...",
                                    "Si consigues las 3 Llaves, busca la verdad!");
                                play_sfx(1150.0f, 2000);
                            } else if (heroGold >= forge_cost()) {
                                heroGold -= forge_cost(); forgeCount++; heroAtk += 4;
                                snprintf(dialogMsg, sizeof(dialogMsg), "VULCAN: Filo templado! (+4 ATK, Total %d, Prox %dg)", heroAtk, forge_cost());
                                play_sfx(1050.0f, 1600);
                            } else {
                                snprintf(dialogMsg, sizeof(dialogMsg), "VULCAN: Forjar (+4 ATK, %dg) | Canon: %d/2 enemigos", forge_cost(), qKillsCanyon);
                                play_sfx(740.0f, 900);
                            }
                        } else if (nid == 2) { /* Alquimista Lyra */
                            if (qState[2] == 0) {
                                qState[2] = 1;
                                show_story("MISION: SOMBRAS DEL BOSQUE (Lyra)",
                                    "Saquearon la Torre de Vigia y el Bosque Esmeralda!",
                                    "Cruza el Camino del Oeste, vence 2 enemigos alli",
                                    "e ilumina su Obelisco (/\\). (+3 Poc, +2 Rac, +25M)");
                                play_sfx(780.0f, 1100);
                            } else if (qState[2] == 2) {
                                qState[2] = 3; heroPotions += 3; heroFood += 2; heroCoins += 25; heroMaxHp += 15; heroHp = heroMaxHp;
                                show_story("MISION COMPLETADA: PAZ EN EL BOSQUE",
                                    "La niebla se disipo! (+3 Poc, +2 Rac, +25 Mon, +15 HP)",
                                    "Toma mis provisiones para tu viaje a las mazmorras.",
                                    "Vuelve cuando necesites comida, pociones o elixires!");
                                play_sfx(1100.0f, 1800);
                            } else {
                                shopOpen = 1; shopCursor = 0;
                                play_sfx(780.0f, 800);
                            }
                        } else if (nid == 3) { /* Sabio Orin */
                            if (qState[3] == 0) {
                                qState[3] = 1;
                                show_story("MISION: EL PACTO DE AETHERIA (Orin)",
                                    "Eldon, Kael y Lyra ven solo las heridas del mundo.",
                                    "Ayuda a los 3 pueblos o descubre las 6 Tablillas",
                                    "para recibir la Bendicion del Wisp!");
                                play_sfx(780.0f, 1100);
                            } else if (qState[3] == 1 && (cluesCount >= 6 || (qState[0] >= 4 && qState[1] >= 4 && qState[2] >= 3))) {
                                qState[3] = 2; heroAtk += 15; heroMaxHp += 30; heroHp = heroMaxHp;
                                show_story("BENDICION DEL WISP (+15 ATK, +30 HP)",
                                    "Has unido los corazones de Aetheria y su verdad!",
                                    "Con las 3 Llaves, sube al Norte y pulsa /\\",
                                    "ante el Dragon para PURIFICARLO y salvar el cielo!");
                                play_sfx(1180.0f, 2400);
                            } else {
                                show_story("SABIO ORIN",
                                    "Las ruinas guardan 6 Tablillas y los pueblos piden ayuda.",
                                    "Conecta las historias de Eldon, Kael y Lyra.",
                                    "Pulsa SELECT para consultar tu Diario de Misiones.");
                                play_sfx(780.0f, 900);
                            }
                        }
                        dialogT = 160;
                    }
                }
                for (int i = 0; i < nProps && !acted; i++) {
                    float dx = props[i].x - heroX, dz = props[i].z - heroZ;
                    if (dx * dx + dz * dz < 10.0f) {
                        if (props[i].kind == 3 && !props[i].active) {
                            props[i].active = 1;
                            if (props[i].id >= 0 && props[i].id < 16) chestOpened[props[i].id] = 1;
                            heroRelics++; heroGold += 35; heroCoins += 20; heroAtk += 4; heroMaxHp += 10; heroHp = heroMaxHp;
                            snprintf(dialogMsg, sizeof(dialogMsg), "COFRE %d: +35 Oro, +20 Monedas, +4 ATK y +10 HP!", heroRelics);
                            dialogT = 150; play_sfx(1100.0f, 1800); acted = 1;
                        } else if (props[i].kind == 4) {
                            heroHp = heroMaxHp; heroStam = heroMaxStam;
                            if (props[i].id == 0) {
                                save_game();
                                snprintf(dialogMsg, sizeof(dialogMsg), "PARTIDA GUARDADA EN PUEBLO DEL ALBA! (HP 100%%)");
                            } else {
                                snprintf(dialogMsg, sizeof(dialogMsg), "VIDA RESTAURADA! (Solo se guarda en Pueblo del Alba)");
                            }
                            dialogT = 150; play_sfx(880.0f, 1400); acted = 1;
                        } else if (props[i].kind == 9) {
                            if (heroGold >= 25) {
                                heroGold -= 25;
                                heroX = 0.0f; heroY = 0.20f; heroZ = 2.0f;
                                snprintf(dialogMsg, sizeof(dialogMsg), "VIAJE RAPIDO A PUEBLO DEL ALBA (-25 Oro)!");
                                play_sfx(1040.0f, 1800);
                            } else {
                                snprintf(dialogMsg, sizeof(dialogMsg), "PORTAL DE RETORNO: Cuesta 25 Oro volver a Pueblo del Alba!");
                                play_sfx(220.0f, 800);
                            }
                            dialogT = 140; acted = 1;
                        } else if (props[i].kind == 6) {
                            saveOvX = heroX; saveOvY = heroY; saveOvZ = heroZ + 1.5f;
                            int dId = props[i].id;
                            build_dungeon(dId);
                            static const char *dNames[3] = { "MAZMORRA 1: TEMPLO DEL BOSQUE", "MAZMORRA 2: RUINAS DEL AGUA", "MAZMORRA 3: FORJA DE OBSIDIANA" };
                            snprintf(bannerMsg, sizeof(bannerMsg), "%s", dNames[dId]);
                            bannerT = 150; play_sfx(660.0f, 2200); acted = 1;
                        } else if (props[i].kind == 7) {
                            build_overworld();
                            heroX = saveOvX; heroY = saveOvY; heroZ = saveOvZ;
                            snprintf(bannerMsg, sizeof(bannerMsg), "REGRESO AL MUNDO EXTERIOR");
                            bannerT = 120; play_sfx(780.0f, 1800); acted = 1;
                        } else if (props[i].kind == 8) {
                            int cid = props[i].id;
                            if (cid >= 0 && cid < 6) {
                                if (!clueFound[cid]) { clueFound[cid] = 1; props[i].active = 1; cluesCount++; heroXp += 25; }
                                show_story(clueTitles[cid], clueText[cid][0], clueText[cid][1], clueText[cid][2]);
                                play_sfx(960.0f, 1600);
                                acted = 1;
                            }
                        }
                    }
                }
            }

            /* Update Enemies (Respawn for Farming + Dark Souls Telegraph AI) */
            for (int i = 0; i < nEnemies; i++) {
                Enemy3D *e = &enemies[i];
                if (!e->alive) {
                    if (!e->isBoss && e->respawnT > 0) {
                        e->respawnT--;
                        if (e->respawnT == 0) {
                            float hx = heroX - e->homeX, hz = heroZ - e->homeZ;
                            if (hx * hx + hz * hz > 100.0f) {
                                e->x = e->homeX; e->y = e->homeY; e->z = e->homeZ;
                                e->hp = e->maxHp; e->alive = 1; e->state = 0; e->flash = 0;
                            } else {
                                e->respawnT = 120;
                            }
                        }
                    }
                    continue;
                }
                if (e->flash > 0) e->flash--;
                float dx = heroX - e->x, dz = heroZ - e->z;
                if (dx * dx + dz * dz > 2500.0f) continue;
                float d = sqrtf(dx * dx + dz * dz);
                float trigDist = e->isBoss ? 3.8f : 2.5f;

                if (e->state == 0) { /* Chase */
                    if (d < 12.0f && d > trigDist * 0.75f) {
                        float es = e->isBoss ? 0.085f : 0.070f;
                        float nx = e->x + (dx / d) * es, nz = e->z + (dz / d) * es, ey = e->y;
                        if (is_walkable(nx, nz, &ey)) { e->x = nx; e->z = nz; e->y = ey; }
                    }
                    if (d <= trigDist) {
                        e->state = 1;
                        e->timer = e->isBoss ? 28 : 22;
                        e->atkX = heroX;
                        e->atkZ = heroZ;
                        e->atkRad = e->isBoss ? 3.3f : 2.0f;
                    }
                } else if (e->state == 1) { /* Wind-up Telegraph */
                    e->timer--;
                    if (e->timer <= 0) {
                        /* Strike at telegraphed zone! */
                        for (int s = 0; s < 12; s++)
                            spawn_part(e->atkX + frand(-1.0f, 1.0f), e->y + 0.3f, e->atkZ + frand(-1.0f, 1.0f),
                                       frand(-0.07f, 0.07f), frand(0.05f, 0.14f), frand(-0.07f, 0.07f),
                                       20.0f, 0.42f, ABGR(255, 65, 55, 255));
                        float hdx = heroX - e->atkX, hdz = heroZ - e->atkZ;
                        if (sqrtf(hdx * hdx + hdz * hdz) <= e->atkRad && invulnT == 0) {
                            static const int mobDmg[3]  = { 18, 32, 52 };
                            static const int bossDmg[3] = { 48, 70, 105 };
                            int dmg = e->isBoss ? bossDmg[e->bossId % 3] : mobDmg[e->kind % 3];
                            heroHp -= dmg;
                            heroFlash = 10; invulnT = 28;
                            play_sfx(150.0f, 1200);
                            if (heroHp <= 0) {
                                if (worldMode == 1) build_overworld();
                                int lostG = heroGold / 4;
                                heroGold -= lostG;
                                heroHp = heroMaxHp; heroX = 0.0f; heroY = 0.25f; heroZ = 4.0f;
                                snprintf(dialogMsg, sizeof(dialogMsg), "CAIDO! Pierdes %d Oro y vuelves al Santuario!", lostG);
                                dialogT = 180;
                            }
                        } else {
                            play_sfx(220.0f, 700);
                        }
                        e->state = 2;
                        e->timer = e->isBoss ? 30 : 22;
                    }
                } else { /* Recovery window */
                    e->timer--;
                    if (e->timer <= 0) e->state = 0;
                }
            }
        }

        for (int i = 0; i < MAX_PARTS; i++) {
            if (parts[i].life > 0.0f) {
                parts[i].x += parts[i].vx; parts[i].y += parts[i].vy; parts[i].z += parts[i].vz;
                parts[i].life -= 1.0f;
            }
        }

        /* ==================== HARDWARE GU 3D RENDER ==================== */
        int curZone = get_current_zone();
        ScePspDateTime pspNow;
        int curHour = 12, curMin = 0;
        if (sceRtcGetCurrentClockLocalTime(&pspNow) >= 0) {
            curHour = pspNow.hour;
            curMin = pspNow.minute;
        }
        float dayHour = (float)curHour + (float)curMin / 60.0f;

        /* Time-of-day palette interpolation: Dawn, Day, Sunset, Night */
        static const unsigned int cHor[4] = {
            ABGR(255, 168, 108, 255), /* 0: Dawn */
            ABGR(140, 210, 252, 255), /* 1: Day */
            ABGR(255, 122,  72, 255), /* 2: Sunset */
            ABGR( 18,  28,  64, 255)  /* 3: Night */
        };
        static const unsigned int cMid[4] = {
            ABGR(165, 118, 172, 255),
            ABGR( 62, 148, 232, 255),
            ABGR(146,  64, 132, 255),
            ABGR( 10,  16,  44, 255)
        };
        static const unsigned int cZen[4] = {
            ABGR( 46,  72, 142, 255),
            ABGR( 22,  76, 174, 255),
            ABGR( 26,  30,  84, 255),
            ABGR(  4,   7,  22, 255)
        };
        int pA = 3, pB = 3; float pt = 0.0f;
        if (dayHour >= 5.0f && dayHour < 8.0f) {
            pA = 3; pB = 0; pt = (dayHour - 5.0f) / 3.0f;
        } else if (dayHour >= 8.0f && dayHour < 10.5f) {
            pA = 0; pB = 1; pt = (dayHour - 8.0f) / 2.5f;
        } else if (dayHour >= 10.5f && dayHour < 16.5f) {
            pA = 1; pB = 1; pt = 0.0f;
        } else if (dayHour >= 16.5f && dayHour < 19.5f) {
            pA = 1; pB = 2; pt = (dayHour - 16.5f) / 3.0f;
        } else if (dayHour >= 19.5f && dayHour < 21.5f) {
            pA = 2; pB = 3; pt = (dayHour - 19.5f) / 2.0f;
        } else {
            pA = 3; pB = 3; pt = 0.0f;
        }
        unsigned int skyHor = lerp_abgr(cHor[pA], cHor[pB], pt);
        unsigned int skyMid = lerp_abgr(cMid[pA], cMid[pB], pt);
        unsigned int skyZen = lerp_abgr(cZen[pA], cZen[pB], pt);
        /* Subtle zone atmosphere blend (crimson canyon / astral citadel) */
        if (curZone == 2) skyHor = lerp_abgr(skyHor, ABGR(180, 78, 86, 255), 0.28f);
        else if (curZone == 3) skyZen = lerp_abgr(skyZen, ABGR(16, 14, 48, 255), 0.35f);

        float nightFactor = 0.0f;
        if (dayHour < 5.5f || dayHour >= 20.5f) nightFactor = 1.0f;
        else if (dayHour < 8.0f) nightFactor = 1.0f - (dayHour - 5.5f) / 2.5f;
        else if (dayHour >= 18.0f) nightFactor = (dayHour - 18.0f) / 2.5f;

        sceGuStart(GU_DIRECT, gulist);
        curTex = NULL;
        curCamCos = cosf(camYaw);
        curCamSin = sinf(camYaw);
        sceGuClearColor(worldMode == 1 ? ABGR(12, 10, 24, 255) : skyHor);
        sceGuClearDepth(0);
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

        sceGumMatrixMode(GU_PROJECTION);
        sceGumLoadIdentity();
        sceGumPerspective(52.0f, 16.0f / 9.0f, 1.0f, 95.0f);

        sceGumMatrixMode(GU_VIEW);
        sceGumLoadIdentity();
        static float smoothCamY = 0.10f;
        smoothCamY += (heroY - smoothCamY) * 0.18f;
        float camDist = inTitle ? 12.5f : 10.2f;
        float camH    = inTitle ? 5.8f  : 6.0f;
        ScePspFVector3 eye = { heroX + sinf(camYaw) * camDist, smoothCamY + camH, heroZ + cosf(camYaw) * camDist };
        ScePspFVector3 at  = { heroX, smoothCamY + 1.1f, heroZ };
        ScePspFVector3 up  = { 0.0f, 1.0f, 0.0f };
        sceGumLookAt(&eye, &at, &up);

        sceGumMatrixMode(GU_MODEL);
        sceGumLoadIdentity();

        /* Beautiful Gradient Sky Dome + Sun/Moon + Stars + Clouds (Overworld) */
        if (worldMode == 0) {
            sceGuDisable(GU_DEPTH_TEST);
            sceGuDisable(GU_FOG);
            sceGuDisable(GU_TEXTURE_2D);

            const int segs = 16;
            const float radH = 78.0f, radM = 68.0f;
            float baseY = eye.y - 6.0f;
            VtxCol3D *sv = (VtxCol3D *)sceGuGetMemory(segs * 12 * sizeof(VtxCol3D));
            int sk = 0;
            for (int i = 0; i < segs; i++) {
                float a0 = ((float)i / (float)segs) * 6.283185f;
                float a1 = ((float)(i + 1) / (float)segs) * 6.283185f;
                float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
                float x0h = eye.x + c0 * radH, z0h = eye.z + s0 * radH;
                float x1h = eye.x + c1 * radH, z1h = eye.z + s1 * radH;
                float x0m = eye.x + c0 * radM, z0m = eye.z + s0 * radM;
                float x1m = eye.x + c1 * radM, z1m = eye.z + s1 * radM;
                float yH = baseY + 2.0f, yM = baseY + 24.0f, yZ = baseY + 58.0f;
                /* Horizon -> Mid band */
                sv[sk++] = (VtxCol3D){ skyHor, x0h, yH, z0h };
                sv[sk++] = (VtxCol3D){ skyHor, x1h, yH, z1h };
                sv[sk++] = (VtxCol3D){ skyMid, x1m, yM, z1m };
                sv[sk++] = (VtxCol3D){ skyHor, x0h, yH, z0h };
                sv[sk++] = (VtxCol3D){ skyMid, x1m, yM, z1m };
                sv[sk++] = (VtxCol3D){ skyMid, x0m, yM, z0m };
                /* Mid -> Zenith cap */
                sv[sk++] = (VtxCol3D){ skyMid, x0m, yM, z0m };
                sv[sk++] = (VtxCol3D){ skyMid, x1m, yM, z1m };
                sv[sk++] = (VtxCol3D){ skyZen, eye.x, yZ, eye.z };
            }
            sceGumDrawArray(GU_TRIANGLES, VTYPE_COL3D, sk, 0, sv);

            /* Twinkling Stars when nightFactor > 0.05 */
            if (nightFactor > 0.05f) {
                for (int st = 0; st < 24; st++) {
                    float az = (float)st * 2.39996f;
                    float el = 0.18f + ((float)((st * 37) % 100) / 100.0f) * 0.72f;
                    float cr = cosf(el) * 70.0f;
                    float sx = eye.x + cosf(az) * cr;
                    float sy = baseY + 4.0f + sinf(el) * 52.0f;
                    float sz = eye.z + sinf(az) * cr;
                    float tw = 0.65f + 0.35f * sinf((float)animFrame * 0.08f + (float)st * 1.7f);
                    int sa = (int)(nightFactor * tw * 245.0f);
                    float ssz = ((st % 4) == 0) ? 1.55f : 0.95f;
                    draw_billboard(sx, sy, sz, ssz, ssz, texSoftGlow, ABGR(220, 235, 255, sa), 0, camYaw);
                }
            }

            /* Sun & Moon orbiting by console clock */
            float sunAng = ((dayHour - 6.0f) / 12.0f) * 3.141592f;
            float sunSin = sinf(sunAng), sunCos = cosf(sunAng);
            if (sunSin > -0.15f) {
                float sx = eye.x + sunCos * 64.0f;
                float sy = baseY + 6.0f + sunSin * 46.0f;
                float sz = eye.z - 22.0f;
                draw_billboard(sx, sy, sz, 11.5f, 11.5f, texSoftGlow, ABGR(255, 210, 110, 145), 0, camYaw);
                draw_billboard(sx, sy, sz, 5.2f, 5.2f, texSoftGlow, ABGR(255, 248, 200, 255), 0, camYaw);
            }
            float moonSin = -sunSin, moonCos = -sunCos;
            if (moonSin > -0.15f) {
                float mx = eye.x + moonCos * 64.0f;
                float my = baseY + 6.0f + moonSin * 46.0f;
                float mz = eye.z - 22.0f;
                draw_billboard(mx, my, mz, 9.0f, 9.0f, texSoftGlow, ABGR(140, 185, 255, 130), 0, camYaw);
                draw_billboard(mx, my, mz, 4.2f, 4.2f, texSoftGlow, ABGR(230, 242, 255, 250), 0, camYaw);
            }

            /* Drifting Soft Sky Clouds */
            unsigned int cldCol = (nightFactor > 0.5f) ? ABGR(95, 115, 165, 110) :
                                  ((pA == 0 || pB == 2) ? ABGR(255, 205, 185, 145) : ABGR(245, 250, 255, 155));
            for (int c = 0; c < 10; c++) {
                float ca = (float)c * 0.6283f + (float)animFrame * 0.0005f;
                float dist = 38.0f + (float)((c * 13) % 26);
                float cx = eye.x + cosf(ca) * dist;
                float cz = eye.z + sinf(ca) * dist;
                float cy = baseY + 18.0f + (float)(c % 4) * 3.5f;
                float cw = 12.0f + (float)(c % 3) * 4.0f;
                draw_billboard(cx, cy, cz, cw, cw * 0.42f, texSoftGlow, cldCol, 0, camYaw);
            }
        }

        sceGuEnable(GU_DEPTH_TEST);
        sceGuEnable(GU_CLIP_PLANES);
        sceGuEnable(GU_FOG);
        sceGuFog(22.0f, 72.0f, worldMode == 1 ? 0xFF180A0Cu : skyHor);

        /* Sky-Sea below Islands (Overworld outer edges only) */
        if (worldMode == 0 && (fabsf(heroX) > 470.0f || fabsf(heroZ) > 410.0f)) {
            float wOff = (float)(animFrame % 256) / 256.0f;
            bind_tex64(texWater, 1, 0);
            Vtx3D *wv = (Vtx3D *)sceGuGetMemory(36 * 6 * sizeof(Vtx3D));
            unsigned int wc = lerp_abgr(ABGR(190, 225, 255, 255), skyHor, 0.35f);
            wc = (wc & 0x00FFFFFFu) | (190u << 24);
            int wk = 0;
            for (int iz = 0; iz < 6; iz++) {
                float wz0 = heroZ - 60.0f + (float)iz * 20.0f, wz1 = wz0 + 20.0f;
                float v0 = (float)iz * 2.5f + wOff, v1 = v0 + 2.5f;
                for (int ix = 0; ix < 6; ix++) {
                    float wx0 = heroX - 60.0f + (float)ix * 20.0f, wx1 = wx0 + 20.0f;
                    float u0 = (float)ix * 2.5f + wOff, u1 = u0 + 2.5f;
                    wv[wk++] = (Vtx3D){ u0, v0, wc, wx0, -3.5f, wz0 };
                    wv[wk++] = (Vtx3D){ u1, v0, wc, wx1, -3.5f, wz0 };
                    wv[wk++] = (Vtx3D){ u1, v1, wc, wx1, -3.5f, wz1 };
                    wv[wk++] = (Vtx3D){ u0, v0, wc, wx0, -3.5f, wz0 };
                    wv[wk++] = (Vtx3D){ u1, v1, wc, wx1, -3.5f, wz1 };
                    wv[wk++] = (Vtx3D){ u0, v1, wc, wx0, -3.5f, wz1 };
                }
            }
            sceGumDrawArray(GU_TRIANGLES, VTYPE_3D, wk, 0, wv);
        }

        /* Platforms (AABB Distance culled for 30 FPS) */
        for (int i = 0; i < nPlats; i++) {
            Platform *p = &plats[i];
            if (fabsf(p->x - heroX) > p->w * 0.5f + 64.0f || fabsf(p->z - heroZ) > p->d * 0.5f + 64.0f) continue;
            const unsigned int *ptex = (p->texType == 0) ? texGrass : (p->texType == 1 ? texStone : (p->texType == 2 ? texObsidian : (p->texType == 3 ? texWood : texCrystal)));
            draw_box3d(p->x, p->y, p->z, p->w, p->h, p->d, ptex, ABGR(255, 255, 255, 255), 0.25f);
        }

        /* 3D Props (LOD culled for 30 FPS) */
        for (int i = 0; i < nProps; i++) {
            Prop3D *pr = &props[i];
            float dx = pr->x - heroX, dz = pr->z - heroZ;
            float d2 = dx * dx + dz * dz;
            if (d2 > 2200.0f) continue;
            if ((pr->kind == 10 || pr->kind == 12) && d2 > 1150.0f) continue;
            if ((pr->kind == 13 || pr->kind == 14) && d2 > 900.0f) continue;
            if (pr->kind == 0) {
                draw_box3d(pr->x, pr->y + pr->h, pr->z, pr->rad * 2, pr->h, pr->rad * 2, texStone, ABGR(220, 220, 235, 255), 0.5f);
            } else if (pr->kind == 1) {
                if (d2 < 360.0f) draw_ground_disc(pr->x, pr->y, pr->z, 1.4f, ABGR(10, 20, 15, 160));
                draw_billboard(pr->x, pr->y, pr->z, pr->h, pr->h * 1.15f, texTree, ABGR(255, 255, 255, 255), 0, camYaw);
            } else if (pr->kind == 2) {
                unsigned int oc = pr->active ? ABGR(140, 255, 255, 255) : ABGR(130, 145, 180, 255);
                draw_box3d(pr->x, pr->y + pr->h, pr->z, pr->rad * 1.6f, pr->h, pr->rad * 1.6f, texCrystal, oc, 0.5f);
            } else if (pr->kind == 3) {
                unsigned int cc = pr->active ? ABGR(130, 130, 130, 255) : ABGR(255, 215, 90, 255);
                draw_box3d(pr->x, pr->y + 0.8f, pr->z, 1.1f, 0.8f, 0.85f, texWood, cc, 1.0f);
            } else if (pr->kind == 4) {
                float bob = sinf((float)animFrame * 0.08f) * 0.2f;
                draw_ground_disc(pr->x, pr->y, pr->z, 1.6f, ABGR(90, 240, 255, 160));
                draw_box3d(pr->x, pr->y + 2.2f + bob, pr->z, 0.85f, 1.6f, 0.85f, texCrystal, ABGR(160, 255, 255, 255), 0.6f);
            } else if (pr->kind == 5) {
                draw_house3d(pr->x, pr->y, pr->z);
            } else if (pr->kind == 6 || pr->kind == 7) {
                unsigned int pc = (pr->kind == 6) ? ABGR(190, 110, 255, 255) : ABGR(100, 255, 190, 255);
                draw_ground_disc(pr->x, pr->y, pr->z, 1.8f, pc);
                draw_box3d(pr->x - 1.1f, pr->y + 3.2f, pr->z, 0.6f, 3.2f, 0.6f, texStone, pc, 0.5f);
                draw_box3d(pr->x + 1.1f, pr->y + 3.2f, pr->z, 0.6f, 3.2f, 0.6f, texStone, pc, 0.5f);
                draw_box3d(pr->x, pr->y + 3.6f, pr->z, 2.8f, 0.5f, 0.7f, texCrystal, pc, 0.5f);
            } else if (pr->kind == 10) {
                draw_ground_decal(pr->x, pr->y, pr->z, pr->rad, pr->id);
            } else if (pr->kind == 11) {
                float bob = sinf((float)animFrame * 0.07f + pr->x) * 0.12f;
                unsigned int lc = (pr->id == 1) ? ABGR(120, 235, 255, 225) : ((pr->id == 2) ? ABGR(255, 125, 70, 225) : ABGR(255, 215, 110, 225));
                draw_box3d(pr->x, pr->y + 1.5f, pr->z, 0.45f, 1.5f, 0.45f, texStone, ABGR(210, 215, 230, 255), 0.6f);
                draw_billboard(pr->x, pr->y + 1.85f + bob, pr->z, 1.35f, 1.35f, texSoftGlow, lc, 0, camYaw);
            } else if (pr->kind == 12) {
                draw_billboard(pr->x, pr->y, pr->z, pr->h * 1.35f, pr->h, texTree, ABGR(210, 255, 195, 255), 0, camYaw);
            } else if (pr->kind == 13) {
                draw_box3d(pr->x, pr->y + pr->h, pr->z, pr->rad * 2.0f, pr->h, pr->rad * 1.8f, texStone, ABGR(185, 195, 200, 255), 0.7f);
            } else if (pr->kind == 14) {
                draw_billboard(pr->x, pr->y, pr->z, 1.15f, pr->h, texTree, ABGR(150, 255, 130, 255), 0, camYaw);
            } else if (pr->kind == 8) {
                unsigned int lc = pr->active ? ABGR(140, 235, 255, 255) : ABGR(255, 215, 95, 255);
                draw_ground_disc(pr->x, pr->y, pr->z, 1.0f, lc);
                draw_box3d(pr->x, pr->y + 1.8f, pr->z, 0.9f, 1.8f, 0.45f, texStone, lc, 0.7f);
            } else if (pr->kind == 9) {
                unsigned int wc = ABGR(255, 210, 90, 255);
                draw_ground_disc(pr->x, pr->y, pr->z, 1.5f, ABGR(110, 215, 255, 190));
                draw_box3d(pr->x - 0.9f, pr->y + 2.6f, pr->z, 0.45f, 2.6f, 0.45f, texCrystal, wc, 0.5f);
                draw_box3d(pr->x + 0.9f, pr->y + 2.6f, pr->z, 0.45f, 2.6f, 0.45f, texCrystal, wc, 0.5f);
                draw_box3d(pr->x, pr->y + 2.9f, pr->z, 2.2f, 0.4f, 0.5f, texCrystal, ABGR(140, 240, 255, 255), 0.5f);
            }
        }

        /* Living NPCs (gentle wandering + WoW Quest Markers ! / ?) */
        for (int i = 0; i < nNpcs; i++) {
            Npc3D *n = &npcs[i];
            n->x = n->homeX + sinf((float)animFrame * 0.018f + (float)i * 1.7f) * 0.9f;
            n->z = n->homeZ + cosf((float)animFrame * 0.015f + (float)i * 2.1f) * 0.6f;
            float dx = n->x - heroX, dz = n->z - heroZ;
            if (dx * dx + dz * dz > 2500.0f) continue;
            draw_ground_disc(n->x, n->y, n->z, 0.8f, ABGR(10, 15, 25, 170));
            draw_billboard(n->x, n->y, n->z, 1.5f, 1.75f, texNpc, ABGR(255, 255, 255, 255), (n->x > heroX) ? 1 : 0, camYaw);
            int mk = get_npc_marker(n->npcId);
            if (mk > 0) {
                float bobQ = sinf((float)animFrame * 0.12f + (float)i) * 0.12f;
                unsigned int mc = (mk == 1) ? ABGR(255, 235, 65, 255) : (mk == 3 ? ABGR(120, 255, 145, 255) : ABGR(190, 205, 230, 220));
                draw_box3d(n->x, n->y + 2.35f + bobQ, n->z, 0.28f, 0.36f, 0.28f, texCrystal, mc, 1.0f);
                draw_billboard(n->x, n->y + 2.1f + bobQ, n->z, 0.55f, 0.55f, texSoftGlow, mc, 0, camYaw);
            }
        }

        /* Enemies & Bosses */
        for (int i = 0; i < nEnemies; i++) {
            Enemy3D *e = &enemies[i];
            if (!e->alive) continue;
            float dx = e->x - heroX, dz = e->z - heroZ;
            if (dx * dx + dz * dz > 2500.0f) continue;
            float sz = e->isBoss ? 3.4f : 1.7f;
            if (e->state == 1) {
                /* Enemy Red Danger Telegraph Zone + Timing Ring! */
                float prog = 1.0f - ((float)e->timer / (float)(e->isBoss ? 28 : 22));
                draw_ground_disc(e->atkX, e->y + 0.02f, e->atkZ, e->atkRad, ABGR(255, 40, 40, 185));
                draw_ground_disc(e->atkX, e->y + 0.04f, e->atkZ, e->atkRad * prog, ABGR(255, 165, 45, 220));
            }
            draw_ground_disc(e->x, e->y, e->z, sz * 0.55f, ABGR(180, 25, 45, 165));
            unsigned int ec = e->flash > 0 ? ABGR(255, 90, 90, 255) : (e->state == 1 ? ABGR(255, 195, 140, 255) : ABGR(255, 255, 255, 255));
            const unsigned int *et = e->isBoss ? texBoss[e->bossId % 3] : texMob[e->kind % 3];
            draw_billboard(e->x, e->y, e->z, sz, sz, et, ec, (e->x > heroX) ? 1 : 0, camYaw);
        }

        /* Hero Aim Hit-Zone Telegraph + Hero + Lumen Wisp */
        if (!inTitle) {
            float aimX = heroX + sinf(heroYaw) * 1.85f;
            float aimZ = heroZ + cosf(heroYaw) * 1.85f;
            unsigned int aimCol = (atkT > 0) ? ABGR(255, 240, 90, 235) : ABGR(110, 225, 255, 120);
            draw_ground_disc(aimX, heroY + 0.02f, aimZ, (atkT > 0) ? 2.1f : 1.45f, aimCol);
            if (heavyT > 0) {
                draw_ground_disc(heroX, heroY + 0.03f, heroZ, 3.8f, ABGR(130, 245, 255, 180));
            }
        }
        float hBob = fabsf(sinf((float)animFrame * 0.12f)) * 0.08f;
        draw_ground_disc(heroX, heroY, heroZ, 0.85f, ABGR(10, 15, 25, 180));
        unsigned int hc = heroFlash > 0 ? ABGR(255, 90, 90, 255) : ABGR(255, 255, 255, 255);
        draw_billboard(heroX, heroY + hBob, heroZ, 1.6f, 1.85f, texHero, hc, heroFlip, camYaw);

        float wx = heroX + cosf((float)animFrame * 0.06f) * 0.95f;
        float wz = heroZ + sinf((float)animFrame * 0.06f) * 0.95f;
        float wy = heroY + 1.55f + sinf((float)animFrame * 0.10f) * 0.18f;
        draw_billboard(wx, wy, wz, 0.55f, 0.55f, texSoftGlow, ABGR(170, 245, 255, 240), 0, camYaw);

        for (int i = 0; i < MAX_PARTS; i++) {
            if (parts[i].life > 0.0f) {
                draw_billboard(parts[i].x, parts[i].y, parts[i].z, parts[i].size, parts[i].size, texSoftGlow, parts[i].col, 0, camYaw);
            }
        }

        /* ==================== HARDWARE 2D GPU OVERLAY ==================== */
        sceGuDisable(GU_DEPTH_TEST);
        sceGuDisable(GU_CLIP_PLANES);
        sceGuDisable(GU_FOG);

        if (inTitle) {
            gu_window2d(54, 34, 372, 82);
            gu_text2d(104, 46, "AETHERIA", ABGR(255, 225, 110, 255), 3);
            gu_text2d(114, 84, "MUNDO 3D: PUEBLOS Y MAZMORRAS", ABGR(145, 235, 255, 255), 1);

            gu_window2d(96, 194, 288, 52);
            if (hasSave) {
                char sb[48]; snprintf(sb, sizeof(sb), "X: CONTINUAR (NV.%d - %d ORO)", heroLv, heroGold);
                gu_text2d(114, 206, sb, ABGR(120, 255, 160, 255), 1);
                gu_text2d(132, 224, "START: NUEVA PARTIDA", ABGR(255, 230, 110, 255), 1);
            } else {
                gu_text2d(136, 208, "PULSA X O START PARA VIAJAR", ABGR(255, 240, 120, 255), 1);
                gu_text2d(124, 224, "3 MAZMORRAS + PUEBLOS + 60 FPS", ABGR(140, 255, 175, 255), 1);
            }
        } else {
            /* Compact Vertical Left-Edge HUD (unobstructed center view!) */
            gu_window2d(6, 6, 72, 152);
            char buf[80];
            snprintf(buf, sizeof(buf), "NV.%d", heroLv);
            gu_text2d(14, 10, buf, ABGR(140, 255, 175, 255), 1);

            /* 4 Vertical Bars: HP, Stamina, Furor, Food */
            const int barY = 22, barH = 60, barW = 8;
            int hpFill = (heroHp * barH) / (heroMaxHp > 0 ? heroMaxHp : 1);
            if (hpFill < 0) hpFill = 0; if (hpFill > barH) hpFill = barH;
            gu_rect2d(13, barY, barW, barH, ABGR(20, 24, 40, 255));
            gu_rect2d(14, barY + barH - hpFill, barW - 2, hpFill, ABGR(70, 235, 125, 255));
            gu_text2d(11, barY + barH + 3, "HP", ABGR(120, 255, 155, 255), 1);

            int stFill = (int)((heroStam * (float)barH) / heroMaxStam);
            if (stFill < 0) stFill = 0; if (stFill > barH) stFill = barH;
            gu_rect2d(27, barY, barW, barH, ABGR(20, 24, 40, 255));
            gu_rect2d(28, barY + barH - stFill, barW - 2, stFill, ABGR(215, 235, 75, 255));
            gu_text2d(25, barY + barH + 3, "ST", ABGR(215, 235, 110, 255), 1);

            int frFill = (furorCharge * barH) / 100;
            if (frFill < 0) frFill = 0; if (frFill > barH) frFill = barH;
            gu_rect2d(41, barY, barW, barH, ABGR(20, 24, 40, 255));
            gu_rect2d(42, barY + barH - frFill, barW - 2, frFill, furorCharge >= 100 ? ABGR(255, 110, 235, 255) : ABGR(120, 195, 255, 255));
            gu_text2d(39, barY + barH + 3, (furorCharge >= 100 && !ghActive && (animFrame & 16)) ? "LR" : "FR",
                      furorCharge >= 100 ? ABGR(255, 235, 80, 255) : ABGR(160, 210, 255, 255), 1);

            int cmFill = (int)((heroHunger * (float)barH) / 100.0f);
            if (cmFill < 0) cmFill = 0; if (cmFill > barH) cmFill = barH;
            gu_rect2d(55, barY, barW, barH, ABGR(20, 24, 40, 255));
            gu_rect2d(56, barY + barH - cmFill, barW - 2, cmFill, (heroHunger < 25.0f) ? ABGR(255, 85, 85, 255) : ABGR(255, 195, 80, 255));
            gu_text2d(53, barY + barH + 3, "CM", (heroHunger < 25.0f) ? ABGR(255, 95, 95, 255) : ABGR(255, 220, 120, 255), 1);

            snprintf(buf, sizeof(buf), "O:%d", heroGold);
            gu_text2d(12, 98, buf, ABGR(255, 225, 110, 255), 1);
            snprintf(buf, sizeof(buf), "M:%d", heroCoins);
            gu_text2d(12, 108, buf, ABGR(255, 200, 120, 255), 1);
            snprintf(buf, sizeof(buf), "P:%d R:%d", heroPotions, heroFood);
            gu_text2d(12, 118, buf, ABGR(185, 235, 255, 255), 1);
            snprintf(buf, sizeof(buf), "E:%d L:%d", heroElixirs, keysFound);
            gu_text2d(12, 128, buf, ABGR(185, 235, 255, 255), 1);
            snprintf(buf, sizeof(buf), "PS:%d/6", cluesCount);
            gu_text2d(12, 138, buf, ABGR(145, 245, 255, 255), 1);

            /* Hero-Centered Open-World Radar Minimap + FPS + Clock */
            gu_window2d(406, 6, 68, 68);
            const float mScale = 0.14f;
            for (int i = 0; i < nPlats; i++) {
                int mx = 440 + (int)((plats[i].x - heroX) * mScale);
                int mz = 40  + (int)((plats[i].z - heroZ) * mScale);
                int mw = (int)(plats[i].w * mScale); if (mw < 2) mw = 2;
                int md = (int)(plats[i].d * mScale); if (md < 2) md = 2;
                int x0 = mx - mw/2, y0 = mz - md/2, x1 = mx + mw/2, y1 = mz + md/2;
                if (x1 < 408 || x0 > 472 || y1 < 8 || y0 > 72) continue;
                if (x0 < 408) x0 = 408;
                if (x1 > 472) x1 = 472;
                if (y0 < 8) y0 = 8;
                if (y1 > 72) y1 = 72;
                gu_rect2d(x0, y0, x1 - x0, y1 - y0, ABGR(90, 140, 210, 170));
            }
            for (int i = 0; i < nNpcs; i++) {
                int nx = 440 + (int)((npcs[i].x - heroX) * mScale);
                int nz = 40  + (int)((npcs[i].z - heroZ) * mScale);
                if (nx >= 409 && nx <= 470 && nz >= 9 && nz <= 70)
                    gu_rect2d(nx - 1, nz - 1, 2, 2, ABGR(100, 245, 255, 255));
            }
            for (int i = 0; i < nEnemies; i++) {
                if (!enemies[i].alive) continue;
                int ex = 440 + (int)((enemies[i].x - heroX) * mScale);
                int ez = 40  + (int)((enemies[i].z - heroZ) * mScale);
                if (ex >= 409 && ex <= 470 && ez >= 9 && ez <= 70)
                    gu_rect2d(ex - 1, ez - 1, 2, 2, ABGR(255, 80, 80, 255));
            }
            gu_rect2d(439, 39, 3, 3, ABGR(255, 240, 80, 255));

            gu_window2d(336, 6, 64, 20);
            snprintf(buf, sizeof(buf), "FPS:%2d", curFps);
            gu_text2d(344, 12, buf, curFps >= 28 ? ABGR(110, 255, 140, 255) : ABGR(255, 225, 90, 255), 1);

            gu_window2d(336, 28, 64, 20);
            snprintf(buf, sizeof(buf), "%02d:%02d", curHour, curMin);
            gu_text2d(349, 34, buf, (nightFactor > 0.5f) ? ABGR(160, 205, 255, 255) : ABGR(255, 230, 120, 255), 1);

            gu_window2d(6, 246, 468, 20);
            gu_text2d(12, 252, "X:ESPADA []:DASH /\\:PULSO O:TIENDA/ENTRAR SELECT:DIARIO START:USAR", ABGR(200, 225, 255, 255), 1);

            if (bannerT > 0) {
                gu_window2d(84, 8, 246, 24);
                gu_text2d(94, 16, bannerMsg, ABGR(255, 230, 110, 255), 1);
            }
            if (dialogT > 0 && !storyOpen) {
                gu_window2d(24, 204, 432, 34);
                gu_text2d(34, 217, dialogMsg, ABGR(255, 255, 255, 255), 1);
            }
            if (storyOpen) {
                gu_window2d(28, 76, 424, 120);
                gu_text2d(44, 90, storyTitle, ABGR(255, 225, 95, 255), 1);
                gu_text2d(44, 114, storyL1, ABGR(240, 245, 255, 255), 1);
                gu_text2d(44, 132, storyL2, ABGR(240, 245, 255, 255), 1);
                gu_text2d(44, 150, storyL3, ABGR(145, 240, 255, 255), 1);
                gu_text2d(150, 176, "[ PULSA X o O PARA CONTINUAR ]", ABGR(255, 225, 110, 255), 1);
            }
            if (shopOpen) {
                static const char *sNames[4] = {
                    "RACION DE VIAJE   (+50% Comida, +20 HP)",
                    "POCION CURATIVA   (+60 HP)",
                    "ELIXIR ESTAMINA   (Estamina 100%, +40 HP)",
                    "PACK SUPERVIVENCIA(+2 Rac, +1 Poc, +25% Com)"
                };
                static const int sPrices[4] = { 10, 18, 22, 35 };
                gu_window2d(34, 68, 412, 130);
                snprintf(buf, sizeof(buf), "TIENDA DE SUPERVIVENCIA   |   MONEDAS: %d", heroCoins);
                gu_text2d(48, 80, buf, ABGR(255, 225, 95, 255), 1);
                for (int s = 0; s < 4; s++) {
                    int ry = 102 + s * 19;
                    if (s == shopCursor) gu_rect2d(44, ry - 3, 392, 15, ABGR(55, 85, 145, 210));
                    snprintf(buf, sizeof(buf), "%s %-40s %2dM", (s == shopCursor) ? ">" : " ", sNames[s], sPrices[s]);
                    unsigned int col = (heroCoins >= sPrices[s]) ? ABGR(235, 250, 255, 255) : ABGR(160, 120, 120, 255);
                    gu_text2d(48, ry, buf, col, 1);
                }
                gu_text2d(92, 182, "[ARRIBA/ABAJO] ELEGIR   [X] COMPRAR   [O] SALIR", ABGR(145, 240, 255, 255), 1);
            }
            if (ghActive) {
                static const unsigned int lCols[4] = {
                    ABGR(235, 95, 225, 255), ABGR(80, 235, 130, 255),
                    ABGR(255, 95, 95, 255),  ABGR(95, 165, 255, 255)
                };
                static const char *lLbls[4] = { "[]", "/\\", "O", "X" };
                gu_window2d(140, 52, 200, 186);
                gu_text2d(156, 58, "SINFONIA CRITICA!", ABGR(255, 225, 90, 255), 1);
                char mb[32]; snprintf(mb, sizeof(mb), "PODER: x%.1f", ghMult);
                gu_text2d(268, 58, mb, ABGR(120, 255, 175, 255), 1);
                for (int l = 0; l < 4; l++) {
                    int lx = 152 + l * 46;
                    gu_rect2d(lx + 14, 72, 2, 148, ABGR(70, 90, 140, 220));
                    gu_rect2d(lx + 2, GH_HIT_Y - 7, 26, 14, ABGR(25, 35, 65, 220));
                    gu_text2d(lx + 9, GH_HIT_Y - 3, lLbls[l], lCols[l], 1);
                }
                for (int i = 0; i < GH_MAX; i++) {
                    if (!ghNotes[i].active) continue;
                    int nx = 154 + ghNotes[i].lane * 46;
                    int ny = (int)ghNotes[i].y;
                    gu_rect2d(nx, ny - 5, 26, 10, lCols[ghNotes[i].lane]);
                }
                if (ghJudgeT > 0) gu_text2d(196, 86, ghJudge, ABGR(255, 240, 100, 255), 1);
            }
        }

        sceGuFinish();
        sceGuSync(0, 0);

        fpsFrames++;
        SceInt64 now = sceKernelGetSystemTimeWide();
        if (now - fpsLastT >= 500000) {
            curFps = (int)((fpsFrames * 1000000LL) / (now - fpsLastT));
            fpsFrames = 0;
            fpsLastT = now;
        }

        sceDisplayWaitVblankStart();
        if (sceKernelGetSystemTimeWide() - frameStart < 24000) sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
    }

    arun = 0;
    if (ath >= 0) sceKernelWaitThreadEnd(ath, NULL);
    if (ach >= 0) sceAudioChRelease(ach);
    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
