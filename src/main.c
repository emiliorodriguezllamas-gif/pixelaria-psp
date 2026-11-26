#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspaudio.h>
#include <psppower.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"
#include "world_sprites.h"

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

#define VTYPE_3D     (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D)
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
    float x, y, z;
    int role; /* 0=mayor, 1=blacksmith(+ATK for 30g), 2=alchemist(potion 20g), 3=sage */
    const char *name;
    const char *msg;
} Npc3D;

typedef struct {
    float x, y, z;
    int hp, maxHp, kind, alive, flash, cd, isBoss, bossId;
} Enemy3D;

typedef struct {
    float x, y, z, vx, vy, vz, life, maxLife, size;
    unsigned int col;
} Particle3D;

#define MAX_PLATS 64
#define MAX_PROPS 128
#define MAX_ENEMIES 28
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

static float heroX = 0.0f, heroY = 0.0f, heroZ = 6.0f;
static float saveOvX = 0.0f, saveOvY = 0.0f, saveOvZ = 6.0f;
static float camYaw = 0.0f;
static int heroFlip = 0, heroHp = 130, heroMaxHp = 130, heroAtk = 22;
static int heroGold = 40, heroPotions = 2, heroLv = 1, heroXp = 0;
static int heroRelics = 0, runesLit = 0, keysFound = 0;
static int dashT = 0, atkT = 0, pulseT = 0, invulnT = 0, heroFlash = 0;
static float dashDx = 0.0f, dashDz = 0.0f;
static int worldMode = 0; /* 0=Overworld, 1=Dungeon */
static int curDungeon = 0, dngRunes = 0;
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
    props[nProps++] = (Prop3D){ x, y, z, rad, h, kind, act, id };
}

static void add_npc(float x, float y, float z, int role, const char *name, const char *msg) {
    if (nNpcs >= MAX_NPCS) return;
    npcs[nNpcs++] = (Npc3D){ x, y, z, role, name, msg };
}

static void add_enemy(float x, float y, float z, int kind, int isBoss, int bossId) {
    if (nEnemies >= MAX_ENEMIES) return;
    int hp = isBoss ? (180 + bossId * 110) : (45 + kind * 28);
    enemies[nEnemies++] = (Enemy3D){ x, y, z, hp, hp, kind, 1, 0, 0, isBoss, bossId };
}

static void spawn_part(float x, float y, float z, float vx, float vy, float vz, float life, float sz, unsigned int col) {
    for (int i = 0; i < MAX_PARTS; i++) {
        if (parts[i].life <= 0.0f) {
            parts[i] = (Particle3D){ x, y, z, vx, vy, vz, life, life, sz, col };
            return;
        }
    }
}

static void build_overworld(void) {
    nPlats = 0; nProps = 0; nEnemies = 0; nNpcs = 0;
    worldMode = 0;

    /* 1. PUEBLO DEL ALBA (Starter Town: x=-14..14, z=-14..14) */
    add_plat(0.0f, 0.0f, 0.0f, 28.0f, 28.0f, 2.5f, 1, 0);
    add_plat(0.0f, 0.20f, 0.0f, 8.0f, 8.0f, 0.20f, 4, 0); /* Central Crystal Plaza */
    add_prop(0.0f, 0.20f, 0.0f, 0.7f, 2.5f, 4, 0); /* Sanctuary Crystal */
    add_prop(-9.5f, 0.0f, -8.5f, 2.2f, 3.0f, 5, 0); /* Town House 1 */
    add_prop( 9.5f, 0.0f, -8.5f, 2.2f, 3.0f, 5, 1); /* Town House 2 */
    add_prop(-9.5f, 0.0f,  8.5f, 2.2f, 3.0f, 5, 2); /* Town House 3 */
    add_prop( 9.5f, 0.0f,  8.5f, 2.2f, 3.0f, 5, 3); /* Town House 4 */
    add_prop(-11.5f, 0.0f, 0.0f, 1.1f, 3.2f, 1, 0);
    add_prop( 11.5f, 0.0f, 0.0f, 1.1f, 3.2f, 1, 0);
    add_prop( 11.0f, 0.0f, 11.0f, 0.6f, 0.9f, 3, 0); /* Starter Chest */
    add_npc(-3.2f, 0.20f,  2.8f, 0, "Alcalde Eldon", "ALCALDE: Encuentra las 3 Llaves en los Templos del Norte, Oeste y Este!");
    add_npc(-6.2f, 0.0f,  -6.5f, 1, "Herrero Kael",  "KAEL: Forjar Espada (+6 ATK) por 30 Oro!");
    add_npc( 6.2f, 0.0f,  -6.5f, 2, "Alquimista Lyra","LYRA: Pocion (+60 HP) por 20 Oro!");
    add_npc( 3.2f, 0.20f,  2.8f, 3, "Sabio Orin",    "ORIN: Usa /\\ cerca de los Obeliscos para iluminarlos y ganar vida!");

    /* 2. BOSQUE ESMERALDA (West: x=-48..-22, z=-12..12) */
    add_plat(-18.0f, -0.04f, 0.0f, 8.0f, 4.5f, 0.6f, 3, 1); /* West Bridge */
    add_plat(-35.0f,  0.0f,  0.0f, 26.0f, 26.0f, 2.5f, 0, 1);
    add_prop(-28.0f, 0.0f, -8.0f, 1.2f, 3.6f, 1, 0);
    add_prop(-42.0f, 0.0f, -8.0f, 1.2f, 3.8f, 1, 0);
    add_prop(-28.0f, 0.0f,  8.0f, 1.2f, 3.4f, 1, 0);
    add_prop(-42.0f, 0.0f,  8.0f, 1.2f, 3.5f, 1, 0);
    add_prop(-35.0f, 0.0f,  0.0f, 0.7f, 3.0f, 2, 0); /* Obelisk 0 */
    add_prop(-44.0f, 0.0f,  9.5f, 0.6f, 0.9f, 3, 1); /* Chest 1 */
    add_prop(-44.0f, 0.0f, -9.5f, 0.6f, 0.9f, 3, 2); /* Chest 2 */
    add_prop(-46.5f, 0.0f,  0.0f, 1.1f, 3.4f, 6, 0); /* DUNGEON 0 PORTAL: Templo del Bosque */
    add_enemy(-30.0f, 0.0f, -4.0f, 0, 0, 0);
    add_enemy(-38.0f, 0.0f,  5.0f, 0, 0, 0);
    add_enemy(-41.0f, 0.0f, -3.5f, 1, 0, 0);

    /* 3. LAGO DE LOS ECOS & ALDEA LACUSTRE (North: x=-13..13, z=-48..-22) */
    add_plat(0.0f, -0.04f, -18.0f, 4.5f, 8.0f, 0.6f, 3, 1); /* North Bridge */
    add_plat(0.0f,  0.0f,  -35.0f, 26.0f, 26.0f, 2.5f, 1, 1);
    add_prop(-8.5f, 0.0f, -27.0f, 2.0f, 2.8f, 5, 4); /* Lake Village House */
    add_prop( 8.5f, 0.0f, -27.0f, 2.0f, 2.8f, 5, 5);
    add_prop( 0.0f, 0.0f, -29.0f, 0.7f, 2.5f, 4, 0); /* Lake Crystal */
    add_prop(-9.0f, 0.0f, -41.0f, 0.7f, 3.0f, 2, 1); /* Obelisk 1 */
    add_prop( 9.5f, 0.0f, -42.0f, 0.6f, 0.9f, 3, 3); /* Chest 3 */
    add_prop( 0.0f, 0.0f, -46.5f, 1.1f, 3.4f, 6, 1); /* DUNGEON 1 PORTAL: Ruinas del Agua */
    add_npc(-6.0f, 0.0f, -25.0f, 2, "Mercader Nilo", "NILO: Pocion (+60 HP) por 20 Oro!");
    add_enemy(-5.0f, 0.0f, -36.0f, 1, 0, 0);
    add_enemy( 5.5f, 0.0f, -38.0f, 1, 0, 0);

    /* 4. CANON CARMESI & CAMPAMENTO MINERO (East: x=22..48, z=-13..13) */
    add_plat(18.0f, -0.04f, 0.0f, 8.0f, 4.5f, 0.6f, 3, 2); /* East Bridge */
    add_plat(35.0f,  0.0f,  0.0f, 26.0f, 26.0f, 2.5f, 2, 2);
    add_prop(27.0f, 0.0f, -8.5f, 2.0f, 2.8f, 5, 6); /* Mining Outpost */
    add_prop(28.0f, 0.0f,  7.0f, 0.6f, 3.4f, 0, 0);
    add_prop(42.0f, 0.0f, -7.0f, 0.6f, 3.4f, 0, 0);
    add_prop(42.0f, 0.0f,  7.0f, 0.6f, 3.4f, 0, 0);
    add_prop(35.0f, 0.0f, -9.0f, 0.7f, 3.0f, 2, 2); /* Obelisk 2 */
    add_prop(44.0f, 0.0f,  9.0f, 0.6f, 0.9f, 3, 4); /* Chest 4 */
    add_prop(46.5f, 0.0f,  0.0f, 1.1f, 3.4f, 6, 2); /* DUNGEON 2 PORTAL: Forja de Obsidiana */
    add_npc(27.0f, 0.0f, -5.0f, 1, "Forjador Vulcan", "VULCAN: Forjar Espada (+6 ATK) por 30 Oro!");
    add_enemy(34.0f, 0.0f,  4.0f, 2, 0, 0);
    add_enemy(40.0f, 0.0f, -3.5f, 2, 0, 0);
    add_enemy(41.0f, 0.0f,  5.0f, 1, 0, 0);

    /* 5. CIUDADELA ASTRAL (Far North: x=-14..14, z=-84..-58) */
    add_plat(0.0f, 0.15f, -53.0f, 5.0f, 10.0f, 0.7f, 4, 3); /* Astral Bridge */
    add_plat(0.0f, 0.30f, -71.0f, 28.0f, 26.0f, 3.0f, 4, 3);
    add_prop(-9.0f, 0.30f, -63.0f, 0.7f, 4.2f, 2, 3); /* Obelisk 3 */
    add_prop( 9.0f, 0.30f, -63.0f, 0.7f, 4.2f, 2, 4); /* Obelisk 4 */
    add_prop(-9.0f, 0.30f, -79.0f, 0.7f, 4.2f, 0, 0);
    add_prop( 9.0f, 0.30f, -79.0f, 0.7f, 4.2f, 0, 0);
    add_prop( 0.0f, 0.30f, -81.0f, 0.7f, 2.8f, 4, 0); /* Astral Crystal */
    add_prop( 0.0f, 0.30f, -66.0f, 0.6f, 0.9f, 3, 5); /* Chest 5 */
    add_enemy(0.0f, 0.30f, -73.0f, 2, 1, 2); /* Final Sky Dragon Boss */
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
    add_prop(-6.5f, 0.0f, -26.0f, 0.7f, 3.0f, 2, 6 + dId * 2);     /* Dungeon Rune 1 */
    add_prop( 6.5f, 0.0f, -26.0f, 0.7f, 3.0f, 2, 6 + dId * 2 + 1); /* Dungeon Rune 2 */
    add_prop(-7.5f, 0.0f, -31.0f, 0.6f, 0.9f, 3, 6 + dId * 2);     /* Dungeon Chest 1 */
    add_prop( 7.5f, 0.0f, -21.0f, 0.6f, 0.9f, 3, 6 + dId * 2 + 1); /* Dungeon Chest 2 */

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
    if (heroZ < -50.0f) return 3;
    if (heroX > 18.0f) return 2;
    if (heroZ < -18.0f) return 1;
    if (heroX < -18.0f) return 1;
    return 0;
}

static int is_walkable(float nx, float nz, float *outY) {
    int found = 0;
    float bestY = -100.0f;
    for (int i = 0; i < nPlats; i++) {
        Platform *p = &plats[i];
        if (nx >= p->x - p->w * 0.5f && nx <= p->x + p->w * 0.5f &&
            nz >= p->z - p->d * 0.5f && nz <= p->z + p->d * 0.5f) {
            if (!found || p->y > bestY) { bestY = p->y; found = 1; }
        }
    }
    if (found) *outY = bestY;
    return found;
}

static unsigned int shade_vtx(float vx, float vy, float vz, unsigned int baseCol) {
    int br = baseCol & 0xFF, bg = (baseCol >> 8) & 0xFF, bb = (baseCol >> 16) & 0xFF;
    float dx = vx - heroX, dz = vz - heroZ;
    float d2 = dx * dx + dz * dz;
    int addR = 0, addG = 0, addB = 0;
    if (d2 < 36.0f) {
        float w = (36.0f - d2) / 36.0f;
        addR += (int)(w * 54.0f); addG += (int)(w * 48.0f); addB += (int)(w * 30.0f);
    }
    if (pulseT > 0) {
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
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, 64, 64, 64, tex);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);

    float hw = w * 0.5f, hd = d * 0.5f, y0 = cy - h, y1 = cy;
    unsigned int cTop = shade_vtx(cx, y1, cz, col);
    unsigned int cSide = ABGR(((col & 0xFF) * 7) / 10, (((col >> 8) & 0xFF) * 7) / 10, (((col >> 16) & 0xFF) * 7) / 10, 255);
    unsigned int cDark = ABGR(((col & 0xFF) * 5) / 10, (((col >> 8) & 0xFF) * 5) / 10, (((col >> 16) & 0xFF) * 5) / 10, 255);

    Vtx3D *v = (Vtx3D *)sceGuGetMemory(30 * sizeof(Vtx3D));
    float uW = w * uvScale, uD = d * uvScale, uH = h * uvScale;
    int k = 0;
    v[k++] = (Vtx3D){ 0,  0,  cTop, cx-hw, y1, cz-hd }; v[k++] = (Vtx3D){ uW, 0,  cTop, cx+hw, y1, cz-hd }; v[k++] = (Vtx3D){ uW, uD, cTop, cx+hw, y1, cz+hd };
    v[k++] = (Vtx3D){ 0,  0,  cTop, cx-hw, y1, cz-hd }; v[k++] = (Vtx3D){ uW, uD, cTop, cx+hw, y1, cz+hd }; v[k++] = (Vtx3D){ 0,  uD, cTop, cx-hw, y1, cz+hd };
    v[k++] = (Vtx3D){ 0,  0,  cSide, cx-hw, y1, cz+hd }; v[k++] = (Vtx3D){ uW, 0,  cSide, cx+hw, y1, cz+hd }; v[k++] = (Vtx3D){ uW, uH, cDark, cx+hw, y0, cz+hd };
    v[k++] = (Vtx3D){ 0,  0,  cSide, cx-hw, y1, cz+hd }; v[k++] = (Vtx3D){ uW, uH, cDark, cx+hw, y0, cz+hd }; v[k++] = (Vtx3D){ 0,  uH, cDark, cx-hw, y0, cz+hd };
    v[k++] = (Vtx3D){ 0,  0,  cSide, cx+hw, y1, cz-hd }; v[k++] = (Vtx3D){ uW, 0,  cSide, cx-hw, y1, cz-hd }; v[k++] = (Vtx3D){ uW, uH, cDark, cx-hw, y0, cz-hd };
    v[k++] = (Vtx3D){ 0,  0,  cSide, cx+hw, y1, cz-hd }; v[k++] = (Vtx3D){ uW, uH, cDark, cx-hw, y0, cz-hd }; v[k++] = (Vtx3D){ 0,  uH, cDark, cx+hw, y0, cz-hd };
    v[k++] = (Vtx3D){ 0,  0,  cSide, cx-hw, y1, cz-hd }; v[k++] = (Vtx3D){ uD, 0,  cSide, cx-hw, y1, cz+hd }; v[k++] = (Vtx3D){ uD, uH, cDark, cx-hw, y0, cz+hd };
    v[k++] = (Vtx3D){ 0,  0,  cSide, cx-hw, y1, cz-hd }; v[k++] = (Vtx3D){ uD, uH, cDark, cx-hw, y0, cz+hd }; v[k++] = (Vtx3D){ 0,  uH, cDark, cx-hw, y0, cz-hd };
    v[k++] = (Vtx3D){ 0,  0,  cSide, cx+hw, y1, cz+hd }; v[k++] = (Vtx3D){ uD, 0,  cSide, cx+hw, y1, cz-hd }; v[k++] = (Vtx3D){ uD, uH, cDark, cx+hw, y0, cz-hd };
    v[k++] = (Vtx3D){ 0,  0,  cSide, cx+hw, y1, cz+hd }; v[k++] = (Vtx3D){ uD, uH, cDark, cx+hw, y0, cz-hd }; v[k++] = (Vtx3D){ 0,  uH, cDark, cx+hw, y0, cz+hd };
    sceGumDrawArray(GU_TRIANGLES, VTYPE_3D, 30, 0, v);
}

static void draw_house3d(float cx, float cy, float cz) {
    /* Walls + Roof + Warm Door */
    draw_box3d(cx, cy + 2.2f, cz, 3.4f, 2.2f, 3.0f, texStone, ABGR(240, 232, 215, 255), 0.5f);
    draw_box3d(cx, cy + 3.1f, cz, 3.9f, 0.9f, 3.5f, texObsidian, ABGR(220, 120, 95, 255), 0.5f);
    draw_box3d(cx, cy + 3.7f, cz, 2.4f, 0.6f, 2.1f, texObsidian, ABGR(240, 140, 105, 255), 0.5f);
    draw_box3d(cx, cy + 1.4f, cz + 1.52f, 0.9f, 1.4f, 0.1f, texWood, ABGR(255, 215, 130, 255), 1.0f);
}

static void draw_billboard(float x, float y, float z, float w, float h, const unsigned int *tex, unsigned int col, int flipX, float camAngle) {
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, 64, 64, 64, tex);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);

    float rx = cosf(camAngle) * (w * 0.5f), rz = -sinf(camAngle) * (w * 0.5f);
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
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, 64, 64, 64, texSoftGlow);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);

    Vtx3D *v = (Vtx3D *)sceGuGetMemory(6 * sizeof(Vtx3D));
    v[0] = (Vtx3D){ 0, 0, col, x - r, y + 0.03f, z - r };
    v[1] = (Vtx3D){ 1, 0, col, x + r, y + 0.03f, z - r };
    v[2] = (Vtx3D){ 1, 1, col, x + r, y + 0.03f, z + r };
    v[3] = (Vtx3D){ 0, 0, col, x - r, y + 0.03f, z - r };
    v[4] = (Vtx3D){ 1, 1, col, x + r, y + 0.03f, z + r };
    v[5] = (Vtx3D){ 0, 1, col, x - r, y + 0.03f, z + r };
    sceGumDrawArray(GU_TRIANGLES, VTYPE_3D, 6, 0, v);
}

int main(void) {
    scePowerSetClockFrequency(333, 333, 166);
    int th = sceKernelCreateThread("cb", cb_th, 0x11, 0xFA0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    build_textures();
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
        sceCtrlPeekBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~oldPad.Buttons;
        oldPad = pad;
        animFrame++;

        if (inTitle) {
            camYaw += 0.004f;
            if (pressed & (PSP_CTRL_CROSS | PSP_CTRL_START)) {
                inTitle = 0;
                play_sfx(880.0f, 1800);
                snprintf(bannerMsg, sizeof(bannerMsg), "PUEBLO DEL ALBA - ELIGE TU CAMINO");
                bannerT = 150;
            }
        } else {
            int zone = get_current_zone();
            curZoneAudio = zone;
            if (worldMode == 0 && zone != prevZone) {
                prevZone = zone;
                static const char *zNames[4] = {
                    "PUEBLO DEL ALBA - REFUGIO CELESTE",
                    "BOSQUE Y LAGO DE LOS ECOS",
                    "CANON CARMESI - FORJA ANTIGUA",
                    "CIUDADELA ASTRAL - CIMA DEL CIELO"
                };
                snprintf(bannerMsg, sizeof(bannerMsg), "%s", zNames[zone]);
                bannerT = 140;
            }
            if (bannerT > 0) bannerT--;
            if (dialogT > 0) dialogT--;
            if (atkT > 0) atkT--;
            if (pulseT > 0) pulseT--;
            if (invulnT > 0) invulnT--;
            if (heroFlash > 0) heroFlash--;

            if (pad.Buttons & PSP_CTRL_LTRIGGER) camYaw -= 0.035f;
            if (pad.Buttons & PSP_CTRL_RTRIGGER) camYaw += 0.035f;

            float ax = ((float)pad.Lx - 128.0f) / 128.0f;
            float ay = ((float)pad.Ly - 128.0f) / 128.0f;
            if (fabsf(ax) < 0.22f) ax = 0.0f;
            if (fabsf(ay) < 0.22f) ay = 0.0f;
            if (pad.Buttons & PSP_CTRL_LEFT)  ax = -1.0f;
            if (pad.Buttons & PSP_CTRL_RIGHT) ax =  1.0f;
            if (pad.Buttons & PSP_CTRL_UP)    ay = -1.0f;
            if (pad.Buttons & PSP_CTRL_DOWN)  ay =  1.0f;
            float mag = sqrtf(ax * ax + ay * ay);
            if (mag > 1.0f) { ax /= mag; ay /= mag; }

            float spd = 0.15f;
            float fwdX = -sinf(camYaw), fwdZ = -cosf(camYaw);
            float rgtX =  cosf(camYaw), rgtZ = -sinf(camYaw);
            float mvX = (rgtX * ax + fwdX * (-ay));
            float mvZ = (rgtZ * ax + fwdZ * (-ay));

            if (dashT > 0) {
                dashT--;
                mvX = dashDx * 0.32f; mvZ = dashDz * 0.32f;
                if ((animFrame & 1) == 0)
                    spawn_part(heroX, heroY + 0.6f, heroZ, 0, 0.02f, 0, 14.0f, 0.45f, ABGR(130, 235, 255, 210));
            } else {
                mvX *= spd; mvZ *= spd;
                if (ax < -0.1f) heroFlip = 1;
                else if (ax > 0.1f) heroFlip = 0;
            }

            if (fabsf(mvX) > 0.001f || fabsf(mvZ) > 0.001f) {
                float ny = -10.0f;
                if (is_walkable(heroX + mvX, heroZ, &ny)) { heroX += mvX; heroY = ny; }
                ny = -10.0f;
                if (is_walkable(heroX, heroZ + mvZ, &ny)) { heroZ += mvZ; heroY = ny; }
            }

            /* START: Use Potion */
            if ((pressed & PSP_CTRL_START) && heroPotions > 0 && heroHp < heroMaxHp) {
                heroPotions--;
                heroHp += 60; if (heroHp > heroMaxHp) heroHp = heroMaxHp;
                snprintf(dialogMsg, sizeof(dialogMsg), "POCION USADA! +60 HP (Quedan %d)", heroPotions);
                dialogT = 100;
                play_sfx(860.0f, 1200);
            }

            /* Square: Dash */
            if ((pressed & PSP_CTRL_SQUARE) && dashT == 0) {
                float dl = sqrtf(mvX * mvX + mvZ * mvZ);
                if (dl > 0.01f) { dashDx = mvX / dl; dashDz = mvZ / dl; }
                else { dashDx = fwdX; dashDz = fwdZ; }
                dashT = 11; invulnT = 14;
                play_sfx(540.0f, 900);
            }

            /* Cross: Light-Blade Slash */
            if ((pressed & PSP_CTRL_CROSS) && atkT == 0) {
                atkT = 15;
                play_sfx(740.0f, 1000);
                for (int p = 0; p < 10; p++) {
                    float ang = ((float)p / 10.0f) * 6.28318f;
                    spawn_part(heroX + cosf(ang) * 1.5f, heroY + 0.7f, heroZ + sinf(ang) * 1.5f,
                               cosf(ang) * 0.06f, 0.03f, sinf(ang) * 0.06f, 16.0f, 0.38f, ABGR(255, 235, 110, 240));
                }
                for (int i = 0; i < nEnemies; i++) {
                    if (!enemies[i].alive) continue;
                    float dx = enemies[i].x - heroX, dz = enemies[i].z - heroZ;
                    float d = sqrtf(dx * dx + dz * dz);
                    if (d < 3.6f) {
                        enemies[i].hp -= heroAtk;
                        enemies[i].flash = 8;
                        if (d > 0.05f) {
                            float kx = enemies[i].x + (dx / d) * 0.7f, kz = enemies[i].z + (dz / d) * 0.7f, ky = enemies[i].y;
                            if (is_walkable(kx, kz, &ky)) { enemies[i].x = kx; enemies[i].z = kz; enemies[i].y = ky; }
                        }
                        play_sfx(920.0f, 1200);
                        for (int s = 0; s < 8; s++)
                            spawn_part(enemies[i].x, enemies[i].y + 0.8f, enemies[i].z,
                                       frand(-0.08f, 0.08f), frand(0.04f, 0.12f), frand(-0.08f, 0.08f),
                                       22.0f, 0.35f, ABGR(255, 120, 80, 255));
                        if (enemies[i].hp <= 0) {
                            enemies[i].alive = 0;
                            int goldDrop = enemies[i].isBoss ? 50 : (12 + enemies[i].kind * 6);
                            int xpGain = enemies[i].isBoss ? 80 : (20 + enemies[i].kind * 10);
                            heroGold += goldDrop;
                            heroXp += xpGain;
                            if (heroXp >= heroLv * 50) {
                                heroXp -= heroLv * 50;
                                heroLv++;
                                heroMaxHp += 15; heroHp = heroMaxHp; heroAtk += 4;
                                snprintf(dialogMsg, sizeof(dialogMsg), "SUBES A NIVEL %d! +15 HP y +4 ATK", heroLv);
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
                                    snprintf(dialogMsg, sizeof(dialogMsg), "VICTORIA SUPREMA! DRAGON DEL ABISMO DERROTADO!");
                                    dialogT = 260;
                                    play_sfx(1040.0f, 3600);
                                }
                            }
                        }
                    }
                }
            }

            /* Triangle: Resonance Pulse (Lights Obelisks) */
            if ((pressed & PSP_CTRL_TRIANGLE) && pulseT == 0) {
                pulseT = 28;
                play_sfx(660.0f, 1800);
                for (int i = 0; i < nProps; i++) {
                    if (props[i].kind == 2 && !props[i].active) {
                        float dx = props[i].x - heroX, dz = props[i].z - heroZ;
                        if (dx * dx + dz * dz < 20.0f) {
                            props[i].active = 1;
                            if (props[i].id >= 0 && props[i].id < 12) obeliskLit[props[i].id] = 1;
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
                        if (npcs[i].role == 1) {
                            if (heroGold >= 30) {
                                heroGold -= 30; heroAtk += 6;
                                snprintf(dialogMsg, sizeof(dialogMsg), "%s: Espada mejorada! (+6 ATK, Total %d)", npcs[i].name, heroAtk);
                                play_sfx(1050.0f, 1600);
                            } else snprintf(dialogMsg, sizeof(dialogMsg), "%s", npcs[i].msg);
                        } else if (npcs[i].role == 2) {
                            if (heroGold >= 20) {
                                heroGold -= 20; heroPotions++;
                                snprintf(dialogMsg, sizeof(dialogMsg), "%s: Compras 1 Pocion! (Tienes %d)", npcs[i].name, heroPotions);
                                play_sfx(920.0f, 1400);
                            } else snprintf(dialogMsg, sizeof(dialogMsg), "%s", npcs[i].msg);
                        } else {
                            snprintf(dialogMsg, sizeof(dialogMsg), "%s", npcs[i].msg);
                            play_sfx(740.0f, 900);
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
                            heroRelics++; heroGold += 35; heroAtk += 4; heroMaxHp += 10; heroHp = heroMaxHp;
                            snprintf(dialogMsg, sizeof(dialogMsg), "COFRE %d: +35 Oro, +4 ATK y +10 HP!", heroRelics);
                            dialogT = 150; play_sfx(1100.0f, 1800); acted = 1;
                        } else if (props[i].kind == 4) {
                            heroHp = heroMaxHp;
                            snprintf(dialogMsg, sizeof(dialogMsg), "CRISTAL DE SANTUARIO: VIDA RESTAURADA!");
                            dialogT = 120; play_sfx(880.0f, 1400); acted = 1;
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
                        }
                    }
                }
            }

            /* Update Enemies */
            for (int i = 0; i < nEnemies; i++) {
                Enemy3D *e = &enemies[i];
                if (!e->alive) continue;
                if (e->flash > 0) e->flash--;
                if (e->cd > 0) e->cd--;
                float dx = heroX - e->x, dz = heroZ - e->z;
                float d = sqrtf(dx * dx + dz * dz);
                if (d < 11.0f && d > 1.0f) {
                    float es = e->isBoss ? 0.055f : 0.045f;
                    float nx = e->x + (dx / d) * es, nz = e->z + (dz / d) * es, ey = e->y;
                    if (is_walkable(nx, nz, &ey)) { e->x = nx; e->z = nz; e->y = ey; }
                }
                if (d < 1.35f && invulnT == 0 && e->cd == 0) {
                    int dmg = e->isBoss ? (18 + e->bossId * 5) : (9 + e->kind * 4);
                    heroHp -= dmg;
                    heroFlash = 10; invulnT = 36; e->cd = 45;
                    play_sfx(160.0f, 1100);
                    if (heroHp <= 0) {
                        if (worldMode == 1) build_overworld();
                        heroHp = heroMaxHp; heroX = 0.0f; heroY = 0.0f; heroZ = 4.0f;
                        snprintf(dialogMsg, sizeof(dialogMsg), "EL SANTUARIO DEL ALBA TE HA RESCATADO!");
                        dialogT = 160;
                    }
                }
            }

            if ((animFrame % 4) == 0) {
                unsigned int pcol = (zone == 2) ? ABGR(255, 130, 45, 220) : (zone == 3 ? ABGR(140, 220, 255, 230) : ABGR(255, 240, 120, 220));
                spawn_part(heroX + frand(-10.0f, 10.0f), heroY + frand(0.3f, 2.8f), heroZ + frand(-10.0f, 10.0f),
                           frand(-0.015f, 0.015f), frand(0.01f, 0.03f), frand(-0.015f, 0.015f),
                           80.0f, 0.25f, pcol);
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
        static const unsigned int skyCols[4] = {
            ABGR(38, 108, 178, 255),
            ABGR(26, 76, 132, 255),
            ABGR(96, 38, 68, 255),
            ABGR(14, 16, 48, 255)
        };
        static const unsigned int fogCols[4] = {
            0xFFB26C26u, 0xFF844C1Au, 0xFF442660u, 0xFF30100Eu
        };

        sceGuStart(GU_DIRECT, gulist);
        sceGuClearColor(worldMode == 1 ? ABGR(12, 10, 24, 255) : skyCols[curZone]);
        sceGuClearDepth(0);
        sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);

        sceGuEnable(GU_DEPTH_TEST);
        sceGuEnable(GU_CLIP_PLANES);
        sceGuEnable(GU_FOG);
        sceGuFog(22.0f, 68.0f, worldMode == 1 ? 0xFF180A0Cu : fogCols[curZone]);

        sceGumMatrixMode(GU_PROJECTION);
        sceGumLoadIdentity();
        sceGumPerspective(52.0f, 16.0f / 9.0f, 1.0f, 95.0f);

        sceGumMatrixMode(GU_VIEW);
        sceGumLoadIdentity();
        float camDist = inTitle ? 12.5f : 10.2f;
        float camH    = inTitle ? 5.8f  : 6.0f;
        ScePspFVector3 eye = { heroX + sinf(camYaw) * camDist, heroY + camH, heroZ + cosf(camYaw) * camDist };
        ScePspFVector3 at  = { heroX, heroY + 1.1f, heroZ };
        ScePspFVector3 up  = { 0.0f, 1.0f, 0.0f };
        sceGumLookAt(&eye, &at, &up);

        sceGumMatrixMode(GU_MODEL);
        sceGumLoadIdentity();

        /* Sky-Sea below Islands (Overworld only) */
        if (worldMode == 0) {
            float wOff = (float)(animFrame % 256) / 256.0f;
            sceGuEnable(GU_TEXTURE_2D);
            sceGuTexMode(GU_PSM_8888, 0, 0, 0);
            sceGuTexImage(0, 64, 64, 64, texWater);
            sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
            sceGuTexFilter(GU_LINEAR, GU_LINEAR);
            sceGuTexWrap(GU_REPEAT, GU_REPEAT);
            Vtx3D *wv = (Vtx3D *)sceGuGetMemory(6 * sizeof(Vtx3D));
            unsigned int wc = ABGR(190, 225, 255, 190);
            wv[0] = (Vtx3D){ 0 + wOff,  0 + wOff,  wc, heroX - 60, -3.5f, heroZ - 60 };
            wv[1] = (Vtx3D){ 16 + wOff, 0 + wOff,  wc, heroX + 60, -3.5f, heroZ - 60 };
            wv[2] = (Vtx3D){ 16 + wOff, 16 + wOff, wc, heroX + 60, -3.5f, heroZ + 60 };
            wv[3] = (Vtx3D){ 0 + wOff,  0 + wOff,  wc, heroX - 60, -3.5f, heroZ - 60 };
            wv[4] = (Vtx3D){ 16 + wOff, 16 + wOff, wc, heroX + 60, -3.5f, heroZ + 60 };
            wv[5] = (Vtx3D){ 0 + wOff,  16 + wOff, wc, heroX - 60, -3.5f, heroZ + 60 };
            sceGumDrawArray(GU_TRIANGLES, VTYPE_3D, 6, 0, wv);
        }

        /* Platforms (Distance culled for 60 FPS) */
        for (int i = 0; i < nPlats; i++) {
            Platform *p = &plats[i];
            float dx = p->x - heroX, dz = p->z - heroZ;
            if (dx * dx + dz * dz > 4200.0f) continue;
            const unsigned int *ptex = (p->texType == 0) ? texGrass : (p->texType == 1 ? texStone : (p->texType == 2 ? texObsidian : (p->texType == 3 ? texWood : texCrystal)));
            draw_box3d(p->x, p->y, p->z, p->w, p->h, p->d, ptex, ABGR(255, 255, 255, 255), 0.25f);
        }

        /* 3D Props */
        for (int i = 0; i < nProps; i++) {
            Prop3D *pr = &props[i];
            float dx = pr->x - heroX, dz = pr->z - heroZ;
            if (dx * dx + dz * dz > 3600.0f) continue;
            if (pr->kind == 0) {
                draw_box3d(pr->x, pr->y + pr->h, pr->z, pr->rad * 2, pr->h, pr->rad * 2, texStone, ABGR(220, 220, 235, 255), 0.5f);
            } else if (pr->kind == 1) {
                draw_ground_disc(pr->x, pr->y, pr->z, 1.4f, ABGR(10, 20, 15, 160));
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
            }
        }

        /* NPCs */
        for (int i = 0; i < nNpcs; i++) {
            Npc3D *n = &npcs[i];
            float dx = n->x - heroX, dz = n->z - heroZ;
            if (dx * dx + dz * dz > 2500.0f) continue;
            draw_ground_disc(n->x, n->y, n->z, 0.8f, ABGR(10, 15, 25, 170));
            draw_billboard(n->x, n->y, n->z, 1.5f, 1.75f, texNpc, ABGR(255, 255, 255, 255), (n->x > heroX) ? 1 : 0, camYaw);
        }

        /* Enemies & Bosses */
        for (int i = 0; i < nEnemies; i++) {
            Enemy3D *e = &enemies[i];
            if (!e->alive) continue;
            float dx = e->x - heroX, dz = e->z - heroZ;
            if (dx * dx + dz * dz > 2500.0f) continue;
            float sz = e->isBoss ? 3.4f : 1.7f;
            draw_ground_disc(e->x, e->y, e->z, sz * 0.55f, ABGR(180, 25, 45, 165));
            unsigned int ec = e->flash > 0 ? ABGR(255, 90, 90, 255) : ABGR(255, 255, 255, 255);
            const unsigned int *et = e->isBoss ? texBoss[e->bossId % 3] : texMob[e->kind % 3];
            draw_billboard(e->x, e->y, e->z, sz, sz, et, ec, (e->x > heroX) ? 1 : 0, camYaw);
        }

        /* Hero + Lumen Wisp */
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

            gu_window2d(108, 196, 264, 48);
            gu_text2d(136, 208, "PULSA X O START PARA VIAJAR", ABGR(255, 240, 120, 255), 1);
            gu_text2d(124, 224, "3 MAZMORRAS + PUEBLOS + 60 FPS", ABGR(140, 255, 175, 255), 1);
        } else {
            gu_window2d(6, 6, 304, 42);
            char buf[72];
            snprintf(buf, sizeof(buf), "NV.%d  HP %d/%d", heroLv, heroHp, heroMaxHp);
            gu_text2d(14, 12, buf, ABGR(120, 255, 155, 255), 1);
            gu_rect2d(124, 12, 112, 8, ABGR(20, 24, 40, 255));
            int hpw = (heroHp * 110) / heroMaxHp;
            gu_rect2d(125, 13, hpw, 6, ABGR(70, 235, 125, 255));

            snprintf(buf, sizeof(buf), "ORO:%d POC:%d LLAVES:%d/3 ATK:%d", heroGold, heroPotions, keysFound, heroAtk);
            gu_text2d(14, 28, buf, ABGR(255, 225, 110, 255), 1);

            /* Minimap + FPS */
            gu_window2d(406, 6, 68, 68);
            for (int i = 0; i < nPlats; i++) {
                int mx = 440 + (int)(plats[i].x * 0.55f);
                int mz = 44 + (int)(plats[i].z * 0.55f);
                int mw = (int)(plats[i].w * 0.55f); if (mw < 2) mw = 2;
                int md = (int)(plats[i].d * 0.55f); if (md < 2) md = 2;
                if (mx - mw/2 >= 408 && mx + mw/2 <= 472 && mz - md/2 >= 8 && mz + md/2 <= 72)
                    gu_rect2d(mx - mw/2, mz - md/2, mw, md, ABGR(90, 140, 210, 180));
            }
            int hx = 440 + (int)(heroX * 0.55f), hz = 44 + (int)(heroZ * 0.55f);
            if (hx >= 408 && hx <= 470 && hz >= 8 && hz <= 70)
                gu_rect2d(hx - 1, hz - 1, 3, 3, ABGR(255, 240, 80, 255));

            gu_window2d(336, 6, 64, 20);
            snprintf(buf, sizeof(buf), "FPS:%2d", curFps);
            gu_text2d(344, 12, buf, curFps >= 55 ? ABGR(110, 255, 140, 255) : ABGR(255, 225, 90, 255), 1);

            gu_window2d(6, 246, 468, 20);
            gu_text2d(12, 252, "STICK:MOV L/R:CAM X:ESPADA []:DASH /\\:OBELISCO O:HABLAR/ENTRAR START:POCION", ABGR(200, 225, 255, 255), 1);

            if (bannerT > 0) {
                gu_window2d(78, 56, 324, 26);
                gu_text2d(96, 65, bannerMsg, ABGR(255, 230, 110, 255), 1);
            }
            if (dialogT > 0) {
                gu_window2d(24, 204, 432, 34);
                gu_text2d(34, 217, dialogMsg, ABGR(255, 255, 255, 255), 1);
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
        sceGuSwapBuffers();
    }

    arun = 0;
    if (ath >= 0) sceKernelWaitThreadEnd(ath, NULL);
    if (ach >= 0) sceAudioChRelease(ach);
    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
