#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspge.h>
#include <pspaudio.h>
#include <psppower.h>
#include <psputility.h>
#include <pspnet.h>
#include <pspnet_adhoc.h>
#include <pspnet_adhocctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "font5x7.h"
#include "bg_arena.h"
#include "bg_title.h"
#include "world_sprites.h"

PSP_MODULE_INFO("ChordbreakerTower", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define RGB(r, g, b) (0xFF000000u | ((unsigned int)(b) << 16) | ((unsigned int)(g) << 8) | (unsigned int)(r))

static unsigned int __attribute__((aligned(16))) gulist[262144];
static unsigned int *vram_bufs[2];
static unsigned int sys_fb[SCR_H * BUF_W] __attribute__((aligned(64)));
static int cur_fb = 0;
static volatile int running = 1;

static int exit_cb(int a1, int a2, void *c) { running = 0; return 0; }
static int cb_th(SceSize a, void *p) {
    int id = sceKernelCreateCallback("Exit", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}

static unsigned int rng_st = 0x13572468u;
static unsigned int xr(void) {
    rng_st ^= rng_st << 13;
    rng_st ^= rng_st >> 17;
    rng_st ^= rng_st << 5;
    return rng_st;
}

/* ================= FAST 32-BIT DRAWING PRIMITIVES ================= */
static void fill_rect(unsigned int *fb, int x, int y, int w, int h, unsigned int col) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x;
    if (y + h > SCR_H) h = SCR_H - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++) {
        unsigned int *row = &fb[(y + j) * BUF_W + x];
        for (int i = 0; i < w; i++) row[i] = col;
    }
}

static void fill_rect_alpha(unsigned int *fb, int x, int y, int w, int h, unsigned int col, int a) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x;
    if (y + h > SCR_H) h = SCR_H - y;
    if (w <= 0 || h <= 0) return;
    int ia = 255 - a;
    unsigned int c_rb = (col & 0x00FF00FFu) * (unsigned int)a;
    unsigned int c_g  = (col & 0x0000FF00u) * (unsigned int)a;
    for (int j = 0; j < h; j++) {
        unsigned int *row = &fb[(y + j) * BUF_W + x];
        for (int i = 0; i < w; i++) {
            unsigned int dst = row[i];
            unsigned int rb = ((c_rb + (dst & 0x00FF00FFu) * (unsigned int)ia) >> 8) & 0x00FF00FFu;
            unsigned int g  = ((c_g  + (dst & 0x0000FF00u) * (unsigned int)ia) >> 8) & 0x0000FF00u;
            row[i] = 0xFF000000u | rb | g;
        }
    }
}

static void draw_border(unsigned int *fb, int x, int y, int w, int h, unsigned int col) {
    fill_rect(fb, x, y, w, 1, col);
    fill_rect(fb, x, y + h - 1, w, 1, col);
    fill_rect(fb, x, y, 1, h, col);
    fill_rect(fb, x + w - 1, y, 1, h, col);
}

static void draw_ff_window(unsigned int *fb, int x, int y, int w, int h) {
    fill_rect_alpha(fb, x, y, w, h, RGB(14, 24, 72), 215);
    draw_border(fb, x, y, w, h, RGB(238, 242, 255));
    draw_border(fb, x + 1, y + 1, w - 2, h - 2, RGB(115, 145, 225));
}

static void draw_char(unsigned int *fb, int x, int y, char c, unsigned int col) {
    unsigned char uc = (unsigned char)c;
    if (uc < 32 || uc > 127) uc = '?';
    const unsigned char *g = font5x7[uc - 32];
    for (int col_i = 0; col_i < 5; col_i++) {
        unsigned char bits = g[col_i];
        int px = x + col_i;
        if ((unsigned)px >= SCR_W) continue;
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                int py = y + row;
                if ((unsigned)py < SCR_H) fb[py * BUF_W + px] = col;
            }
        }
    }
}

static void draw_text(unsigned int *fb, int x, int y, const char *s, unsigned int col) {
    while (*s) {
        draw_char(fb, x + 1, y + 1, *s, RGB(0, 0, 0));
        draw_char(fb, x, y, *s, col);
        x += 6;
        s++;
    }
}

static void draw_text_2x(unsigned int *fb, int x, int y, const char *s, unsigned int col) {
    while (*s) {
        unsigned char uc = (unsigned char)(*s);
        if (uc < 32 || uc > 127) uc = '?';
        const unsigned char *g = font5x7[uc - 32];
        for (int ci = 0; ci < 5; ci++) {
            unsigned char bits = g[ci];
            for (int r = 0; r < 7; r++) {
                if (bits & (1 << r)) {
                    fill_rect(fb, x + ci * 2 + 1, y + r * 2 + 1, 2, 2, RGB(0, 0, 0));
                    fill_rect(fb, x + ci * 2, y + r * 2, 2, 2, col);
                }
            }
        }
        x += 12;
        s++;
    }
}

static void draw_bg_full(unsigned int *fb, const unsigned int *bg) {
    for (int y = 0; y < SCR_H; y++) {
        memcpy(&fb[y * BUF_W], &bg[y * SCR_W], SCR_W * 4);
    }
}

static void draw_spr_scaled(unsigned int *fb, const unsigned int *spr, int sw, int sh, int dx, int dy, int dw, int dh, int flipX, int flashWhite) {
    for (int j = 0; j < dh; j++) {
        int py = dy + j;
        if ((unsigned)py >= SCR_H) continue;
        int sj = (j * sh) / dh;
        unsigned int *dst = &fb[py * BUF_W];
        const unsigned int *srow = &spr[sj * sw];
        for (int i = 0; i < dw; i++) {
            int px = dx + i;
            if ((unsigned)px >= SCR_W) continue;
            int si = (i * sw) / dw;
            if (flipX) si = sw - 1 - si;
            unsigned int c = srow[si];
            if ((c >> 24) > 32) {
                dst[px] = flashWhite ? RGB(255, 255, 255) : c;
            }
        }
    }
}

/* ================= FAST LUT AUDIO SYNTHESIZER ================= */
static int ach = -1, ath = -1;
static volatile int arun = 0, sfx_t = 0, sfx_max = 3000, sfx_on = 1, curSong = 0;
static volatile int bgm_step = 0;
static volatile float bgm_sub = 0.0f;
static float sfx_f = 440.0f, sfx_p = 0.0f, sfx_p2 = 0.0f;
static short abuf[2][1024 * 2];
static float echoL[4096], echoR[4096];
static int echoIdx = 0;

static void play_sfx(float f, int dur) {
    if (sfx_on) { sfx_f = f; sfx_t = dur; sfx_max = dur > 0 ? dur : 1; sfx_p = 0.0f; sfx_p2 = 0.0f; }
}

static const float songScale[4][4] = {
    { 293.66f, 349.23f, 440.00f, 587.33f }, /* 0: D-min Tower */
    { 329.63f, 392.00f, 493.88f, 659.25f }, /* 1: E-min Shred */
    { 164.81f, 220.00f, 246.94f, 329.63f }, /* 2: Heavy Boss */
    { 220.00f, 261.63f, 329.63f, 440.00f }  /* 3: Abyss Finale */
};

static const float songBass[4][32] = {
    { 146.83f,146.83f,174.61f,146.83f, 130.81f,130.81f,164.81f,130.81f, 116.54f,116.54f,146.83f,116.54f, 130.81f,146.83f,164.81f,110.00f,
      146.83f,220.00f,174.61f,146.83f, 130.81f,196.00f,164.81f,130.81f, 116.54f,174.61f,146.83f,116.54f, 110.00f,130.81f,146.83f,110.00f },
    { 164.81f,164.81f,196.00f,164.81f, 146.83f,146.83f,174.61f,146.83f, 130.81f,130.81f,164.81f,130.81f, 146.83f,164.83f,196.00f,123.47f,
      164.81f,246.94f,196.00f,164.81f, 146.83f,220.00f,174.61f,146.83f, 130.81f,196.00f,164.81f,130.81f, 123.47f,146.83f,164.81f,123.47f },
    {  82.41f, 82.41f, 82.41f, 98.00f,  82.41f, 82.41f,110.00f, 98.00f,  73.42f, 73.42f, 98.00f, 73.42f,  82.41f,110.00f,123.47f, 98.00f,
       82.41f, 82.41f,123.47f, 82.41f,  98.00f, 98.00f,146.83f, 98.00f,  87.31f, 87.31f,130.81f, 87.31f,  82.41f, 98.00f,110.00f,123.47f },
    { 110.00f,110.00f,164.81f,110.00f,  98.00f, 98.00f,146.83f, 98.00f,  87.31f, 87.31f,130.81f, 87.31f,  82.41f,110.00f,130.81f,164.81f,
      110.00f,164.81f,220.00f,164.81f,  98.00f,146.83f,196.00f,146.83f,  87.31f,130.81f,174.61f,130.81f,  82.41f, 98.00f,123.47f,164.81f }
};

static const float songLead[4][32] = {
    { 587.33f,  0.00f,659.25f,698.46f, 783.99f,  0.00f,698.46f,659.25f, 587.33f,523.25f,587.33f,  0.00f, 440.00f,523.25f,587.33f,  0.00f,
      698.46f,  0.00f,783.99f,880.00f, 783.99f,698.46f,659.25f,587.33f, 523.25f,587.33f,659.25f,523.25f, 440.00f,  0.00f,587.33f,  0.00f },
    { 659.25f,  0.00f,783.99f,659.25f, 587.33f,659.25f,783.99f,  0.00f, 880.00f,783.99f,659.25f,587.33f, 659.25f,  0.00f,493.88f,  0.00f,
      659.25f,783.99f,987.77f,  0.00f, 880.00f,783.99f,659.25f,587.33f, 523.25f,587.33f,659.25f,783.99f, 659.25f,  0.00f,659.25f,  0.00f },
    { 329.63f,  0.00f,392.00f,329.63f, 440.00f,  0.00f,392.00f,329.63f, 293.66f,329.63f,392.00f,  0.00f, 329.63f,246.94f,293.66f,  0.00f,
      329.63f,493.88f,440.00f,392.00f, 440.00f,  0.00f,523.25f,493.88f, 440.00f,392.00f,329.63f,293.66f, 329.63f,  0.00f,246.94f,  0.00f },
    { 440.00f,523.25f,659.25f,523.25f, 587.33f,659.25f,783.99f,659.25f, 698.46f,659.25f,587.33f,523.25f, 493.88f,587.33f,659.25f,  0.00f,
      880.00f,783.99f,659.25f,587.33f, 523.25f,587.33f,659.25f,783.99f, 698.46f,659.25f,587.33f,523.25f, 440.00f,  0.00f,440.00f,  0.00f }
};

static int audio_th(SceSize a, void *p) {
    static float audSin[256], audExp[256];
    static int audLutInit = 0;
    if (!audLutInit) {
        for (int i = 0; i < 256; i++) {
            audSin[i] = sinf(((float)i / 256.0f) * 6.2831853f);
            audExp[i] = expf(-((float)i / 255.0f) * 10.0f);
        }
        audLutInit = 1;
    }
    #define FAST_SIN(ph) (audSin[(int)((ph) * 40.743665f) & 255])
    #define FAST_EXP(t)  (audExp[((t) < 0.0f ? 0 : ((t) >= 1.0f ? 255 : (int)((t) * 255.0f)))])

    int bi = 0;
    float pB = 0.0f, pL = 0.0f, pK = 0.0f, pS = 0.0f;
    unsigned int nRng = 0x91827364u;
    const float invSr = 1.0f / 44100.0f;
    const float stepSamples = 7056.0f; /* ~125 BPM 8th notes */
    while (arun) {
        short *b = abuf[bi];
        int sIdx = curSong & 3;
        for (int i = 0; i < 1024; i++) {
            bgm_sub += 1.0f;
            if (bgm_sub >= stepSamples) {
                bgm_sub -= stepSamples;
                bgm_step = (bgm_step + 1) & 31;
                if ((bgm_step & 3) == 0) pK = 0.0f;
                if ((bgm_step & 7) == 4) pS = 0.0f;
            }
            int st = bgm_step;
            float env = 1.0f - (bgm_sub / stepSamples);
            float fB = songBass[sIdx][st];
            pB += 6.2831853f * fB * invSr;
            if (pB > 6.2831853f) pB -= 6.2831853f;
            float sB = (FAST_SIN(pB) * 0.5f + ((pB < 3.14159f) ? 0.25f : -0.25f)) * 0.20f * env;

            float fL = songLead[sIdx][st];
            float sL = 0.0f;
            if (fL > 10.0f) {
                pL += 6.2831853f * fL * invSr;
                if (pL > 6.2831853f) pL -= 6.2831853f;
                sL = FAST_SIN(pL) * 0.14f * env;
            }

            float sD = 0.0f;
            if (pK < 0.08f) {
                float kf = 115.0f - pK * 900.0f; if (kf < 40.0f) kf = 40.0f;
                sD += FAST_SIN(pK * 6.28318f * kf) * 0.28f * FAST_EXP(pK * 3.5f);
                pK += invSr;
            }
            if (pS < 0.07f) {
                nRng ^= nRng << 13; nRng ^= nRng >> 17; nRng ^= nRng << 5;
                float nz = ((float)(int)(nRng & 255) - 128.0f) / 128.0f;
                sD += nz * 0.16f * FAST_EXP(pS * 4.5f);
                pS += invSr;
            }

            float sfx = 0.0f;
            if (sfx_t > 0) {
                float se = (float)sfx_t / (float)sfx_max;
                sfx_p += 6.2831853f * sfx_f * invSr;
                if (sfx_p > 6.2831853f) sfx_p -= 6.2831853f;
                sfx = FAST_SIN(sfx_p) * 0.28f * se;
                sfx_t--;
            }

            float L = sB + sL * 0.7f + sD + sfx;
            float R = sB + sL * 1.1f + sD + sfx;
            int rIdx = (echoIdx - 2200) & 4095;
            L += echoR[rIdx] * 0.16f;
            R += echoL[rIdx] * 0.16f;
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

/* ================= PSP AD-HOC NETWORKING ================= */
#define ADHOC_PORT 0x309
static int adhocLoaded = 0, adhocReady = 0, pdpId = -1;
static unsigned char myMac[6], peerMac[6];
static int isMulti = 0, isHost = 0, peerFound = 0;

typedef enum {
    PKT_JOIN = 1,
    PKT_WELCOME = 2,
    PKT_STATE = 3,
    PKT_TURN_END = 4,
    PKT_CARD_DONE = 5,
    PKT_RETRY_FLOOR = 6
} PktType;

typedef struct __attribute__((packed)) {
    unsigned char type;
    unsigned char senderHero;
    unsigned char activeTurn; /* 0 = P1(Host), 1 = P2(Client) */
    unsigned char floorResult; /* 0=fighting, 1=victory, 2=defeat */
    unsigned int  seed;
    short floor;
    short eHp, eMaxHp;
    short p1Hp, p1MaxHp, p1Mp;
    short p2Hp, p2MaxHp, p2Mp;
    short activeCombo;
    short lastDmg;
    unsigned char cardReady;
    unsigned char debuffs;
} NetPacket;

static char netStatus[64] = "Ad-Hoc listo";

static int init_adhoc_stack(void) {
    if (adhocReady) return 1;
    if (!adhocLoaded) {
        int r1 = sceUtilityLoadNetModule(PSP_NET_MODULE_COMMON);
        int r2 = sceUtilityLoadNetModule(PSP_NET_MODULE_ADHOC);
        if (r1 < 0 && (unsigned)r1 != 0x80110802u) {
            snprintf(netStatus, sizeof(netStatus), "Err NetCommon %08X", r1);
            return 0;
        }
        if (r2 < 0 && (unsigned)r2 != 0x80110802u) {
            snprintf(netStatus, sizeof(netStatus), "Err NetAdhoc %08X", r2);
            return 0;
        }
        adhocLoaded = 1;
    }
    if (sceNetInit(128 * 1024, 42, 4 * 1024, 42, 4 * 1024) < 0) {
        snprintf(netStatus, sizeof(netStatus), "Error sceNetInit");
        return 0;
    }
    if (sceNetAdhocInit() < 0) {
        snprintf(netStatus, sizeof(netStatus), "Error sceNetAdhocInit");
        sceNetTerm();
        return 0;
    }
    struct productStruct prod;
    memset(&prod, 0, sizeof(prod));
    strcpy(prod.product, "ULUS99999");
    if (sceNetAdhocctlInit(32 * 1024, 0x20, &prod) < 0) {
        snprintf(netStatus, sizeof(netStatus), "Error AdhocctlInit (WiFi ON?)");
        sceNetAdhocTerm();
        sceNetTerm();
        return 0;
    }
    sceNetGetLocalEtherAddr(myMac);
    adhocReady = 1;
    return 1;
}

static void shutdown_adhoc_stack(void) {
    if (pdpId >= 0) { sceNetAdhocPdpDelete(pdpId, 0); pdpId = -1; }
    if (adhocReady) {
        sceNetAdhocctlDisconnect();
        sceNetAdhocctlTerm();
        sceNetAdhocTerm();
        sceNetTerm();
        adhocReady = 0;
    }
    peerFound = 0;
}

static void send_pkt(const NetPacket *pkt) {
    if (pdpId < 0) return;
    unsigned char dst[6];
    if (peerFound) memcpy(dst, peerMac, 6);
    else memset(dst, 0xFF, 6);
    sceNetAdhocPdpSend(pdpId, dst, ADHOC_PORT, (void *)pkt, sizeof(NetPacket), 0, 1);
}

static int recv_pkt(NetPacket *pkt, unsigned char *srcMacOut) {
    if (pdpId < 0) return 0;
    unsigned short port = 0;
    int len = sizeof(NetPacket);
    unsigned char src[6];
    int res = sceNetAdhocPdpRecv(pdpId, src, &port, pkt, &len, 0, 1);
    if (res == 0 && len == (int)sizeof(NetPacket)) {
        if (memcmp(src, myMac, 6) == 0) return 0; /* ignore self-echo */
        if (srcMacOut) memcpy(srcMacOut, src, 6);
        return 1;
    }
    return 0;
}

/* ================= TOWER RPG DATA & STATE ================= */
typedef struct {
    const char *name;
    const unsigned int *spr;
    int maxHp, hp, maxMp, mp, atk, def;
    int lifesteal, critPct, spdPct;
    int debuffCount;
    int cardDone;
} HeroState;

static const char *heroNames[3] = { "Bardo Laud", "Paladin Bajo", "Maga Arpa" };
static const unsigned int *heroSprs[3] = { spr_bard_48, spr_band1_48, spr_band2_48 };
static const int heroBaseHp[3]  = { 125, 155, 105 };
static const int heroBaseAtk[3] = { 18, 15, 22 };
static const int heroBaseDef[3] = { 10, 14, 8 };

static HeroState players[2];
static int myHeroIdx = 0;
static int p2HeroIdx = 1;
static int mySlot = 0; /* 0 = Host(P1), 1 = Client(P2) */

static int towerFloor = 1, bestFloor = 1;
static int activeTurn = 0; /* 0 = P1, 1 = P2 */
static int turnTimer = 0;
#define TURN_FRAMES (14 * 60) /* 14 seconds per player turn in 2P relay */

static int eHp = 120, eMaxHp = 120, eAtk = 12, eIsBoss = 0, eSpriteId = 0;
static char eName[40] = "Slime de Torre";

/* Rhythm Highway */
#define MAX_NOTES 28
#define HIT_Y 224
typedef struct { float y, spd; int lane, active; float freq; } Note;
static Note notes[MAX_NOTES];
static int spawnT = 0, combo = 0, maxCombo = 0;
static int heroFlash = 0, enemyFlash = 0, lastDmg = 0;
static char judge[40] = "";
static int judgeT = 0;

/* Cards & Debuffs */
typedef struct {
    const char *title;
    const char *desc;
    int id;
} CardDef;

static const CardDef cardPool[8] = {
    { "Furia Sinfonica", "+20% ATK", 0 },
    { "Corazon Hierro",  "+30 MaxHP y Cura", 1 },
    { "Egida de Acero",  "+4 DEF", 2 },
    { "Acorde Vampirico","+3 HP por golpe", 3 },
    { "Maestro Combo",   "+25% Critico", 4 },
    { "Fuente de Mana",  "+25 MaxMP y MP", 5 },
    { "Pocion Mayor",    "Cura 100% HP", 6 },
    { "Purificacion",    "Quita 1 Debufo", 7 }
};

static const char *debuffNames[4] = {
    "Maldicion Disonante (-15% ATK)",
    "Armadura Agrietada (-3 DEF)",
    "Fatiga Abismal (-15% MaxHP)",
    "Cuerdas Pesadas (+15% Vel)"
};

static int offeredCards[3];
static int cardCursor = 0;
static int lastDebuffId = 0;

typedef enum {
    ST_TITLE = 0,
    ST_ADHOC_MENU,
    ST_ADHOC_WAIT,
    ST_FLOOR_INTRO,
    ST_BATTLE,
    ST_CARDS,
    ST_DEBUFF
} GameState;

static GameState state = ST_TITLE;
static int menuCursor = 0, adhocCursor = 0, introTimer = 0;

static void init_hero(HeroState *h, int idx) {
    h->name = heroNames[idx];
    h->spr = heroSprs[idx];
    h->maxHp = heroBaseHp[idx];
    h->hp = h->maxHp;
    h->maxMp = 100;
    h->mp = 40;
    h->atk = heroBaseAtk[idx];
    h->def = heroBaseDef[idx];
    h->lifesteal = 0;
    h->critPct = 15;
    h->spdPct = 100;
    h->debuffCount = 0;
    h->cardDone = 0;
}

static void setup_floor(int fl) {
    towerFloor = fl;
    if (towerFloor > bestFloor) bestFloor = towerFloor;
    eIsBoss = (fl % 5 == 0);
    if (eIsBoss) {
        int bIdx = ((fl / 5) - 1) % 3;
        eSpriteId = bIdx;
        static const char *bNames[3] = { "Golem de Hierro", "Automata Abismal", "Dragon Sinfonico" };
        snprintf(eName, sizeof(eName), "%s Nv.%d", bNames[bIdx], fl);
        eMaxHp = 240 + fl * 75 + fl * fl * 2;
        eAtk = 14 + fl * 2;
        curSong = 2 + (bIdx & 1);
    } else {
        int mIdx = (fl - 1) % 3;
        eSpriteId = mIdx;
        static const char *mNames[3] = { "Slime de Torre", "Goblin Metalero", "Espectro del Eco" };
        snprintf(eName, sizeof(eName), "%s Nv.%d", mNames[mIdx], fl);
        eMaxHp = 110 + fl * 42 + fl * fl;
        eAtk = 9 + (fl * 3) / 2;
        curSong = fl & 1;
    }
    eHp = eMaxHp;
    activeTurn = 0;
    turnTimer = TURN_FRAMES;
    combo = 0;
    spawnT = 18;
    memset(notes, 0, sizeof(notes));
    judge[0] = 0;
    judgeT = 0;
    players[0].cardDone = 0;
    players[1].cardDone = 0;
}

static void roll_cards(const HeroState *h) {
    int used[8] = {0};
    if (h->debuffCount > 0) {
        offeredCards[0] = 7; /* Guarantee Purificacion option when debuffed */
        used[7] = 1;
    } else {
        int c = (int)(xr() % 7);
        offeredCards[0] = c;
        used[c] = 1;
    }
    for (int i = 1; i < 3; i++) {
        int c;
        do { c = (int)(xr() % 7); } while (used[c]);
        used[c] = 1;
        offeredCards[i] = c;
    }
    cardCursor = 0;
}

static void apply_card(HeroState *h, int cid) {
    switch (cid) {
        case 0: h->atk = (h->atk * 120) / 100 + 2; break;
        case 1: h->maxHp += 30; h->hp += 30; if (h->hp > h->maxHp) h->hp = h->maxHp; break;
        case 2: h->def += 4; break;
        case 3: h->lifesteal += 3; break;
        case 4: h->critPct += 25; break;
        case 5: h->maxMp += 25; h->mp += 35; if (h->mp > h->maxMp) h->mp = h->maxMp; break;
        case 6: h->hp = h->maxHp; break;
        case 7: if (h->debuffCount > 0) { h->debuffCount--; h->atk += 2; h->def += 2; } break;
    }
    h->cardDone = 1;
}

static void apply_debuff(HeroState *h, int did) {
    h->debuffCount++;
    switch (did) {
        case 0: h->atk = (h->atk * 85) / 100; if (h->atk < 6) h->atk = 6; break;
        case 1: h->def -= 3; if (h->def < 0) h->def = 0; break;
        case 2: h->maxHp = (h->maxHp * 85) / 100; if (h->maxHp < 50) h->maxHp = 50; break;
        case 3: h->spdPct += 15; if (h->spdPct > 160) h->spdPct = 160; break;
    }
    h->hp = (h->maxHp * 75) / 100;
    if (h->hp < 20) h->hp = 20;
}

static void build_net_packet(NetPacket *p, unsigned char type, unsigned char res) {
    memset(p, 0, sizeof(*p));
    p->type = type;
    p->senderHero = (unsigned char)myHeroIdx;
    p->activeTurn = (unsigned char)activeTurn;
    p->floorResult = res;
    p->seed = rng_st;
    p->floor = (short)towerFloor;
    p->eHp = (short)eHp;
    p->eMaxHp = (short)eMaxHp;
    p->p1Hp = (short)players[0].hp; p->p1MaxHp = (short)players[0].maxHp; p->p1Mp = (short)players[0].mp;
    p->p2Hp = (short)players[1].hp; p->p2MaxHp = (short)players[1].maxHp; p->p2Mp = (short)players[1].mp;
    p->activeCombo = (short)combo;
    p->lastDmg = (short)lastDmg;
    p->cardReady = (unsigned char)players[mySlot].cardDone;
    p->debuffs = (unsigned char)players[mySlot].debuffCount;
}

static void draw_hp_bar(unsigned int *fb, int x, int y, int w, int h, int val, int maxVal, unsigned int col) {
    fill_rect(fb, x, y, w, h, RGB(18, 20, 35));
    if (maxVal > 0 && val > 0) {
        int fw = (val * (w - 2)) / maxVal;
        if (fw > w - 2) fw = w - 2;
        fill_rect(fb, x + 1, y + 1, fw, h - 2, col);
    }
    draw_border(fb, x, y, w, h, RGB(195, 210, 240));
}

int main(void) {
    scePowerSetClockFrequency(333, 333, 166);
    int th = sceKernelCreateThread("cb", cb_th, 0x11, 0xFA0, 0, NULL);
    if (th >= 0) sceKernelStartThread(th, 0, NULL);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    unsigned int vram_base = ((unsigned int)sceGeEdramGetAddr()) | 0x40000000u;
    vram_bufs[0] = (unsigned int *)vram_base;
    vram_bufs[1] = (unsigned int *)(vram_base + BUF_W * SCR_H * 4);
    memset((void *)vram_base, 0, BUF_W * SCR_H * 4 * 2);

    sceGuInit();
    sceGuStart(GU_DIRECT, gulist);
    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)(BUF_W * SCR_H * 4), BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuShadeModel(GU_FLAT);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    ach = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, 1024, PSP_AUDIO_FORMAT_STEREO);
    if (ach >= 0) {
        arun = 1;
        ath = sceKernelCreateThread("ct_aud", audio_th, 0x12, 0x10000, 0, NULL);
        if (ath >= 0) sceKernelStartThread(ath, 0, NULL);
    }

    SceCtrlData pad, oldPad;
    memset(&oldPad, 0, sizeof(oldPad));
    int animFrame = 0, netTick = 0;

    static const unsigned int laneCols[4] = {
        RGB(235, 95, 225), /* Square */
        RGB(80, 235, 130), /* Triangle */
        RGB(255, 95, 95),  /* Circle */
        RGB(95, 165, 255)  /* Cross */
    };
    static const char *laneLbl[4] = { "[]", "/\\", "O", "X" };

    while (running) {
        sceCtrlPeekBufferPositive(&pad, 1);
        unsigned int pressed = pad.Buttons & ~oldPad.Buttons;
        oldPad = pad;
        animFrame++;

        unsigned int *fb = sys_fb;

        /* ==================== UPDATE ==================== */
        if (state == ST_TITLE) {
            if (pressed & PSP_CTRL_UP)   { menuCursor = (menuCursor + 1) & 1; play_sfx(520.0f, 500); }
            if (pressed & PSP_CTRL_DOWN) { menuCursor = (menuCursor + 1) & 1; play_sfx(520.0f, 500); }
            if (pressed & PSP_CTRL_LEFT) { myHeroIdx = (myHeroIdx + 2) % 3; play_sfx(640.0f, 500); }
            if (pressed & PSP_CTRL_RIGHT){ myHeroIdx = (myHeroIdx + 1) % 3; play_sfx(640.0f, 500); }
            if (pressed & PSP_CTRL_CROSS) {
                play_sfx(880.0f, 1200);
                if (menuCursor == 0) {
                    isMulti = 0; mySlot = 0;
                    init_hero(&players[0], myHeroIdx);
                    rng_st = (unsigned int)sceKernelGetSystemTimeLow() | 1u;
                    setup_floor(1);
                    introTimer = 90;
                    state = ST_FLOOR_INTRO;
                } else {
                    adhocCursor = 0;
                    snprintf(netStatus, sizeof(netStatus), "Elige Crear o Unirse");
                    state = ST_ADHOC_MENU;
                }
            }
        } else if (state == ST_ADHOC_MENU) {
            if (pressed & (PSP_CTRL_UP | PSP_CTRL_DOWN)) { adhocCursor ^= 1; play_sfx(520.0f, 500); }
            if (pressed & PSP_CTRL_CIRCLE) { state = ST_TITLE; play_sfx(420.0f, 500); }
            if (pressed & PSP_CTRL_CROSS) {
                play_sfx(760.0f, 800);
                if (init_adhoc_stack()) {
                    isHost = (adhocCursor == 0);
                    mySlot = isHost ? 0 : 1;
                    peerFound = 0;
                    int cr = isHost ? sceNetAdhocctlCreate("CHORDTWR") : sceNetAdhocctlConnect("CHORDTWR");
                    if (cr < 0) {
                        snprintf(netStatus, sizeof(netStatus), "Error Adhocctl %08X", cr);
                        shutdown_adhoc_stack();
                    } else {
                        snprintf(netStatus, sizeof(netStatus), isHost ? "Creando sala CHORDTWR..." : "Buscando sala CHORDTWR...");
                        state = ST_ADHOC_WAIT;
                    }
                }
            }
        } else if (state == ST_ADHOC_WAIT) {
            if (pressed & PSP_CTRL_CIRCLE) {
                shutdown_adhoc_stack();
                state = ST_ADHOC_MENU;
                play_sfx(420.0f, 500);
            } else {
                int ctlState = 0;
                sceNetAdhocctlGetState(&ctlState);
                if (ctlState == 1) {
                    if (pdpId < 0) {
                        pdpId = sceNetAdhocPdpCreate(myMac, ADHOC_PORT, 0x2000, 0);
                        snprintf(netStatus, sizeof(netStatus), isHost ? "Sala lista! Esperando J2..." : "Conectado! Buscando Host...");
                    }
                    NetPacket pkt;
                    unsigned char src[6];
                    if (isHost) {
                        if (recv_pkt(&pkt, src) && pkt.type == PKT_JOIN) {
                            memcpy(peerMac, src, 6);
                            peerFound = 1;
                            p2HeroIdx = pkt.senderHero % 3;
                            rng_st = (unsigned int)sceKernelGetSystemTimeLow() | 1u;
                            isMulti = 1;
                            init_hero(&players[0], myHeroIdx);
                            init_hero(&players[1], p2HeroIdx);
                            setup_floor(1);
                            NetPacket wp;
                            build_net_packet(&wp, PKT_WELCOME, 0);
                            send_pkt(&wp);
                            introTimer = 90;
                            state = ST_FLOOR_INTRO;
                            play_sfx(980.0f, 1500);
                        }
                    } else {
                        if ((animFrame % 15) == 0) {
                            NetPacket jp;
                            build_net_packet(&jp, PKT_JOIN, 0);
                            send_pkt(&jp);
                        }
                        if (recv_pkt(&pkt, src) && pkt.type == PKT_WELCOME) {
                            memcpy(peerMac, src, 6);
                            peerFound = 1;
                            p2HeroIdx = myHeroIdx;
                            myHeroIdx = myHeroIdx; /* keep local choice */
                            rng_st = pkt.seed;
                            isMulti = 1;
                            init_hero(&players[0], pkt.senderHero % 3);
                            init_hero(&players[1], myHeroIdx);
                            setup_floor(1);
                            introTimer = 90;
                            state = ST_FLOOR_INTRO;
                            play_sfx(980.0f, 1500);
                        }
                    }
                }
            }
        } else if (state == ST_FLOOR_INTRO) {
            introTimer--;
            if (introTimer <= 0 || (pressed & PSP_CTRL_CROSS)) {
                state = ST_BATTLE;
                turnTimer = TURN_FRAMES;
            }
        } else if (state == ST_BATTLE) {
            int myTurn = (!isMulti) || (activeTurn == mySlot);
            HeroState *actH = &players[activeTurn];

            if (judgeT > 0) judgeT--;
            if (heroFlash > 0) heroFlash--;
            if (enemyFlash > 0) enemyFlash--;

            if (myTurn) {
                /* Active player runs Guitar Hero rhythm combat */
                int baseEvery = 30 - (towerFloor / 2);
                if (baseEvery < 15) baseEvery = 15;
                if (eIsBoss) baseEvery = (baseEvery * 4) / 5;
                spawnT--;
                if (spawnT <= 0) {
                    spawnT = baseEvery + (int)(xr() % 7);
                    for (int i = 0; i < MAX_NOTES; i++) {
                        if (!notes[i].active) {
                            int ln = (int)(xr() & 3);
                            notes[i].active = 1;
                            notes[i].lane = ln;
                            notes[i].y = 42.0f;
                            notes[i].spd = (2.5f + ((float)(towerFloor > 30 ? 30 : towerFloor) * 0.055f)) * ((float)actH->spdPct / 100.0f);
                            notes[i].freq = songScale[curSong & 3][ln];
                            break;
                        }
                    }
                }

                /* L = Heal / Shield (30 MP), R = Symphony Finisher (50 MP) */
                if ((pressed & PSP_CTRL_LTRIGGER) && actH->mp >= 30 && actH->hp < actH->maxHp) {
                    actH->mp -= 30;
                    int heal = 28 + actH->maxHp / 5;
                    actH->hp += heal;
                    if (actH->hp > actH->maxHp) actH->hp = actH->maxHp;
                    snprintf(judge, sizeof(judge), "CURA +%d HP!", heal);
                    judgeT = 36;
                    play_sfx(780.0f, 1200);
                }
                if ((pressed & PSP_CTRL_RTRIGGER) && actH->mp >= 50) {
                    actH->mp -= 50;
                    int sdmg = actH->atk * 3 + combo * 2;
                    eHp -= sdmg;
                    if (eHp < 0) eHp = 0;
                    lastDmg = sdmg;
                    enemyFlash = 10;
                    snprintf(judge, sizeof(judge), "SINFONIA -%d!", sdmg);
                    judgeT = 40;
                    play_sfx(980.0f, 1800);
                }

                static const unsigned int laneBtns[4] = { PSP_CTRL_SQUARE, PSP_CTRL_TRIANGLE, PSP_CTRL_CIRCLE, PSP_CTRL_CROSS };
                for (int l = 0; l < 4; l++) {
                    if (pressed & laneBtns[l]) {
                        int best = -1;
                        float bestD = 999.0f;
                        for (int i = 0; i < MAX_NOTES; i++) {
                            if (notes[i].active && notes[i].lane == l) {
                                float d = fabsf(notes[i].y - (float)HIT_Y);
                                if (d < bestD) { bestD = d; best = i; }
                            }
                        }
                        if (best >= 0 && bestD <= 26.0f) {
                            notes[best].active = 0;
                            combo++;
                            if (combo > maxCombo) maxCombo = combo;
                            int perf = (bestD <= 11.0f);
                            int dmg = actH->atk + (combo / 4) * 2;
                            if (perf) dmg = (dmg * 130) / 100;
                            if ((int)(xr() % 100) < actH->critPct) dmg = (dmg * 150) / 100;
                            eHp -= dmg;
                            if (eHp < 0) eHp = 0;
                            lastDmg = dmg;
                            enemyFlash = 6;
                            actH->mp += perf ? 9 : 6;
                            if (actH->mp > actH->maxMp) actH->mp = actH->maxMp;
                            if (actH->lifesteal > 0 && actH->hp > 0) {
                                actH->hp += actH->lifesteal;
                                if (actH->hp > actH->maxHp) actH->hp = actH->maxHp;
                            }
                            snprintf(judge, sizeof(judge), perf ? "PERFECT! -%d" : "BIEN! -%d", dmg);
                            judgeT = 28;
                            play_sfx(notes[best].freq, 1800);
                        } else {
                            combo = 0;
                            int edmg = eAtk - actH->def / 2;
                            if (edmg < 2) edmg = 2;
                            actH->hp -= edmg;
                            if (actH->hp < 0) actH->hp = 0;
                            heroFlash = 8;
                            snprintf(judge, sizeof(judge), "FALLO -%dHP", edmg);
                            judgeT = 28;
                            play_sfx(140.0f, 900);
                        }
                    }
                }

                for (int i = 0; i < MAX_NOTES; i++) {
                    if (!notes[i].active) continue;
                    notes[i].y += notes[i].spd;
                    if (notes[i].y > HIT_Y + 28.0f) {
                        notes[i].active = 0;
                        combo = 0;
                        int edmg = eAtk - actH->def / 2;
                        if (edmg < 2) edmg = 2;
                        actH->hp -= edmg;
                        if (actH->hp < 0) actH->hp = 0;
                        heroFlash = 8;
                        snprintf(judge, sizeof(judge), "MISS -%dHP", edmg);
                        judgeT = 26;
                        play_sfx(125.0f, 800);
                    }
                }

                /* Check Floor Victory / Turn Relay / Defeat */
                if (eHp <= 0) {
                    if (isMulti) {
                        NetPacket vp;
                        build_net_packet(&vp, PKT_STATE, 1);
                        send_pkt(&vp);
                    }
                    roll_cards(&players[mySlot]);
                    state = ST_CARDS;
                    play_sfx(1040.0f, 2200);
                } else if (isMulti) {
                    turnTimer--;
                    if ((++netTick % 3) == 0) {
                        NetPacket sp;
                        build_net_packet(&sp, PKT_STATE, 0);
                        send_pkt(&sp);
                    }
                    if (actH->hp <= 0 || turnTimer <= 0) {
                        int other = activeTurn ^ 1;
                        if (players[other].hp > 0) {
                            activeTurn = other;
                            turnTimer = TURN_FRAMES;
                            memset(notes, 0, sizeof(notes));
                            combo = 0;
                            NetPacket tp;
                            build_net_packet(&tp, PKT_TURN_END, 0);
                            send_pkt(&tp);
                            snprintf(judge, sizeof(judge), "TURNO DE J%d!", activeTurn + 1);
                            judgeT = 45;
                        } else {
                            /* Both players defeated -> Debuff! */
                            NetPacket dp;
                            build_net_packet(&dp, PKT_STATE, 2);
                            send_pkt(&dp);
                            lastDebuffId = (int)(xr() & 3);
                            apply_debuff(&players[mySlot], lastDebuffId);
                            state = ST_DEBUFF;
                            play_sfx(110.0f, 2000);
                        }
                    }
                } else {
                    if (actH->hp <= 0) {
                        lastDebuffId = (int)(xr() & 3);
                        apply_debuff(&players[0], lastDebuffId);
                        state = ST_DEBUFF;
                        play_sfx(110.0f, 2000);
                    }
                }
            } else {
                /* Spectator turn in 2P Ad-Hoc: receive live updates from active peer */
                NetPacket rp;
                if (recv_pkt(&rp, NULL)) {
                    eHp = rp.eHp; eMaxHp = rp.eMaxHp;
                    players[0].hp = rp.p1Hp; players[0].maxHp = rp.p1MaxHp; players[0].mp = rp.p1Mp;
                    players[1].hp = rp.p2Hp; players[1].maxHp = rp.p2MaxHp; players[1].mp = rp.p2Mp;
                    combo = rp.activeCombo;
                    if (rp.lastDmg != lastDmg) { lastDmg = rp.lastDmg; enemyFlash = 5; }
                    if (rp.floorResult == 1) {
                        roll_cards(&players[mySlot]);
                        state = ST_CARDS;
                        play_sfx(1040.0f, 2200);
                    } else if (rp.floorResult == 2) {
                        lastDebuffId = (int)(xr() & 3);
                        apply_debuff(&players[mySlot], lastDebuffId);
                        state = ST_DEBUFF;
                        play_sfx(110.0f, 2000);
                    } else if (rp.type == PKT_TURN_END) {
                        activeTurn = rp.activeTurn;
                        turnTimer = TURN_FRAMES;
                        memset(notes, 0, sizeof(notes));
                        combo = 0;
                        snprintf(judge, sizeof(judge), "TU TURNO!");
                        judgeT = 45;
                        play_sfx(880.0f, 1000);
                    }
                }
            }
        } else if (state == ST_CARDS) {
            if (!players[mySlot].cardDone) {
                if (pressed & PSP_CTRL_LEFT)  { cardCursor = (cardCursor + 2) % 3; play_sfx(520.0f, 400); }
                if (pressed & PSP_CTRL_RIGHT) { cardCursor = (cardCursor + 1) % 3; play_sfx(520.0f, 400); }
                if (pressed & PSP_CTRL_CROSS) {
                    apply_card(&players[mySlot], offeredCards[cardCursor]);
                    play_sfx(880.0f, 1000);
                    if (!isMulti) {
                        setup_floor(towerFloor + 1);
                        introTimer = 80;
                        state = ST_FLOOR_INTRO;
                    } else {
                        NetPacket cp;
                        build_net_packet(&cp, PKT_CARD_DONE, 1);
                        send_pkt(&cp);
                    }
                }
            } else if (isMulti) {
                if ((animFrame % 15) == 0) {
                    NetPacket cp;
                    build_net_packet(&cp, PKT_CARD_DONE, 1);
                    send_pkt(&cp);
                }
                NetPacket rp;
                if (recv_pkt(&rp, NULL) && rp.type == PKT_CARD_DONE && rp.cardReady) {
                    players[mySlot ^ 1].cardDone = 1;
                }
                if (players[0].cardDone && players[1].cardDone) {
                    setup_floor(towerFloor + 1);
                    introTimer = 80;
                    state = ST_FLOOR_INTRO;
                }
            }
        } else if (state == ST_DEBUFF) {
            if (pressed & PSP_CTRL_CROSS) {
                play_sfx(640.0f, 800);
                if (!isMulti) {
                    setup_floor(towerFloor);
                    introTimer = 75;
                    state = ST_FLOOR_INTRO;
                } else {
                    NetPacket rp;
                    build_net_packet(&rp, PKT_RETRY_FLOOR, 0);
                    send_pkt(&rp);
                    setup_floor(towerFloor);
                    introTimer = 75;
                    state = ST_FLOOR_INTRO;
                }
            } else if (pressed & PSP_CTRL_START) {
                init_hero(&players[0], myHeroIdx);
                if (isMulti) init_hero(&players[1], p2HeroIdx);
                setup_floor(1);
                introTimer = 75;
                state = ST_FLOOR_INTRO;
            }
            if (isMulti) {
                NetPacket rp;
                if (recv_pkt(&rp, NULL) && rp.type == PKT_RETRY_FLOOR) {
                    setup_floor(towerFloor);
                    introTimer = 75;
                    state = ST_FLOOR_INTRO;
                }
            }
        }

        /* ==================== RENDER ==================== */
        if (state == ST_TITLE) {
            draw_bg_full(fb, bg_title);
            draw_ff_window(fb, 34, 22, 412, 46);
            draw_text_2x(fb, 68, 32, "TORRE DEL ABISMO", RGB(255, 225, 90));
            draw_text(fb, 132, 52, "RPG GUITAR HERO AD-HOC", RGB(150, 225, 255));

            draw_ff_window(fb, 44, 82, 392, 74);
            draw_text(fb, 62, 92, "ELIGE TU HEROE (< / >):", RGB(255, 235, 130));
            draw_spr_scaled(fb, heroSprs[myHeroIdx], 48, 48, 66, 106, 42, 42, 0, 0);
            draw_text(fb, 122, 110, heroNames[myHeroIdx], RGB(255, 255, 255));
            char stats[64];
            snprintf(stats, sizeof(stats), "HP:%d  ATK:%d  DEF:%d", heroBaseHp[myHeroIdx], heroBaseAtk[myHeroIdx], heroBaseDef[myHeroIdx]);
            draw_text(fb, 122, 126, stats, RGB(140, 255, 165));

            draw_ff_window(fb, 90, 170, 300, 68);
            draw_text(fb, 118, 186, menuCursor == 0 ? "> SOLO (TORRE INFINITA)" : "  SOLO (TORRE INFINITA)", menuCursor == 0 ? RGB(255, 230, 80) : RGB(220, 225, 240));
            draw_text(fb, 118, 208, menuCursor == 1 ? "> MULTIJUGADOR AD-HOC (PSP-PSP)" : "  MULTIJUGADOR AD-HOC (PSP-PSP)", menuCursor == 1 ? RGB(110, 255, 160) : RGB(220, 225, 240));
            draw_text(fb, 118, 250, "X: ACEPTAR   < / >: CAMBIAR HEROE", RGB(180, 200, 240));
        } else if (state == ST_ADHOC_MENU || state == ST_ADHOC_WAIT) {
            draw_bg_full(fb, bg_title);
            draw_ff_window(fb, 54, 46, 372, 176);
            draw_text_2x(fb, 92, 62, "SALA AD-HOC PSP", RGB(110, 245, 180));
            if (state == ST_ADHOC_MENU) {
                draw_text(fb, 96, 108, adhocCursor == 0 ? "> CREAR SALA (HOST - JUGADOR 1)" : "  CREAR SALA (HOST - JUGADOR 1)", adhocCursor == 0 ? RGB(255, 230, 80) : RGB(220, 225, 240));
                draw_text(fb, 96, 132, adhocCursor == 1 ? "> UNIRSE A SALA (JUGADOR 2)" : "  UNIRSE A SALA (JUGADOR 2)", adhocCursor == 1 ? RGB(110, 235, 255) : RGB(220, 225, 240));
                draw_text(fb, 84, 170, netStatus, RGB(255, 180, 110));
                draw_text(fb, 84, 196, "X: CONECTAR   O: VOLVER", RGB(190, 210, 240));
            } else {
                draw_text(fb, 84, 116, netStatus, RGB(255, 235, 110));
                draw_text(fb, 84, 146, "Asegurate de tener el Wi-Fi ON en ambas PSP.", RGB(180, 220, 255));
                draw_text(fb, 84, 196, "O: CANCELAR Y VOLVER", RGB(255, 130, 130));
            }
        } else if (state == ST_FLOOR_INTRO) {
            draw_bg_full(fb, bg_arena);
            draw_ff_window(fb, 76, 68, 328, 128);
            char fbuf[48];
            snprintf(fbuf, sizeof(fbuf), eIsBoss ? "PISO %d - JEFE!" : "PISO %d", towerFloor);
            draw_text_2x(fb, 150, 88, fbuf, eIsBoss ? RGB(255, 90, 120) : RGB(255, 225, 90));
            draw_text(fb, 116, 126, eName, RGB(255, 255, 255));
            if (isMulti) {
                snprintf(fbuf, sizeof(fbuf), "EMPIEZA TURNO: JUGADOR %d", activeTurn + 1);
                draw_text(fb, 148, 152, fbuf, RGB(120, 255, 165));
            }
            draw_text(fb, 156, 176, "PULSA X PARA EMPEZAR", RGB(180, 210, 255));
        } else if (state == ST_BATTLE) {
            draw_bg_full(fb, bg_arena);

            /* Top HUD: Floor & Enemy HP */
            draw_ff_window(fb, 8, 4, 464, 34);
            char tbuf[64];
            snprintf(tbuf, sizeof(tbuf), "PISO %d %s", towerFloor, eIsBoss ? "[JEFE]" : "");
            draw_text(fb, 16, 10, tbuf, eIsBoss ? RGB(255, 100, 120) : RGB(255, 225, 90));
            draw_text(fb, 16, 22, eName, RGB(230, 235, 255));
            draw_hp_bar(fb, 230, 10, 232, 12, eHp, eMaxHp, RGB(230, 60, 85));
            snprintf(tbuf, sizeof(tbuf), "HP:%d/%d", eHp, eMaxHp);
            draw_text(fb, 236, 25, tbuf, RGB(255, 195, 200));

            /* Heroes on Left */
            int bob = (animFrame >> 3) & 1;
            draw_spr_scaled(fb, players[0].spr, 48, 48, 42, 92 - bob, 56, 56, 0, activeTurn == 0 ? heroFlash : 0);
            if (isMulti) {
                draw_spr_scaled(fb, players[1].spr, 48, 48, 12, 122 - (bob ^ 1), 46, 46, 0, activeTurn == 1 ? heroFlash : 0);
            }

            /* Enemy / Boss on Right */
            const unsigned int *espr = eIsBoss
                ? (eSpriteId == 0 ? boss_golem : (eSpriteId == 1 ? boss_automaton : boss_dragon))
                : (eSpriteId == 0 ? mob_slime : (eSpriteId == 1 ? mob_goblin : mob_wraith));
            int esz = eIsBoss ? 86 : 64;
            draw_spr_scaled(fb, espr, 64, 64, 386 - (eIsBoss ? 14 : 0), 90 - (eIsBoss ? 16 : 0) + bob, esz, esz, 0, enemyFlash);

            /* Highway in Center-Bottom */
            fill_rect_alpha(fb, 140, 42, 200, 224, RGB(8, 12, 28), 205);
            draw_border(fb, 140, 42, 200, 224, RGB(120, 160, 240));
            for (int l = 0; l < 4; l++) {
                int lx = 148 + l * 48;
                fill_rect(fb, lx + 18, 44, 2, 218, RGB(50, 62, 100));
                draw_border(fb, lx + 4, HIT_Y - 8, 30, 16, laneCols[l]);
                draw_text(fb, lx + 13, HIT_Y - 3, laneLbl[l], laneCols[l]);
            }

            int myTurn = (!isMulti) || (activeTurn == mySlot);
            if (myTurn) {
                for (int i = 0; i < MAX_NOTES; i++) {
                    if (!notes[i].active) continue;
                    int nx = 154 + notes[i].lane * 48;
                    int ny = (int)notes[i].y;
                    fill_rect(fb, nx, ny - 6, 26, 12, laneCols[notes[i].lane]);
                    draw_border(fb, nx, ny - 6, 26, 12, RGB(255, 255, 255));
                }
            } else {
                draw_ff_window(fb, 152, 120, 176, 54);
                snprintf(tbuf, sizeof(tbuf), "TURNO DE JUGADOR %d", activeTurn + 1);
                draw_text(fb, 180, 134, tbuf, RGB(255, 225, 90));
                draw_text(fb, 174, 152, "OBSERVANDO EN VIVO...", RGB(140, 235, 255));
            }

            /* Player Status Panels */
            draw_ff_window(fb, 6, 184, 128, 82);
            snprintf(tbuf, sizeof(tbuf), "J1 %s", activeTurn == 0 ? "[ACT]" : "");
            draw_text(fb, 12, 192, tbuf, RGB(255, 230, 110));
            draw_hp_bar(fb, 12, 204, 114, 10, players[0].hp, players[0].maxHp, RGB(70, 230, 120));
            draw_hp_bar(fb, 12, 218, 114, 8, players[0].mp, players[0].maxMp, RGB(70, 165, 255));
            snprintf(tbuf, sizeof(tbuf), "HP:%d ATK:%d", players[0].hp, players[0].atk);
            draw_text(fb, 12, 232, tbuf, RGB(225, 235, 255));
            snprintf(tbuf, sizeof(tbuf), "Debuf:%d", players[0].debuffCount);
            draw_text(fb, 12, 246, tbuf, players[0].debuffCount > 0 ? RGB(255, 110, 120) : RGB(140, 220, 180));

            if (isMulti) {
                draw_ff_window(fb, 346, 184, 128, 82);
                snprintf(tbuf, sizeof(tbuf), "J2 %s", activeTurn == 1 ? "[ACT]" : "");
                draw_text(fb, 352, 192, tbuf, RGB(120, 235, 255));
                draw_hp_bar(fb, 352, 204, 114, 10, players[1].hp, players[1].maxHp, RGB(70, 230, 120));
                draw_hp_bar(fb, 352, 218, 114, 8, players[1].mp, players[1].maxMp, RGB(70, 165, 255));
                snprintf(tbuf, sizeof(tbuf), "HP:%d/%d", players[1].hp, players[1].maxHp);
                draw_text(fb, 352, 232, tbuf, RGB(225, 235, 255));
                snprintf(tbuf, sizeof(tbuf), "Relevo:%ds", turnTimer / 60);
                draw_text(fb, 352, 246, tbuf, RGB(255, 220, 120));
            } else {
                draw_ff_window(fb, 346, 184, 128, 82);
                snprintf(tbuf, sizeof(tbuf), "COMBO: x%d", combo);
                draw_text(fb, 354, 194, tbuf, RGB(255, 230, 80));
                draw_text(fb, 354, 214, "L: CURAR (30MP)", RGB(110, 255, 175));
                draw_text(fb, 354, 230, "R: SINFONIA (50)", RGB(255, 145, 235));
                snprintf(tbuf, sizeof(tbuf), "RECORD: P.%d", bestFloor);
                draw_text(fb, 354, 248, tbuf, RGB(170, 210, 255));
            }

            if (judgeT > 0) {
                draw_text(fb, 186, 62, judge, RGB(255, 240, 110));
            }
        } else if (state == ST_CARDS) {
            draw_bg_full(fb, bg_arena);
            draw_ff_window(fb, 42, 16, 396, 44);
            draw_text_2x(fb, 76, 26, "VICTORIA DE PISO!", RGB(120, 255, 160));
            draw_text(fb, 114, 48, "ELIGE 1 TARJETA DE MEJORA (< / > y X)", RGB(230, 240, 255));

            for (int i = 0; i < 3; i++) {
                int cx = 24 + i * 148;
                int sel = (i == cardCursor);
                draw_ff_window(fb, cx, 78, 136, 148);
                if (sel) draw_border(fb, cx - 2, 76, 140, 152, RGB(255, 230, 70));
                const CardDef *cd = &cardPool[offeredCards[i]];
                draw_text(fb, cx + 10, 96, cd->title, sel ? RGB(255, 235, 90) : RGB(220, 230, 255));
                draw_text(fb, cx + 10, 130, cd->desc, RGB(130, 255, 175));
                if (sel) draw_text(fb, cx + 28, 198, "[ PULSA X ]", RGB(255, 225, 80));
            }
            if (isMulti && players[mySlot].cardDone) {
                draw_ff_window(fb, 100, 234, 280, 28);
                draw_text(fb, 122, 244, "Esperando a que el otro jugador elija...", RGB(255, 230, 110));
            }
        } else if (state == ST_DEBUFF) {
            draw_bg_full(fb, bg_arena);
            draw_ff_window(fb, 46, 52, 388, 168);
            draw_text_2x(fb, 138, 68, "DERROTA...", RGB(255, 85, 100));
            draw_text(fb, 84, 108, "HAS RECIBIDO UN DEBUFO:", RGB(255, 200, 120));
            draw_text(fb, 84, 128, debuffNames[lastDebuffId], RGB(255, 115, 135));
            draw_text(fb, 84, 162, "X: REINTENTAR PISO CON DEBUFO", RGB(120, 255, 165));
            draw_text(fb, 84, 184, "START: REINICIAR DESDE PISO 1", RGB(180, 210, 255));
        }

        /* Hardware GE DMA transfer from L1-cached sys_fb to VRAM */
        sceKernelDcacheWritebackRange(sys_fb, BUF_W * SCR_H * 4);
        sceGuStart(GU_DIRECT, gulist);
        sceGuCopyImage(GU_PSM_8888, 0, 0, SCR_W, SCR_H, BUF_W, sys_fb, 0, 0, BUF_W, (void *)(((unsigned int)vram_bufs[cur_fb]) & 0x1FFFFFFFu));
        sceGuFinish();
        sceGuSync(0, 0);

        sceDisplayWaitVblankStart();
        sceGuSwapBuffers();
        cur_fb ^= 1;
    }

    shutdown_adhoc_stack();
    arun = 0;
    if (ath >= 0) sceKernelWaitThreadEnd(ath, NULL);
    if (ach >= 0) sceAudioChRelease(ach);
    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
