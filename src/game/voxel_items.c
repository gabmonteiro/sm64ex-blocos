/*
 * Mod "Blocos": itens - espada de cristal e arco e flecha.
 *
 *  - Espada: golpe com animacao de arco (o braco desce em curva girando),
 *    alcance ~3 blocos, acerta ate 3 inimigos no cone a frente.
 *  - Arco: segure o botao direito para puxar, solte para atirar. Forca segue
 *    a curva de carga classica (carga^2 + 2*carga)/3, flecha com gravidade,
 *    arrasto do ar e da agua, crava em blocos/terreno e acerta inimigos.
 *
 * Os modelos sao montados em codigo com caixas e "vigas" orientadas.
 * Na mao (1a pessoa) o item e desenhado no espaco da camera, com as faces
 * ordenadas de tras para frente (o porte ignora limpar o z-buffer no meio
 * do frame, entao nao da para usar profundidade aqui).
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "sm64.h"
#include "voxel.h"
#include "area.h"
#include "camera.h"
#include "game_init.h"
#include "interaction.h"
#include "level_update.h"
#include "mario.h"
#include "memory.h"
#include "object_list_processor.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "audio/external.h"
#include "pc/fs/fs.h"
#include <stb/stb_image.h>

/* ------------------------------------------------------------------------- */
/* Matematica pequena                                                        */
/* ------------------------------------------------------------------------- */

typedef f32 M3[3][3];

struct Xform {
    M3 r;      // rotacao (colunas = eixos do item no espaco de destino)
    Vec3f t;   // translacao
    f32 s;     // escala
};

static void m3_ident(M3 m) {
    bzero(m, sizeof(M3));
    m[0][0] = m[1][1] = m[2][2] = 1.0f;
}

static void m3_mul(M3 out, M3 a, M3 b) {
    M3 r;
    for (s32 i = 0; i < 3; i++) {
        for (s32 j = 0; j < 3; j++) {
            r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
        }
    }
    memcpy(out, r, sizeof(M3));
}

static void m3_rot(M3 m, s32 axis, f32 ang) {
    f32 c = cosf(ang), s = sinf(ang);
    m3_ident(m);
    if (axis == 0) { m[1][1] = c; m[1][2] = -s; m[2][1] = s; m[2][2] = c; }
    if (axis == 1) { m[0][0] = c; m[0][2] = s; m[2][0] = -s; m[2][2] = c; }
    if (axis == 2) { m[0][0] = c; m[0][1] = -s; m[1][0] = s; m[1][1] = c; }
}

// out = rot(axis, ang) * m  (aplica a rotacao depois)
static void m3_prerot(M3 m, s32 axis, f32 ang) {
    M3 r;
    m3_rot(r, axis, ang);
    m3_mul(m, r, m);
}

static void xf_point(const struct Xform *x, const f32 p[3], Vec3f out) {
    for (s32 i = 0; i < 3; i++) {
        out[i] = x->t[i] + x->s * (x->r[i][0] * p[0] + x->r[i][1] * p[1] + x->r[i][2] * p[2]);
    }
}

static void xf_dir(const struct Xform *x, const f32 d[3], Vec3f out) {
    for (s32 i = 0; i < 3; i++) {
        out[i] = x->r[i][0] * d[0] + x->r[i][1] * d[1] + x->r[i][2] * d[2];
    }
}

/* ------------------------------------------------------------------------- */
/* Coletor de quads                                                          */
/* ------------------------------------------------------------------------- */

struct Quad {
    Vec3f p[4];
    u8 c[3];
    f32 depth;
};

struct QuadSink {
    struct Quad *q;
    s32 n, max;
    Vec3f light;  // direcao da luz "assada" nas cores
};

static void sink_quad(struct QuadSink *k, Vec3f p[4], Vec3f n, const u8 col[3]) {
    if (k->n >= k->max) {
        return;
    }
    struct Quad *q = &k->q[k->n++];
    // ordem anti-horaria vista de fora
    Vec3f a, b, c;
    vec3f_dif(a, p[1], p[0]);
    vec3f_dif(b, p[2], p[0]);
    vec3f_cross(c, a, b);
    s32 flip = (c[0] * n[0] + c[1] * n[1] + c[2] * n[2]) < 0;
    for (s32 i = 0; i < 4; i++) {
        vec3f_copy(q->p[i], p[flip ? (4 - i) % 4 : i]);
    }
    f32 d = n[0] * k->light[0] + n[1] * k->light[1] + n[2] * k->light[2];
    f32 sh = 0.58f + 0.42f * (d > 0 ? d : 0);
    for (s32 i = 0; i < 3; i++) {
        f32 v = col[i] * sh;
        q->c[i] = v > 255 ? 255 : (u8) v;
    }
    q->depth = (q->p[0][2] + q->p[1][2] + q->p[2][2] + q->p[3][2]) * 0.25f;
}

static const s32 sBoxFace[6][4][3] = {
    { { 1, 1, 1 }, { 1, 0, 1 }, { 1, 0, 0 }, { 1, 1, 0 } },
    { { 0, 1, 0 }, { 0, 0, 0 }, { 0, 0, 1 }, { 0, 1, 1 } },
    { { 0, 1, 0 }, { 0, 1, 1 }, { 1, 1, 1 }, { 1, 1, 0 } },
    { { 0, 0, 1 }, { 0, 0, 0 }, { 1, 0, 0 }, { 1, 0, 1 } },
    { { 0, 1, 1 }, { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 } },
    { { 1, 1, 0 }, { 1, 0, 0 }, { 0, 0, 0 }, { 0, 1, 0 } },
};
static const f32 sBoxN[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };

// caixa alinhada no espaco do item
static void add_box(struct QuadSink *k, const struct Xform *x, f32 x0, f32 y0, f32 z0, f32 x1, f32 y1, f32 z1,
                    u8 r, u8 g, u8 b) {
    f32 lo[3] = { x0, y0, z0 }, hi[3] = { x1, y1, z1 };
    u8 col[3] = { r, g, b };
    for (s32 f = 0; f < 6; f++) {
        Vec3f p[4], n;
        for (s32 c = 0; c < 4; c++) {
            f32 q[3];
            for (s32 i = 0; i < 3; i++) {
                q[i] = sBoxFace[f][c][i] ? hi[i] : lo[i];
            }
            xf_point(x, q, p[c]);
        }
        xf_dir(x, sBoxN[f], n);
        sink_quad(k, p, n, col);
    }
}

// "viga": caixa de secao quadrada entre dois pontos do espaco do item
static void add_beam(struct QuadSink *k, const struct Xform *x, const f32 a[3], const f32 b[3], f32 half,
                     u8 r, u8 g, u8 bl) {
    Vec3f pa, pb, d, up = { 0, 1, 0 }, u, w;
    xf_point(x, a, pa);
    xf_point(x, b, pb);
    vec3f_dif(d, pb, pa);
    if (vec3f_length(d) < 0.001f) {
        return;
    }
    vec3f_normalize(d);
    if (fabsf(d[1]) > 0.9f) {
        up[0] = 1; up[1] = 0; up[2] = 0;
    }
    vec3f_cross(u, d, up);
    vec3f_normalize(u);
    vec3f_cross(w, u, d);
    f32 h = half * x->s;
    Vec3f c[8]; // 0-3 em pa, 4-7 em pb
    const f32 su[4] = { 1, -1, -1, 1 }, sw[4] = { 1, 1, -1, -1 };
    for (s32 i = 0; i < 4; i++) {
        for (s32 j = 0; j < 3; j++) {
            c[i][j] = pa[j] + (u[j] * su[i] + w[j] * sw[i]) * h;
            c[i + 4][j] = pb[j] + (u[j] * su[i] + w[j] * sw[i]) * h;
        }
    }
    u8 col[3] = { r, g, bl };
    for (s32 i = 0; i < 4; i++) {
        s32 i2 = (i + 1) % 4;
        Vec3f p[4], n;
        vec3f_copy(p[0], c[i]);
        vec3f_copy(p[1], c[i2]);
        vec3f_copy(p[2], c[i2 + 4]);
        vec3f_copy(p[3], c[i + 4]);
        for (s32 j = 0; j < 3; j++) {
            n[j] = u[j] * (su[i] + su[i2]) + w[j] * (sw[i] + sw[i2]);
        }
        vec3f_normalize(n);
        sink_quad(k, p, n, col);
    }
    Vec3f p[4], nd;
    for (s32 i = 0; i < 4; i++) vec3f_copy(p[i], c[i]);
    nd[0] = -d[0]; nd[1] = -d[1]; nd[2] = -d[2];
    sink_quad(k, p, nd, col);
    for (s32 i = 0; i < 4; i++) vec3f_copy(p[i], c[i + 4]);
    sink_quad(k, p, d, col);
}

/* ------------------------------------------------------------------------- */
/* Modelos (espaco do item)                                                  */
/* ------------------------------------------------------------------------- */

// Espada de cristal: lamina ao longo de +Y, largura em X, espessura em Z.
static void model_sword(struct QuadSink *k, const struct Xform *x) {
    add_box(k, x, -4.5f, -6, -4, 4.5f, 0, 4, 214, 168, 52);       // pomo dourado
    add_box(k, x, -3, 0, -3, 3, 22, 3, 92, 60, 36);                // cabo enrolado
    add_box(k, x, -3.4f, 6, -3.4f, 3.4f, 9, 3.4f, 70, 44, 26);     // tira do cabo
    add_box(k, x, -3.4f, 14, -3.4f, 3.4f, 17, 3.4f, 70, 44, 26);
    add_box(k, x, -15, 22, -4.5f, 15, 28, 4.5f, 76, 104, 140);     // guarda azul-aco
    add_box(k, x, -16.5f, 21, -5, -12, 29, 5, 214, 168, 52);       // pontas douradas
    add_box(k, x, 12, 21, -5, 16.5f, 29, 5, 214, 168, 52);
    add_box(k, x, -5.5f, 28, -1.8f, 5.5f, 80, 1.8f, 120, 232, 236); // lamina cristal
    add_box(k, x, -1.6f, 30, -2.3f, 1.6f, 78, 2.3f, 56, 170, 186);  // nucleo
    add_box(k, x, 3.8f, 30, -2.0f, 5.6f, 78, 2.0f, 214, 252, 252);  // fio brilhante
    add_box(k, x, -4, 80, -1.6f, 4, 87, 1.6f, 120, 232, 236);       // ponta escalonada
    add_box(k, x, -2, 87, -1.4f, 2, 92, 1.4f, 160, 244, 246);
}

// Arco: membros ao longo de Y, curvados em +Z (para o arqueiro). pull = 0..1
static void model_bow(struct QuadSink *k, const struct Xform *x, f32 pull, s32 withArrow) {
    const s32 SEG = 5;
    f32 tipZ = 14.0f + pull * 7.0f;
    f32 prev[3] = { 0, -50, 0 };
    for (s32 i = -SEG; i <= SEG; i++) {
        f32 y = i * 10.0f;
        f32 tt = y / 50.0f;
        f32 z = (14.0f + pull * 7.0f) * tt * tt;
        f32 cur[3] = { 0, y, z };
        if (i > -SEG) {
            s32 grip = (i == 0 || i == 1);
            add_beam(k, x, prev, cur, grip ? 3.4f : 2.3f, grip ? 74 : 150, grip ? 46 : 104, grip ? 30 : 58);
        }
        memcpy(prev, cur, sizeof(prev));
    }
    // corda
    f32 top[3] = { 0, 50, tipZ }, bot[3] = { 0, -50, tipZ }, nock[3] = { 0, 0, tipZ + pull * 26.0f };
    add_beam(k, x, top, nock, 0.6f, 226, 226, 220);
    add_beam(k, x, nock, bot, 0.6f, 226, 226, 220);
    if (withArrow) {
        f32 tail[3] = { 0, 0, nock[2] }, tip[3] = { 0, 0, nock[2] - 64.0f }, headStart[3] = { 0, 0, nock[2] - 55.0f };
        f32 fl[3] = { 0, 0, nock[2] - 10.0f };
        add_beam(k, x, tail, headStart, 1.1f, 196, 160, 104);
        add_beam(k, x, headStart, tip, 2.6f, 150, 156, 164);
        add_beam(k, x, tail, fl, 2.8f, 236, 236, 236);
    }
}

// Flecha ao longo de +X (ponta em +X)
static void model_arrow(struct QuadSink *k, const struct Xform *x) {
    f32 tail[3] = { -32, 0, 0 }, hs[3] = { 22, 0, 0 }, tip[3] = { 32, 0, 0 }, fl[3] = { -22, 0, 0 };
    add_beam(k, x, tail, hs, 1.3f, 196, 160, 104);
    add_beam(k, x, hs, tip, 3.0f, 150, 156, 164);
    add_beam(k, x, tail, fl, 3.2f, 236, 236, 236);
}

/* ------------------------------------------------------------------------- */
/* Sprites extrudados (itens desenhados pixel a pixel a partir do PNG)       */
/* ------------------------------------------------------------------------- */
/*
 * O icone do item (o mesmo da hotbar) vira um modelo 3D: cada pixel opaco
 * vira um cubinho, com frente/verso e so as bordas expostas. Assim, um
 * texture pack instalado (tools/importar_texturepack.py) muda a espada e o
 * arco na mao automaticamente, igual ao sprite do pack.
 */

enum { SPR_ESPADA, SPR_ARCO, SPR_ARCO1, SPR_ARCO2, SPR_ARCO3, SPR_COUNT };
static const char *sSprPaths[SPR_COUNT] = {
    "gfx/textures/blocos/espada.rgba16.png",
    "gfx/textures/blocos/arco.rgba16.png",
    "gfx/textures/blocos/arco_1.rgba16.png",
    "gfx/textures/blocos/arco_2.rgba16.png",
    "gfx/textures/blocos/arco_3.rgba16.png",
};

#define SPR_MAX 32
#define SPR_MAX_QUADS 2400

struct SprQuad {
    f32 p[4][3];
    f32 n[3];
    u8 c[3];
};

struct SpriteMesh {
    u8 tried, ok;
    s32 count;
    struct SprQuad *q;
};
static struct SpriteMesh sSprites[SPR_COUNT];

static void spr_quad(struct SpriteMesh *m, f32 x0, f32 y0, f32 z0, f32 x1, f32 y1, f32 z1, s32 face, const u8 c[3]) {
    if (m->count >= SPR_MAX_QUADS) return;
    struct SprQuad *q = &m->q[m->count++];
    f32 lo[3] = { x0, y0, z0 }, hi[3] = { x1, y1, z1 };
    for (s32 k = 0; k < 4; k++) {
        for (s32 i = 0; i < 3; i++) {
            q->p[k][i] = sBoxFace[face][k][i] ? hi[i] : lo[i];
        }
    }
    memcpy(q->n, sBoxN[face], sizeof(q->n));
    memcpy(q->c, c, 3);
}

// monta a malha extrudada: o sprite ocupa 96 unidades de lado, centrado na origem
static struct SpriteMesh *sprite_mesh(s32 id) {
    struct SpriteMesh *m = &sSprites[id];
    if (m->tried) return m->ok ? m : NULL;
    m->tried = TRUE;
    uint64_t size = 0;
    u8 *file = fs_load_file(sSprPaths[id], &size);
    if (!file) return NULL;
    s32 w, h;
    u8 *img = stbi_load_from_memory(file, (int) size, &w, &h, NULL, 4);
    free(file);
    if (!img) return NULL;
    s32 n = w < h ? w : h;           // usa o quadrado do topo (packs animados)
    s32 step = (n + SPR_MAX - 1) / SPR_MAX;
    s32 g = n / step;                // resolucao da grade (<= 32)
    static u8 grid[SPR_MAX][SPR_MAX][4];
    for (s32 r = 0; r < g; r++) {
        for (s32 c = 0; c < g; c++) {
            u8 *p = &img[((r * step) * w + c * step) * 4];
            memcpy(grid[r][c], p, 4);
        }
    }
    stbi_image_free(img);
    m->q = malloc(sizeof(struct SprQuad) * SPR_MAX_QUADS);
    if (!m->q) return NULL;
    f32 ps = 96.0f / g, t = 3.0f;    // tamanho do pixel e meia espessura
#define OPAQ(rr, cc) ((rr) >= 0 && (rr) < g && (cc) >= 0 && (cc) < g && grid[rr][cc][3] >= 128)
    for (s32 r = 0; r < g; r++) {
        for (s32 c = 0; c < g; c++) {
            if (!OPAQ(r, c)) continue;
            f32 x0 = (c - g / 2.0f) * ps, x1 = x0 + ps;
            f32 y1 = (g / 2.0f - r) * ps, y0 = y1 - ps;
            const u8 *col = grid[r][c];
            spr_quad(m, x0, y0, -t, x1, y1, t, 4, col); // frente (+Z)
            spr_quad(m, x0, y0, -t, x1, y1, t, 5, col); // verso
            if (!OPAQ(r, c + 1)) spr_quad(m, x0, y0, -t, x1, y1, t, 0, col);
            if (!OPAQ(r, c - 1)) spr_quad(m, x0, y0, -t, x1, y1, t, 1, col);
            if (!OPAQ(r - 1, c)) spr_quad(m, x0, y0, -t, x1, y1, t, 2, col);
            if (!OPAQ(r + 1, c)) spr_quad(m, x0, y0, -t, x1, y1, t, 3, col);
        }
    }
#undef OPAQ
    m->ok = m->count > 0;
    return m->ok ? m : NULL;
}

static s32 add_sprite(struct QuadSink *k, const struct Xform *x, s32 id) {
    struct SpriteMesh *m = sprite_mesh(id);
    if (!m) return FALSE;
    for (s32 i = 0; i < m->count; i++) {
        Vec3f p[4], n;
        for (s32 c = 0; c < 4; c++) xf_point(x, m->q[i].p[c], p[c]);
        xf_dir(x, m->q[i].n, n);
        sink_quad(k, p, n, m->q[i].c);
    }
    return TRUE;
}

// base do sprite: a diagonal do sprite (+X+Y) vira 'diag' e a outra diagonal (-X+Y) vira 'perp'
static void sprite_basis(struct Xform *x, Vec3f diag, Vec3f perpHint) {
    Vec3f d, q, nrm, ax, ay;
    vec3f_copy(d, diag);
    vec3f_normalize(d);
    vec3f_cross(nrm, d, perpHint);
    vec3f_normalize(nrm);
    vec3f_cross(q, nrm, d);  // q perpendicular a d, no plano de perpHint
    for (s32 i = 0; i < 3; i++) {
        ax[i] = (d[i] - q[i]) * 0.70710678f;
        ay[i] = (d[i] + q[i]) * 0.70710678f;
    }
    for (s32 r = 0; r < 3; r++) {
        x->r[r][0] = ax[r];
        x->r[r][1] = ay[r];
        x->r[r][2] = nrm[r];
    }
}

/* ------------------------------------------------------------------------- */
/* Emissao de display lists                                                  */
/* ------------------------------------------------------------------------- */

static void set_vtx(Vtx *v, f32 x, f32 y, f32 z, s32 s, s32 t, const u8 c[3]) {
    v->v.ob[0] = (s16) lroundf(x);
    v->v.ob[1] = (s16) lroundf(y);
    v->v.ob[2] = (s16) lroundf(z);
    v->v.flag = 0;
    v->v.tc[0] = s;
    v->v.tc[1] = t;
    v->v.cn[0] = c[0];
    v->v.cn[1] = c[1];
    v->v.cn[2] = c[2];
    v->v.cn[3] = 255;
}

static Gfx *emit_quads(Gfx *g, Vtx **vp, struct Quad *q, s32 n, f32 posScale) {
    for (s32 i = 0; i < n; i += 8) {
        s32 batch = n - i > 8 ? 8 : n - i;
        Vtx *bv = *vp;
        for (s32 j = 0; j < batch; j++) {
            for (s32 c = 0; c < 4; c++) {
                Vec3f *p = &q[i + j].p[c];
                set_vtx((*vp)++, (*p)[0] * posScale, (*p)[1] * posScale, (*p)[2] * posScale, 0, 0, q[i + j].c);
            }
        }
        gSPVertex(g++, bv, batch * 4, 0);
        for (s32 j = 0; j < batch; j++) {
            s32 b = j * 4;
            gSP2Triangles(g++, b, b + 1, b + 2, 0, b, b + 2, b + 3, 0);
        }
    }
    return g;
}

static int cmp_depth(const void *a, const void *b) {
    f32 da = ((const struct Quad *) a)->depth, db = ((const struct Quad *) b)->depth;
    return (da > db) - (da < db); // mais longe (z mais negativo) primeiro
}

/* ------------------------------------------------------------------------- */
/* Estado dos itens                                                          */
/* ------------------------------------------------------------------------- */

#define SWING_FRAMES 9
#define MAX_ARROWS 48
#define ARROW_LIFE_STUCK (30 * 30)
#define ARROW_SPEED_MAX 200.0f    // 3 blocos por tique (20 tiques/s) -> por frame a 30 fps
#define ARROW_GRAVITY 2.22f       // 0,05 bloco/tique^2 convertido para frames de 30 fps
#define ARROW_DRAG_AIR 0.9933f    // 0,99 por tique
#define ARROW_DRAG_WATER 0.711f   // 0,6 por tique

struct Arrow {
    u8 active, stuck;
    Vec3f pos, vel;
    s32 life;
    s32 stuckBlock[3];
    u8 inBlock;
};

static struct Arrow sArrows[MAX_ARROWS];
static s32 sSwing = 0;        // frames restantes do golpe
static s32 sCharge = 0;       // frames puxando o arco
static s32 sAttackCooldown = 0;
static u32 sFrame = 0;

#define ENEMY_MASK (INTERACT_BOUNCE_TOP | INTERACT_BOUNCE_TOP2 | INTERACT_HIT_FROM_BELOW | INTERACT_KOOPA \
                    | INTERACT_BREAKABLE | INTERACT_BULLY | INTERACT_UNKNOWN_08)

void voxel_items_swing(void) {
    sSwing = SWING_FRAMES;
}

f32 voxel_items_charge(void) {
    f32 f = sCharge / 30.0f;
    f = (f * f + f * 2.0f) / 3.0f;
    return f > 1.0f ? 1.0f : f;
}

static s32 is_enemy(struct Object *o) {
    if (!(o->activeFlags & ACTIVE_FLAG_ACTIVE) || o == gMarioObject || o->oIntangibleTimer != 0) {
        return FALSE;
    }
    return (o->oInteractType & ENEMY_MASK) != 0;
}

static void hit_object(struct Object *o) {
    o->oInteractStatus = ATTACK_PUNCH | INT_STATUS_INTERACTED | INT_STATUS_WAS_ATTACKED;
    play_sound(SOUND_ACTION_HIT, gDefaultSoundArgs);
}

// procura um inimigo cujo hitbox contem o ponto (com folga)
static struct Object *enemy_at(Vec3f p, f32 pad) {
    for (s32 l = 0; l < NUM_OBJ_LISTS; l++) {
        struct ObjectNode *head = &gObjectLists[l];
        for (struct ObjectNode *nd = head->next; nd != head; nd = nd->next) {
            struct Object *o = (struct Object *) nd;
            if (!is_enemy(o)) {
                continue;
            }
            f32 r = (o->hitboxRadius > 0 ? o->hitboxRadius : 50.0f) + pad;
            f32 h = o->hitboxHeight > 0 ? o->hitboxHeight : 80.0f;
            f32 dx = p[0] - o->oPosX, dz = p[2] - o->oPosZ;
            f32 y0 = o->oPosY - o->hitboxDownOffset - pad;
            if (dx * dx + dz * dz <= r * r && p[1] >= y0 && p[1] <= y0 + h + pad * 2) {
                return o;
            }
        }
    }
    return NULL;
}

// ataque da espada: inimigos ate 'reach' dentro de um cone de ~60 graus
static s32 sword_attack(Vec3f orig, Vec3f dir) {
    s32 hits = 0;
    f32 hx = dir[0], hz = dir[2];
    f32 hl = sqrtf(hx * hx + hz * hz);
    if (hl > 0.001f) { hx /= hl; hz /= hl; }
    for (s32 l = 0; l < NUM_OBJ_LISTS && hits < 3; l++) {
        struct ObjectNode *head = &gObjectLists[l];
        for (struct ObjectNode *nd = head->next; nd != head && hits < 3; nd = nd->next) {
            struct Object *o = (struct Object *) nd;
            if (!is_enemy(o)) {
                continue;
            }
            f32 r = o->hitboxRadius > 0 ? o->hitboxRadius : 50.0f;
            f32 dx = o->oPosX - orig[0], dz = o->oPosZ - orig[2];
            f32 dist = sqrtf(dx * dx + dz * dz);
            f32 cy = o->oPosY - o->hitboxDownOffset + o->hitboxHeight * 0.5f;
            if (dist - r > 320.0f || fabsf(cy - orig[1]) > 190.0f) {
                continue;
            }
            if (dist > r) {
                f32 dot = (dx * hx + dz * hz) / dist;
                if (dot < 0.5f) {
                    continue;
                }
            }
            hit_object(o);
            hits++;
        }
    }
    return hits;
}

static void fire_arrow(Vec3f orig, Vec3f dir, f32 power) {
    s32 slot = -1, oldest = 0;
    for (s32 i = 0; i < MAX_ARROWS; i++) {
        if (!sArrows[i].active) { slot = i; break; }
        if (sArrows[i].life < sArrows[oldest].life) oldest = i;
    }
    if (slot < 0) slot = oldest;
    struct Arrow *a = &sArrows[slot];
    bzero(a, sizeof(*a));
    a->active = TRUE;
    vec3f_copy(a->pos, orig);
    // pequena imprecisao, como no arco classico
    f32 spread = 0.0075f;
    for (s32 i = 0; i < 3; i++) {
        f32 jitter = ((rand() % 2001) - 1000) / 1000.0f * spread;
        a->vel[i] = (dir[i] + jitter) * ARROW_SPEED_MAX * power;
    }
    a->life = 60 * 30; // some depois de 60 s voando sem acertar nada
    play_sound(SOUND_ACTION_THROW, gDefaultSoundArgs);
}

static void step_arrow(struct Arrow *a) {
    if (a->stuck) {
        // se o bloco onde cravou sumir, a flecha volta a cair
        if (a->inBlock && !voxel_is_solid(a->stuckBlock[0], a->stuckBlock[1], a->stuckBlock[2])) {
            a->stuck = FALSE;
            a->vel[0] = a->vel[1] = a->vel[2] = 0;
        } else {
            if (--a->life <= 0) a->active = FALSE;
            return;
        }
    }
    if (--a->life <= 0) {
        a->active = FALSE;
        return;
    }

    f32 water = find_water_level(a->pos[0], a->pos[2]);
    f32 drag = a->pos[1] < water ? ARROW_DRAG_WATER : ARROW_DRAG_AIR;
    a->vel[0] *= drag;
    a->vel[1] *= drag;
    a->vel[2] *= drag;
    a->vel[1] -= ARROW_GRAVITY;

    f32 len = vec3f_length(a->vel);
    if (len < 0.01f) {
        return;
    }
    Vec3f dir;
    vec3f_copy(dir, a->vel);
    vec3f_normalize(dir);

    // 1) inimigos (amostras ao longo do trajeto deste frame)
    s32 samples = (s32)(len / 20.0f) + 1;
    for (s32 i = 1; i <= samples; i++) {
        Vec3f p;
        f32 t = len * i / samples;
        p[0] = a->pos[0] + dir[0] * t;
        p[1] = a->pos[1] + dir[1] * t;
        p[2] = a->pos[2] + dir[2] * t;
        struct Object *o = enemy_at(p, 12.0f);
        if (o) {
            hit_object(o);
            a->active = FALSE; // flecha some ao acertar um inimigo
            return;
        }
    }

    // 2) blocos e terreno
    f32 tBlock = 1e9f, tTerrain = 1e9f;
    s32 cell[3];
    if (voxel_raycast(a->pos, dir, len, &tBlock, cell) == 0) {
        tBlock = 1e9f;
    }
    struct Surface *surf = NULL;
    Vec3f hit, ray = { a->vel[0], a->vel[1], a->vel[2] };
    find_surface_on_ray(a->pos, ray, &surf, hit);
    if (surf) {
        tTerrain = sqrtf((hit[0] - a->pos[0]) * (hit[0] - a->pos[0]) + (hit[1] - a->pos[1]) * (hit[1] - a->pos[1])
                         + (hit[2] - a->pos[2]) * (hit[2] - a->pos[2]));
    }
    f32 tHit = tBlock < tTerrain ? tBlock : tTerrain;
    if (tHit <= len) {
        // crava: a ponta entra um pouco
        f32 t = tHit + 6.0f - 32.0f;
        a->pos[0] += dir[0] * t;
        a->pos[1] += dir[1] * t;
        a->pos[2] += dir[2] * t;
        a->stuck = TRUE;
        a->inBlock = tBlock < tTerrain;
        memcpy(a->stuckBlock, cell, sizeof(cell));
        a->vel[0] = dir[0];
        a->vel[1] = dir[1];
        a->vel[2] = dir[2];
        a->life = ARROW_LIFE_STUCK;
        play_sound(SOUND_ACTION_HIT_3, gDefaultSoundArgs);
        return;
    }
    a->pos[0] += a->vel[0];
    a->pos[1] += a->vel[1];
    a->pos[2] += a->vel[2];
    if (a->pos[1] < -12000.0f) {
        a->active = FALSE;
    }
}

/* ------------------------------------------------------------------------- */
/* Flechas no mundo                                                          */
/* ------------------------------------------------------------------------- */

// Flecha no mundo: dois planos cruzados com a faixa lateral da textura
// "flecha" (32x32: u 0-16, v 0-5, cauda a esquerda e ponta a direita).
static Vtx sArrowVtx[MAX_ARROWS * 8];
static Gfx sArrowGfx[MAX_ARROWS * 4 + 32];
static u8 sArrowDlValid;

static void build_arrows_dl(void) {
    Gfx *g = sArrowGfx;
    Vtx *v = sArrowVtx;
    s32 n = 0;
    gDPPipeSync(g++);
    gSPClearGeometryMode(g++, G_LIGHTING | G_CULL_BACK);
    gSPSetGeometryMode(g++, G_SHADE);
    gDPSetCombineMode(g++, G_CC_MODULATERGBA, G_CC_MODULATERGBA);
    gDPSetTextureFilter(g++, G_TF_POINT);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPLoadTextureBlock(g++, "textures/blocos/flecha.rgba16", G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, 0,
                        G_TX_CLAMP | G_TX_NOMIRROR, G_TX_CLAMP | G_TX_NOMIRROR, 5, 5, G_TX_NOLOD, G_TX_NOLOD);
    for (s32 i = 0; i < MAX_ARROWS; i++) {
        struct Arrow *a = &sArrows[i];
        if (!a->active) continue;
        Vec3f d, up = { 0, 1, 0 }, side, up2;
        vec3f_copy(d, a->vel);
        if (vec3f_length(d) < 0.001f) { d[0] = 1; d[1] = 0; d[2] = 0; }
        vec3f_normalize(d);
        if (fabsf(d[1]) > 0.95f) { up[0] = 1; up[1] = 0; }
        vec3f_cross(side, d, up);
        vec3f_normalize(side);
        vec3f_cross(up2, side, d);
        const f32 L = 32.0f, H = 5.0f; // meia largura e meia altura (unidades)
        const u8 white[3] = { 255, 255, 255 };
        Vtx *bv = v;
        for (s32 plane = 0; plane < 2; plane++) {
            f32 *e = plane ? side : up2;
            for (s32 c = 0; c < 4; c++) {
                f32 sx = (c == 1 || c == 2) ? 1.0f : -1.0f;  // -1 cauda, +1 ponta
                f32 sy = (c >= 2) ? 1.0f : -1.0f;
                f32 px = a->pos[0] + d[0] * L * sx + e[0] * H * sy;
                f32 py = a->pos[1] + d[1] * L * sx + e[1] * H * sy;
                f32 pz = a->pos[2] + d[2] * L * sx + e[2] * H * sy;
                set_vtx(v++, px, py, pz, (sx > 0 ? 16 : 0) << 5, (sy > 0 ? 0 : 5) << 5, white);
            }
        }
        gSPVertex(g++, bv, 8, 0);
        gSP2Triangles(g++, 0, 1, 2, 0, 0, 2, 3, 0);
        gSP2Triangles(g++, 4, 5, 6, 0, 4, 6, 7, 0);
        n++;
    }
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetTextureFilter(g++, G_TF_BILERP);
    gDPPipeSync(g++);
    gSPSetGeometryMode(g++, G_LIGHTING | G_CULL_BACK);
    gSPEndDisplayList(g++);
    sArrowDlValid = n > 0;
}

Gfx *voxel_items_world_dl(void) {
    return sArrowDlValid ? sArrowGfx : NULL;
}

/* ------------------------------------------------------------------------- */
/* Atualizacao                                                               */
/* ------------------------------------------------------------------------- */

static void start_mario_swing_anim(struct MarioState *m) {
    if (m->heldObj != NULL) return;
    u32 group = m->action & ACT_GROUP_MASK;
    if (group == ACT_GROUP_STATIONARY && !(m->action & ACT_FLAG_ATTACKING) && (m->action & ACT_FLAG_STATIONARY)
        && m->action != ACT_CROUCHING) {
        set_mario_action(m, ACT_PUNCHING, 0);
    } else if (group == ACT_GROUP_MOVING && !(m->action & ACT_FLAG_ATTACKING)
               && (m->action == ACT_WALKING || m->action == ACT_BRAKING)) {
        set_mario_action(m, ACT_MOVE_PUNCHING, 0);
    }
}

// retorna TRUE se o clique esquerdo foi usado pela espada (nao quebra bloco)
s32 voxel_items_update(u8 held, u32 mb, u32 pressed, u32 released, s32 allowInput) {
    struct MarioState *m = gMarioState;
    s32 consumedLeft = FALSE;
    sFrame++;
    if (sSwing > 0) sSwing--;
    // teste: congela o golpe (BLOCOS_GOLPE=0..1) ou a carga do arco (BLOCOS_CARGA=frames)
    {
        static f32 fp = -2.0f, fc = -2.0f;
        if (fp < -1.5f) { const char *e = getenv("BLOCOS_GOLPE"); fp = e ? (f32) atof(e) : -1.0f; }
        if (fc < -1.5f) { const char *e = getenv("BLOCOS_CARGA"); fc = e ? (f32) atof(e) : -1.0f; }
        if (fp >= 0.0f) sSwing = (s32)(SWING_FRAMES * (1.0f - fp) + 0.5f);
        if (fc >= 0.0f && held == ITEM_ARCO) { sCharge = (s32) fc; mb |= 4; }
    }
    if (sAttackCooldown > 0) sAttackCooldown--;

    if (allowInput && held == ITEM_ESPADA && (pressed & 1)) {
        consumedLeft = TRUE;
        if (sAttackCooldown == 0) {
            Vec3f orig, dir;
            if (voxel_fp_active()) {
                voxel_fp_eye(orig);
                voxel_fp_look(dir);
            } else {
                orig[0] = m->pos[0];
                orig[1] = m->pos[1] + 80.0f;
                orig[2] = m->pos[2];
                dir[0] = sins(m->faceAngle[1]);
                dir[1] = 0;
                dir[2] = coss(m->faceAngle[1]);
            }
            s32 hits = sword_attack(orig, dir);
            start_mario_swing_anim(m);
            play_sound(SOUND_GENERAL_SWISH_AIR, gDefaultSoundArgs);
            sSwing = SWING_FRAMES;
            sAttackCooldown = 8;
            consumedLeft = hits > 0; // sem inimigo: o clique tambem quebra bloco
        }
    }

    if (held == ITEM_ARCO && allowInput) {
        if (mb & 4) { // botao direito (SDL_BUTTON_RMASK)
            sCharge++;
        } else if ((released & 4) && sCharge > 0) {
            f32 power = voxel_items_charge();
            if (power >= 0.1f) {
                Vec3f orig, dir;
                voxel_shot_ray(orig, dir);
                fire_arrow(orig, dir, power);
            }
            sCharge = 0;
        } else {
            sCharge = 0;
        }
    } else {
        sCharge = 0;
    }

    for (s32 i = 0; i < MAX_ARROWS; i++) {
        if (sArrows[i].active) {
            step_arrow(&sArrows[i]);
        }
    }
    build_arrows_dl();
    return consumedLeft;
}

void voxel_items_clear_arrows(void) {
    bzero(sArrows, sizeof(sArrows));
    sArrowDlValid = FALSE;
}

/* ------------------------------------------------------------------------- */
/* Item na mao (1a pessoa)                                                   */
/* ------------------------------------------------------------------------- */

#define VM_MAX_QUADS 2600
static struct Quad sVmQuads[VM_MAX_QUADS];
static Vtx sVmVtx[VM_MAX_QUADS * 4 + 64];
static Gfx sVmGfx[VM_MAX_QUADS / 8 * 9 + 128];

// Animacao de golpe: o braco "corta" para baixo e para a esquerda, girando em
// torno da mao (pivo). Entrada rapida e volta suave (curvas com raiz de p).
static f32 sEquip = 1.0f;     // 0 = item abaixado (trocando), 1 = na mao
static u8 sLastHeld = 0xFF;
static f32 sDraw = 0.0f;      // mistura suave entre pose normal e pose de puxar o arco

static void swing_matrix(M3 r, Vec3f tr, f32 amp) {
    m3_ident(r);
    tr[0] = tr[1] = tr[2] = 0.0f;
    if (sSwing <= 0 || amp <= 0.0f) return;
    f32 p = 1.0f - (f32) sSwing / SWING_FRAMES;
    f32 s1 = sinf(sqrtf(p) * 3.14159265f);   // sobe rapido, volta devagar
    f32 s2 = sinf(p * p * 3.14159265f);      // atrasado: torcao do pulso
    m3_prerot(r, 0, -0.80f * s1 * amp);      // ponta desce para frente
    m3_prerot(r, 2, 0.45f * s1 * amp);       // deita a lamina para dentro
    m3_prerot(r, 1, -0.50f * s2 * amp);      // torce o pulso no fim
    tr[0] = -40.0f * s1 * amp;
    tr[1] = 9.0f * sinf(sqrtf(p) * 2.0f * 3.14159265f) * amp;
    tr[2] = -16.0f * sinf(p * 3.14159265f) * amp;
}

// posiciona o item: 'grip' (ponto do item no espaco local) fica na mao 'anchor',
// e o golpe gira tudo em torno da mao
static void place_in_hand(struct Xform *x, const Vec3f anchor, const f32 grip[3], f32 amp, f32 bx, f32 by) {
    M3 rs;
    Vec3f tr, g, gr;
    swing_matrix(rs, tr, amp);
    xf_dir(x, grip, g);
    for (s32 i = 0; i < 3; i++) g[i] *= x->s;
    for (s32 i = 0; i < 3; i++) gr[i] = -(rs[i][0] * g[0] + rs[i][1] * g[1] + rs[i][2] * g[2]);
    m3_mul(x->r, rs, x->r);
    f32 drop = (1.0f - sEquip) * 70.0f;
    x->t[0] = anchor[0] + tr[0] + gr[0] + bx;
    x->t[1] = anchor[1] + tr[1] + gr[1] + by - drop;
    x->t[2] = anchor[2] + tr[2] + gr[2];
}

static void vm_textured_cube(Gfx **gp, Vtx **vp, u8 block, const struct Xform *x) {
    Gfx *g = *gp;
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(g++, G_CC_MODULATERGB, G_CC_MODULATERGB);
    gDPSetTextureFilter(g++, G_TF_POINT);
    Vec3f light = { -0.3f, 0.75f, 0.6f };
    vec3f_normalize(light);
    for (s32 f = 0; f < 6; f++) {
        Vec3f n;
        xf_dir(x, sBoxN[f], n);
        if (n[2] <= 0.0f) continue; // so as faces voltadas para a camera
        gDPLoadTextureBlock(g++, voxel_block_tex_name(block, f), G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0,
                            G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, 4, 4, G_TX_NOLOD, G_TX_NOLOD);
        f32 d = n[0] * light[0] + n[1] * light[1] + n[2] * light[2];
        u8 sh = (u8)(255 * (0.6f + 0.4f * (d > 0 ? d : 0)));
        u8 col[3] = { sh, sh, sh };
        Vtx *bv = *vp;
        Vec3f p[4];
        for (s32 c = 0; c < 4; c++) {
            f32 q[3];
            for (s32 i = 0; i < 3; i++) q[i] = sBoxFace[f][c][i] ? 0.5f : -0.5f;
            xf_point(x, q, p[c]);
            s32 su, tv;
            if (f == 2 || f == 3) { su = sBoxFace[f][c][0]; tv = sBoxFace[f][c][2]; }
            else { su = (f == 0 || f == 1) ? sBoxFace[f][c][2] : sBoxFace[f][c][0]; tv = 1 - sBoxFace[f][c][1]; }
            set_vtx((*vp)++, p[c][0], p[c][1], p[c][2], su * (16 << 5), tv * (16 << 5), col);
        }
        // ordem anti-horaria vista da camera
        Vec3f a, b, cr;
        vec3f_dif(a, p[1], p[0]);
        vec3f_dif(b, p[2], p[0]);
        vec3f_cross(cr, a, b);
        gSPVertex(g++, bv, 4, 0);
        if (cr[0] * n[0] + cr[1] * n[1] + cr[2] * n[2] >= 0) {
            gSP2Triangles(g++, 0, 1, 2, 0, 0, 2, 3, 0);
        } else {
            gSP2Triangles(g++, 0, 2, 1, 0, 0, 3, 2, 0);
        }
    }
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetTextureFilter(g++, G_TF_BILERP);
    *gp = g;
}

// chamado no passe do HUD; desenha no espaco da camera
void voxel_items_render_viewmodel(u8 held) {
    if (!voxel_fp_active()) {
        return;
    }
    // troca de item: abaixa e sobe de novo
    if (held != sLastHeld) {
        if (sLastHeld != 0xFF) sEquip = 0.0f;
        sLastHeld = held;
    }
    sEquip = fminf(1.0f, sEquip + 0.2f);
    sDraw += ((sCharge > 0 && held == ITEM_ARCO ? 1.0f : 0.0f) - sDraw) * 0.35f;

    Mtx *proj = alloc_display_list(sizeof(Mtx));
    Mtx *ident = alloc_display_list(sizeof(Mtx));
    if (proj == NULL || ident == NULL) {
        return;
    }
    u16 perspNorm;
    guPerspective(proj, &perspNorm, 70.0f, 4.0f / 3.0f, 8.0f, 1000.0f, 1.0f);
    guMtxIdent(ident);

    Gfx *g = gDisplayListHead;
    gDPPipeSync(g++);
    gSPPerspNormalize(g++, perspNorm);
    gSPMatrix(g++, VIRTUAL_TO_PHYSICAL(proj), G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPMatrix(g++, VIRTUAL_TO_PHYSICAL(ident), G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_NOPUSH);
    gSPClearGeometryMode(g++, G_LIGHTING | G_ZBUFFER);
    gSPSetGeometryMode(g++, G_SHADE | G_CULL_BACK);
    gDPSetRenderMode(g++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);

    Vtx *v = sVmVtx;
    struct QuadSink k = { sVmQuads, 0, VM_MAX_QUADS, { -0.3f, 0.75f, 0.6f } };
    vec3f_normalize(k.light);
    f32 bx, by;
    voxel_fp_bob(&bx, &by);

    struct Xform x;
    if (held == ITEM_ESPADA) {
        // sprite: cabo no canto inferior esquerdo, ponta no superior direito.
        // Na mao: ponta para cima, para frente e um pouco para dentro da tela.
        Vec3f diag = { -0.38f, 0.82f, -0.42f }, perp = { 0.80f, 0.30f, 0.30f };
        Vec3f anchor = { 44.0f, -44.0f, -62.0f };
        const f32 grip[3] = { -30.0f, -30.0f, 0.0f };
        sprite_basis(&x, diag, perp);
        x.s = 0.56f;
        place_in_hand(&x, anchor, grip, 1.0f, bx, by);
        if (!add_sprite(&k, &x, SPR_ESPADA)) {
            m3_ident(x.r);
            m3_prerot(x.r, 1, 0.35f);
            m3_prerot(x.r, 2, 0.55f);
            m3_prerot(x.r, 0, -0.55f);
            x.s = 0.62f;
            const f32 grip2[3] = { 0.0f, 10.0f, 0.0f };
            place_in_hand(&x, anchor, grip2, 1.0f, bx, by);
            model_sword(&k, &x);
        }
    } else if (held == ITEM_ARCO) {
        f32 c = voxel_items_charge();
        f32 d = sDraw;
        s32 spr = sCharge <= 0 ? SPR_ARCO : (c >= 0.9f ? SPR_ARCO3 : (c >= 0.65f ? SPR_ARCO2 : SPR_ARCO1));
        if (!sprite_mesh(spr)) spr = SPR_ARCO;
        // pose normal (de lado, a direita) -> pose de puxar (centro, inclinado, perto da camera)
        Vec3f diag, perp, anchor;
        // perp = para onde a curva do arco aponta: para a esquerda (rosto do sprite visivel)
        // e, ao puxar, mais para dentro da tela (a flecha aponta para a mira)
        const f32 dI[3] = { -0.30f, 1.0f, 0.15f }, dD[3] = { -0.12f, 1.0f, 0.25f };
        const f32 pI[3] = { -1.0f, 0.0f, -0.30f }, pD[3] = { -0.75f, 0.05f, -1.0f };
        const f32 aI[3] = { 34.0f, -32.0f, -72.0f }, aD[3] = { 6.0f, -16.0f, -64.0f };
        for (s32 i = 0; i < 3; i++) {
            diag[i] = dI[i] + (dD[i] - dI[i]) * d;
            perp[i] = pI[i] + (pD[i] - pI[i]) * d;
            anchor[i] = aI[i] + (aD[i] - aI[i]) * d;
        }
        anchor[2] += 10.0f * c * d;  // recua para perto do rosto conforme puxa
        if (sCharge > 0 && c > 0.1f) { // treme cada vez mais conforme a carga
            anchor[1] += sinf(sFrame * 1.35f) * (c - 0.1f) * 1.6f;
            anchor[0] += cosf(sFrame * 1.1f) * (c - 0.1f) * 0.8f;
        }
        sprite_basis(&x, diag, perp);
        x.s = 0.56f;
        const f32 grip[3] = { 0.0f, 0.0f, 0.0f };
        place_in_hand(&x, anchor, grip, 0.4f, bx * (1.0f - d), by * (1.0f - d));
        if (!add_sprite(&k, &x, spr)) {
            model_bow(&k, &x, c, sCharge > 0);
        }
    } else if (held > BLOCO_AR && held < BLOCO_COUNT) {
        m3_ident(x.r);
        m3_prerot(x.r, 1, 0.78f);
        m3_prerot(x.r, 0, 0.30f);
        x.s = 24.0f;
        Vec3f anchor = { 44.0f, -38.0f, -66.0f };
        const f32 grip[3] = { 0.0f, 0.0f, 0.0f };
        place_in_hand(&x, anchor, grip, 0.75f, bx, by);
        vm_textured_cube(&g, &v, held, &x);
    }

    if (k.n > 0) {
        qsort(sVmQuads, k.n, sizeof(struct Quad), cmp_depth);
        g = emit_quads(g, &v, sVmQuads, k.n, 1.0f);
    }

    gDPPipeSync(g++);
    gSPSetGeometryMode(g++, G_LIGHTING);
    gDisplayListHead = g;
}

/* ------------------------------------------------------------------------- */
/* Item na mao (3a pessoa) - espaco local do osso da mao                     */
/* ------------------------------------------------------------------------- */

static struct Quad sHandQuads[2][VM_MAX_QUADS];
static Vtx sHandVtx[2][VM_MAX_QUADS * 4];
static Gfx sHandGfx[2][VM_MAX_QUADS / 8 * 9 + 16];
static u8 sHandInit;

static void build_hand_item(s32 idx, u8 item) {
    struct QuadSink k = { sHandQuads[idx], 0, VM_MAX_QUADS, { 0.3f, 0.8f, 0.5f } };
    vec3f_normalize(k.light);
    struct Xform x;
    // Direcoes medidas no osso da mao direita (pose parada), em coordenadas locais:
    // "frente do personagem" ~ (0.91,-0.19,-0.37) e "para cima" ~ (-0.41,-0.43,-0.80).
    // O modelo do personagem e desenhado com escala 0,25 (unidades locais = 4x o mundo).
    Vec3f fwd = { 0.91f, -0.19f, -0.37f }, up = { -0.41f, -0.43f, -0.80f };
    Vec3f ix, iy, iz;
    if (item == ITEM_ESPADA) {
        // lamina para frente e um pouco para cima
        iy[0] = fwd[0] + up[0] * 0.6f; iy[1] = fwd[1] + up[1] * 0.6f; iy[2] = fwd[2] + up[2] * 0.6f;
        vec3f_normalize(iy);
        vec3f_cross(ix, iy, up);
        vec3f_normalize(ix);
        vec3f_cross(iz, ix, iy);
        x.s = 3.9f;  // ~90 unidades de mundo
    } else {
        // arco em pe, corda voltada para o corpo
        vec3f_copy(iy, up);
        iz[0] = -fwd[0]; iz[1] = -fwd[1]; iz[2] = -fwd[2];
        vec3f_cross(ix, iy, iz);
        vec3f_normalize(ix);
        vec3f_cross(iz, ix, iy);
        x.s = 3.2f;  // ~80 unidades de mundo
    }
    for (s32 r = 0; r < 3; r++) { x.r[r][0] = ix[r]; x.r[r][1] = iy[r]; x.r[r][2] = iz[r]; }
    x.t[0] = 22.0f; x.t[1] = 0.0f; x.t[2] = 0.0f; // centro do punho
    s32 usedSprite = FALSE;
    {
        struct Xform xs;
        Vec3f diag, perp;
        if (item == ITEM_ESPADA) {
            // diagonal do sprite (cabo -> ponta) para frente e para cima
            for (s32 i = 0; i < 3; i++) { diag[i] = fwd[i] + up[i] * 0.6f; perp[i] = up[i]; }
        } else {
            // corda (diagonal) em pe; a curva do arco aponta para frente
            for (s32 i = 0; i < 3; i++) { diag[i] = up[i]; perp[i] = fwd[i]; }
        }
        sprite_basis(&xs, diag, perp);
        xs.s = item == ITEM_ESPADA ? 4.2f : 3.6f;
        xs.t[0] = 22.0f; xs.t[1] = 0.0f; xs.t[2] = 0.0f;
        if (item == ITEM_ESPADA) {
            // o cabo do sprite (canto inferior esquerdo) fica no punho
            Vec3f off, local = { -30.0f, -30.0f, 0.0f };
            xf_dir(&xs, local, off);
            for (s32 i = 0; i < 3; i++) xs.t[i] -= off[i] * xs.s;
        }
        usedSprite = add_sprite(&k, &xs, item == ITEM_ESPADA ? SPR_ESPADA : SPR_ARCO);
    }
    if (!usedSprite && item == ITEM_ESPADA) {
        x.t[0] -= iy[0] * 11.0f * x.s; x.t[1] -= iy[1] * 11.0f * x.s; x.t[2] -= iy[2] * 11.0f * x.s;
        model_sword(&k, &x);
    } else if (!usedSprite) {
        model_bow(&k, &x, 0.0f, FALSE);
    }
    Gfx *g = sHandGfx[idx];
    Vtx *v = sHandVtx[idx];
    gDPPipeSync(g++);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    gSPClearGeometryMode(g++, G_LIGHTING);
    gSPSetGeometryMode(g++, G_SHADE | G_CULL_BACK);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    g = emit_quads(g, &v, k.q, k.n, 1.0f);
    gDPPipeSync(g++);
    gSPSetGeometryMode(g++, G_LIGHTING);
    gSPEndDisplayList(g++);
}

const Gfx *voxel_items_hand_dl(u8 item) {
    if (!sHandInit) {
        build_hand_item(0, ITEM_ESPADA);
        build_hand_item(1, ITEM_ARCO);
        sHandInit = TRUE;
    }
    if (item == ITEM_ESPADA) return sHandGfx[0];
    if (item == ITEM_ARCO) return sHandGfx[1];
    return NULL;
}

s32 voxel_items_arrow_count(s32 *flying, s32 *stuck) {
    *flying = *stuck = 0;
    for (s32 i = 0; i < MAX_ARROWS; i++) {
        if (!sArrows[i].active) continue;
        if (sArrows[i].stuck) (*stuck)++; else (*flying)++;
    }
    return *flying + *stuck;
}
