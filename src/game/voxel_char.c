/*
 * Mod "Blocos": personagem quadradao original ("Mineiro").
 *
 * Em vez de editar o modelo, cada malha do corpo original e trocada em
 * tempo de renderizacao por uma peca feita de caixas. O esqueleto e as
 * animacoes do jogo continuam iguais, entao o Mineiro anda, pula, mergulha
 * e soca com a fisica e as animacoes originais.
 *
 * Eixos locais de cada osso: X ao longo do membro, Y para a frente, Z lateral.
 */

#include <stdlib.h>
#include <string.h>

#include "sm64.h"
#include "voxel.h"
#include "actors/group0.h"


enum CharPart {
    PART_EMPTY, PART_HEAD, PART_HELMET, PART_TORSO, PART_HIPS, PART_ARM, PART_FOREARM, PART_HAND,
    PART_THIGH, PART_LEG, PART_FOOT, PART_HAND_R, PART_COUNT
};

struct DlMap {
    const Gfx *orig;
    u8 part;
};

static struct DlMap sMap[] = {
#include "voxel_char_table.inc.h"
};
#define MAP_LEN (sizeof(sMap) / sizeof(sMap[0]))

/* --- paleta original do Mineiro --- */
enum { COR_PELE, COR_CAMISA, COR_CALCA, COR_BOTA, COR_CAPACETE, COR_LANTERNA, COR_OLHO, COR_BOCA, COR_LUVA, COR_CINTO, COR_COUNT };
static const u8 sCores[COR_COUNT][3] = {
    { 226, 176, 132 }, // pele
    { 228, 112, 38 },  // camisa laranja
    { 58, 72, 98 },    // calca azul-ardosia
    { 72, 46, 30 },    // botas
    { 242, 196, 36 },  // capacete amarelo
    { 255, 250, 210 }, // lanterna
    { 28, 28, 36 },    // olhos
    { 132, 66, 52 },   // boca
    { 112, 74, 42 },   // luvas de couro
    { 40, 30, 24 },    // cinto
};

struct Box {
    s16 x0, y0, z0, x1, y1, z1;
    u8 cor;
};

/* caixas de cada peca, no espaco local do osso */
static const struct Box sHead[] = {
    { 0, -66, -66, 128, 66, 66, COR_PELE },
    { 112, -72, -72, 158, 72, 72, COR_CAPACETE },     // capacete
    { 108, 60, -72, 120, 96, 72, COR_CAPACETE },      // aba da frente
    { 122, 72, -15, 146, 86, 15, COR_LANTERNA },      // lanterna
    { 62, 64, 16, 84, 70, 40, COR_OLHO },             // olho
    { 62, 64, -40, 84, 70, -16, COR_OLHO },           // olho
    { 26, 64, -24, 34, 70, 24, COR_BOCA },            // boca
};
static const struct Box sHelmet[] = {
    { -10, -40, -40, 20, 40, 40, COR_CAPACETE },
    { 4, 30, -40, 12, 56, 40, COR_CAPACETE },
};
static const struct Box sTorso[] = {
    { -6, -42, -66, 96, 42, 66, COR_CAMISA },
};
static const struct Box sHips[] = {
    { -26, -44, -66, 10, 44, 66, COR_CALCA },
    { 4, -45, -67, 14, 45, 67, COR_CINTO },
};
static const struct Box sArm[] = {
    { -8, -24, -24, 70, 24, 24, COR_CAMISA },
};
static const struct Box sForearm[] = {
    { -6, -21, -21, 62, 21, 21, COR_PELE },
};
static const struct Box sHand[] = {
    { -6, -23, -23, 40, 23, 23, COR_LUVA },
};
static const struct Box sThigh[] = {
    { -8, -29, -29, 92, 29, 29, COR_CALCA },
};
static const struct Box sLeg[] = {
    { -6, -27, -27, 70, 27, 27, COR_CALCA },
};
static const struct Box sFoot[] = {
    { -26, -30, -30, 70, 34, 30, COR_BOTA },
};

struct PartDef {
    const struct Box *boxes;
    s32 count;
};
static const struct PartDef sParts[PART_COUNT] = {
    { NULL, 0 },
    { sHead, sizeof(sHead) / sizeof(sHead[0]) },
    { sHelmet, sizeof(sHelmet) / sizeof(sHelmet[0]) },
    { sTorso, 1 },
    { sHips, 2 },
    { sArm, 1 },
    { sForearm, 1 },
    { sHand, 1 },
    { sThigh, 1 },
    { sLeg, 1 },
    { sFoot, 1 },
    { sHand, 1 },
};

#define MAX_BOXES_TOTAL 32
static Vtx sCharVtx[MAX_BOXES_TOTAL * 24];
static Gfx sCharGfx[PART_COUNT][8 * 10 + 8];
static Lights1 sLights[COR_COUNT];
static Gfx sEmptyDl[1];
static u8 sInit = FALSE;

static int cmp_map(const void *a, const void *b) {
    const struct DlMap *x = a, *y = b;
    return (x->orig > y->orig) - (x->orig < y->orig);
}

static const s8 sN[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };

static void box_face(s32 f, s32 c[4][3]) {
    static const s32 tbl[6][4][3] = {
        { { 1, 1, 1 }, { 1, 0, 1 }, { 1, 0, 0 }, { 1, 1, 0 } },
        { { 0, 1, 0 }, { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 1 } },
        { { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 } },
        { { 0, 0, 1 }, { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 } },
        { { 0, 1, 1 }, { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 } },
        { { 1, 1, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 0, 1, 0 } },
    };
    memcpy(c, tbl[f], sizeof(tbl[f]));
    // garante ordem anti-horaria vista de fora
    s32 ax = c[1][0] - c[0][0], ay = c[1][1] - c[0][1], az = c[1][2] - c[0][2];
    s32 bx = c[2][0] - c[0][0], by = c[2][1] - c[0][1], bz = c[2][2] - c[0][2];
    s32 nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
    if (nx * sN[f][0] + ny * sN[f][1] + nz * sN[f][2] < 0) {
        s32 t[3];
        memcpy(t, c[1], sizeof(t));
        memcpy(c[1], c[3], sizeof(t));
        memcpy(c[3], t, sizeof(t));
    }
}

static void init_char(void) {
    qsort(sMap, MAP_LEN, sizeof(sMap[0]), cmp_map);
    gSPEndDisplayList(&sEmptyDl[0]);

    for (s32 i = 0; i < COR_COUNT; i++) {
        Lights1 *l = &sLights[i];
        bzero(l, sizeof(*l));
        for (s32 k = 0; k < 3; k++) {
            u8 c = sCores[i][k];
            u8 amb = (u8)(c * 0.45f);
            if (i == COR_LANTERNA) {
                amb = c; // a lanterna "brilha"
            }
            l->a.l.col[k] = l->a.l.colc[k] = amb;
            l->l[0].l.col[k] = l->l[0].l.colc[k] = c;
        }
        l->l[0].l.dir[0] = 0x28;
        l->l[0].l.dir[1] = 0x28;
        l->l[0].l.dir[2] = 0x28;
    }

    Vtx *v = sCharVtx;
    for (s32 p = 1; p < PART_COUNT; p++) {
        Gfx *g = sCharGfx[p];
        gDPPipeSync(g++);
        gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
        gSPSetGeometryMode(g++, G_LIGHTING | G_SHADE | G_CULL_BACK);
        gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
        for (s32 b = 0; b < sParts[p].count; b++) {
            const struct Box *bx = &sParts[p].boxes[b];
            Vtx *bv = v;
            s16 lo[3] = { bx->x0, bx->y0, bx->z0 }, hi[3] = { bx->x1, bx->y1, bx->z1 };
            for (s32 f = 0; f < 6; f++) {
                s32 c[4][3];
                box_face(f, c);
                for (s32 k = 0; k < 4; k++) {
                    v->n.ob[0] = c[k][0] ? hi[0] : lo[0];
                    v->n.ob[1] = c[k][1] ? hi[1] : lo[1];
                    v->n.ob[2] = c[k][2] ? hi[2] : lo[2];
                    v->n.flag = 0;
                    v->n.tc[0] = v->n.tc[1] = 0;
                    v->n.n[0] = sN[f][0] * 127;
                    v->n.n[1] = sN[f][1] * 127;
                    v->n.n[2] = sN[f][2] * 127;
                    v->n.a = 255;
                    v++;
                }
            }
            gSPSetLights1(g++, sLights[bx->cor]);
            gSPVertex(g++, bv, 24, 0);
            for (s32 f = 0; f < 6; f++) {
                s32 q = f * 4;
                gSP2Triangles(g++, q, q + 1, q + 2, 0, q, q + 2, q + 3, 0);
            }
        }
        gSPEndDisplayList(g++);
    }
    sInit = TRUE;
}

static u8 sHideForFirstPerson(void);

// mao direita + item segurado (espada ou arco)
static Gfx sHandWithItem[2][4];
static const Gfx *right_hand_dl(void) {
    u8 held = voxel_held_item();
    const Gfx *item = voxel_items_hand_dl(held);

    if (item == NULL) {
        return sCharGfx[PART_HAND_R];
    }
    s32 idx = held == ITEM_ESPADA ? 0 : 1;
    Gfx *g = sHandWithItem[idx];
    gSPDisplayList(g++, sCharGfx[PART_HAND_R]);
    gSPDisplayList(g++, item);
    gSPEndDisplayList(g++);
    return sHandWithItem[idx];
}

const Gfx *voxel_char_substitute(const Gfx *dl) {
    if (dl == NULL) {
        return NULL;
    }
    if (!sInit) {
        init_char();
    }
    // busca binaria pelo ponteiro da malha original
    s32 lo = 0, hi = (s32) MAP_LEN - 1;
    while (lo <= hi) {
        s32 mid = (lo + hi) / 2;
        if (sMap[mid].orig == dl) {
            u8 p = sMap[mid].part;
            if (p == PART_EMPTY || sHideForFirstPerson()) {
                return sEmptyDl;
            }
            if (p == PART_HAND_R) {
                return right_hand_dl();
            }
            return sCharGfx[p];
        }
        if (sMap[mid].orig < dl) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return dl;
}

extern u8 gVoxelHideMario;
static u8 sHideForFirstPerson(void) {
    return gVoxelHideMario;
}
