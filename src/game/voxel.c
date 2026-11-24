/*
 * Mod "Blocos" - camada de blocos sobre as fases do Super Mario 64 (sm64ex).
 *
 *  - Mundo de blocos esparso (tabela hash) separado por fase/area
 *  - Colisao real: blocos viram chao, parede e teto para Mario e objetos
 *  - Quebrar (botao esquerdo) / colocar (botao direito) / pegar (meio)
 *  - Hotbar de 9 slots (1-9 ou roda do mouse)
 *  - Camera em 1a pessoa (F5) com mouse
 *  - Salva os blocos em blocos.dat na pasta de saves
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "sm64.h"
#include "voxel.h"
#include "area.h"
#include "camera.h"
#include "game_init.h"
#include "ingame_menu.h"
#include "level_update.h"
#include "mario.h"
#include "memory.h"
#include "object_list_processor.h"
#include "print.h"
#include "save_file.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "audio/external.h"
#include "pc/platform.h"
#include "pc/fs/fs.h"

#ifndef EXTERNAL_DATA
#error "O mod Blocos precisa de EXTERNAL_DATA=1 (texturas externas)"
#endif

#define S VOXEL_SIZE
#define VOX_TABLE_BITS 17
#define VOX_TABLE_SIZE (1 << VOX_TABLE_BITS)
#define VOX_TABLE_MASK (VOX_TABLE_SIZE - 1)
#define VOX_MAX_BLOCKS 60000
#define VOX_MAX_FACES 65536
#define VOX_SURF_RING 2048
#define VOX_REACH 520.0f
#define VOX_NO_FLOOR -11000.0f
#define VOX_NO_CEIL 20000.0f

/* ------------------------------------------------------------------------- */
/* Estado                                                                    */
/* ------------------------------------------------------------------------- */

struct VoxCell {
    u64 key;
    u8 type; // 0 = vazio
};

static struct VoxCell sTable[VOX_TABLE_SIZE];
static s32 sBlockCount;      // total (todas as fases)
static s32 sAreaCount;       // blocos na fase/area atual
static s32 sMinY, sMaxY;     // limites verticais na area atual
static u64 sAreaPrefix = ~0ULL;
static u8 sMeshDirty = TRUE;
static u8 sLoaded = FALSE;
static s32 sSaveTimer = -1;

u8 gVoxelFirstPerson = FALSE;
static u8 sFpApplied = FALSE; // 1a pessoa realmente aplicada neste frame
u8 gVoxelHideMario = FALSE;
static s16 sFpYaw, sFpPitch;

u32 gVoxelMouseButtons = 0;
s32 gVoxelMouseWheel = 0;
extern int mouse_x, mouse_y;

static s32 sSlot = 0;
static s32 sSlotLabelTimer = 0;
static const u8 sHotbar[9] = {
    ITEM_ESPADA, ITEM_ARCO, BLOCO_GRAMA, BLOCO_TERRA, BLOCO_PEDRA,
    BLOCO_PEDREGULHO, BLOCO_TABUAS, BLOCO_TRONCO, BLOCO_TIJOLO,
};
static u8 sHotbarSel[9];

static const char *sBlockNames[BLOCO_COUNT] = {
    "AR", "GRAMA", "TERRA", "PEDRA", "PEDREGULHO", "TABUAS", "TRONCO", "TIJOLO", "AREIA", "FOLHAS",
};

// alvo do raio
static u8 sHasTarget;      // mirando um bloco existente
static u8 sHasPlace;       // ha uma celula valida para colocar
static s32 sTarget[3], sPlace[3];
static s32 sBreakCooldown, sPlaceCooldown;
static u32 sPrevMouse;
static u8 sPrevF5, sPrevF6;
static u8 sPrevNum[9];

// teste automatizado (variavel de ambiente BLOCOS_TESTE)
static s32 sTestMode = -1;
static s32 sTestFrame = 0;

/* ------------------------------------------------------------------------- */
/* Texturas                                                                  */
/* ------------------------------------------------------------------------- */

enum {
    TEX_GRAMA_TOPO, TEX_GRAMA_LADO, TEX_TERRA, TEX_PEDRA, TEX_PEDREGULHO, TEX_TABUAS,
    TEX_TRONCO_LADO, TEX_TRONCO_TOPO, TEX_TIJOLO, TEX_AREIA, TEX_FOLHAS, TEX_ESPADA, TEX_ARCO, TEX_COUNT
};

static const char *sTexNames[TEX_COUNT] = {
    "textures/blocos/grama_topo.rgba16",
    "textures/blocos/grama_lado.rgba16",
    "textures/blocos/terra.rgba16",
    "textures/blocos/pedra.rgba16",
    "textures/blocos/pedregulho.rgba16",
    "textures/blocos/tabuas.rgba16",
    "textures/blocos/tronco_lado.rgba16",
    "textures/blocos/tronco_topo.rgba16",
    "textures/blocos/tijolo.rgba16",
    "textures/blocos/areia.rgba16",
    "textures/blocos/folhas.rgba16",
    "textures/blocos/espada.rgba16",
    "textures/blocos/arco.rgba16",
};

enum { FACE_PX, FACE_NX, FACE_PY, FACE_NY, FACE_PZ, FACE_NZ };
static const s8 sFaceDir[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
static const u8 sFaceShade[6] = { 204, 204, 255, 140, 170, 170 };

static s32 block_tex(u8 type, s32 face) {
    switch (type) {
        case ITEM_ESPADA: return TEX_ESPADA;
        case ITEM_ARCO: return TEX_ARCO;
        case BLOCO_GRAMA:
            return face == FACE_PY ? TEX_GRAMA_TOPO : (face == FACE_NY ? TEX_TERRA : TEX_GRAMA_LADO);
        case BLOCO_TERRA: return TEX_TERRA;
        case BLOCO_PEDRA: return TEX_PEDRA;
        case BLOCO_PEDREGULHO: return TEX_PEDREGULHO;
        case BLOCO_TABUAS: return TEX_TABUAS;
        case BLOCO_TRONCO: return (face == FACE_PY || face == FACE_NY) ? TEX_TRONCO_TOPO : TEX_TRONCO_LADO;
        case BLOCO_TIJOLO: return TEX_TIJOLO;
        case BLOCO_AREIA: return TEX_AREIA;
        case BLOCO_FOLHAS: return TEX_FOLHAS;
    }
    return TEX_TERRA;
}

/* ------------------------------------------------------------------------- */
/* Tabela hash de blocos                                                     */
/* ------------------------------------------------------------------------- */

static inline u64 area_prefix(void) {
    return ((u64)(u8) gCurrLevelNum << 56) | ((u64)(gCurrAreaIndex & 0xF) << 52);
}

static inline u64 vox_key(s32 x, s32 y, s32 z) {
    return area_prefix() | ((u64)((x + 0x20000) & 0x3FFFF) << 34) | ((u64)((z + 0x20000) & 0x3FFFF) << 16)
           | (u64)((y + 0x8000) & 0xFFFF);
}

static inline void key_coords(u64 k, s32 *x, s32 *y, s32 *z) {
    *x = (s32)((k >> 34) & 0x3FFFF) - 0x20000;
    *z = (s32)((k >> 16) & 0x3FFFF) - 0x20000;
    *y = (s32)(k & 0xFFFF) - 0x8000;
}

static inline u32 vox_hash(u64 k) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdULL;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ULL;
    k ^= k >> 33;
    return (u32) k & VOX_TABLE_MASK;
}

static inline u8 table_get(u64 k) {
    u32 i = vox_hash(k);
    while (sTable[i].type) {
        if (sTable[i].key == k) {
            return sTable[i].type;
        }
        i = (i + 1) & VOX_TABLE_MASK;
    }
    return 0;
}

static inline u8 vox_get(s32 x, s32 y, s32 z) {
    if (sAreaCount == 0 || y < sMinY || y > sMaxY) {
        return 0;
    }
    return table_get(vox_key(x, y, z));
}

static s32 table_set(u64 k, u8 type) {
    u32 i = vox_hash(k);
    while (sTable[i].type) {
        if (sTable[i].key == k) {
            sTable[i].type = type;
            return 0;
        }
        i = (i + 1) & VOX_TABLE_MASK;
    }
    if (sBlockCount >= VOX_MAX_BLOCKS) {
        return -1;
    }
    sTable[i].key = k;
    sTable[i].type = type;
    sBlockCount++;
    return 1;
}

static s32 table_remove(u64 k) {
    u32 i = vox_hash(k);
    while (sTable[i].type) {
        if (sTable[i].key == k) {
            break;
        }
        i = (i + 1) & VOX_TABLE_MASK;
    }
    if (!sTable[i].type) {
        return 0;
    }
    // remocao com deslocamento para tras (sondagem linear sem lapides)
    u32 j = i;
    for (;;) {
        j = (j + 1) & VOX_TABLE_MASK;
        if (!sTable[j].type) {
            break;
        }
        u32 h = vox_hash(sTable[j].key);
        u32 distJ = (j - h) & VOX_TABLE_MASK;
        u32 distI = (j - i) & VOX_TABLE_MASK;
        if (distJ >= distI) {
            sTable[i] = sTable[j];
            i = j;
        }
    }
    sTable[i].type = 0;
    sTable[i].key = 0;
    sBlockCount--;
    return 1;
}

static void recount_area(void) {
    u64 prefix = area_prefix();
    sAreaCount = 0;
    sMinY = 0x7FFF;
    sMaxY = -0x7FFF;
    for (u32 i = 0; i < VOX_TABLE_SIZE; i++) {
        if (sTable[i].type && (sTable[i].key & 0xFFF0000000000000ULL) == prefix) {
            s32 x, y, z;
            key_coords(sTable[i].key, &x, &y, &z);
            sAreaCount++;
            if (y < sMinY) sMinY = y;
            if (y > sMaxY) sMaxY = y;
        }
    }
    sAreaPrefix = prefix;
    sMeshDirty = TRUE;
    voxel_items_clear_arrows();
}

static void schedule_save(void) {
    sSaveTimer = 90; // salva ~3s depois da ultima mudanca
}

static s32 vox_set(s32 x, s32 y, s32 z, u8 type) {
    s32 r = table_set(vox_key(x, y, z), type);
    if (r < 0) {
        return FALSE;
    }
    if (r > 0) {
        sAreaCount++;
    }
    if (y < sMinY) sMinY = y;
    if (y > sMaxY) sMaxY = y;
    sMeshDirty = TRUE;
    schedule_save();
    return TRUE;
}

static s32 vox_remove(s32 x, s32 y, s32 z) {
    if (table_remove(vox_key(x, y, z))) {
        sAreaCount--;
        sMeshDirty = TRUE;
        schedule_save();
        return TRUE;
    }
    return FALSE;
}

s32 voxel_any_blocks(void) {
    return sAreaCount > 0;
}

/* ------------------------------------------------------------------------- */
/* Salvar / carregar                                                         */
/* ------------------------------------------------------------------------- */

#define BLOCOS_MAGIC 0x534F434CU // "LCOS"

static void blocos_path(char *out, size_t len) {
    // fs_get_write_path respeita --savepath (sys_user_path nao)
    snprintf(out, len, "%s", fs_get_write_path("blocos.dat"));
}

static void voxel_save(void) {
    char path[SYS_MAX_PATH];
    blocos_path(path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) {
        return;
    }
    u32 hdr[2] = { BLOCOS_MAGIC, (u32) sBlockCount };
    fwrite(hdr, sizeof(hdr), 1, f);
    for (u32 i = 0; i < VOX_TABLE_SIZE; i++) {
        if (sTable[i].type) {
            fwrite(&sTable[i].key, sizeof(u64), 1, f);
            fwrite(&sTable[i].type, 1, 1, f);
        }
    }
    fclose(f);
}

static void voxel_load(void) {
    char path[SYS_MAX_PATH];
    blocos_path(path, sizeof(path));
    sLoaded = TRUE;
    FILE *f = fopen(path, "rb");
    if (!f) {
        return;
    }
    u32 hdr[2];
    if (fread(hdr, sizeof(hdr), 1, f) == 1 && hdr[0] == BLOCOS_MAGIC) {
        for (u32 n = 0; n < hdr[1] && n < VOX_MAX_BLOCKS; n++) {
            u64 k;
            u8 t;
            if (fread(&k, sizeof(u64), 1, f) != 1 || fread(&t, 1, 1, f) != 1) {
                break;
            }
            if (t > BLOCO_AR && t < BLOCO_COUNT) {
                table_set(k, t);
            }
        }
    }
    fclose(f);
    sAreaPrefix = ~0ULL;
}

/* ------------------------------------------------------------------------- */
/* Colisao                                                                   */
/* ------------------------------------------------------------------------- */

static struct Surface sSurfRing[VOX_SURF_RING];
static u32 sSurfIdx;

static struct Surface *new_surf(void) {
    struct Surface *s = &sSurfRing[sSurfIdx++ & (VOX_SURF_RING - 1)];
    bzero(s, sizeof(*s));
    s->type = SURFACE_DEFAULT;
    return s;
}

static inline s32 to_block(f32 v) {
    return (s32) floorf(v / (f32) S);
}

static inline s32 voxel_collision_on(void) {
    // camera nao colide com blocos (evita tremer); objetos e Mario sim
    if (sAreaCount == 0 || gCheckingSurfaceCollisionsForCamera) {
        return FALSE;
    }
    return sAreaPrefix == area_prefix();
}

f32 voxel_find_floor(f32 x, f32 y, f32 z, struct Surface **pfloor) {
    *pfloor = NULL;
    if (!voxel_collision_on()) {
        return VOX_NO_FLOOR;
    }
    s32 bx = to_block(x), bz = to_block(z);
    // chao aceito se altura <= y + 78 (mesma folga do jogo original)
    s32 by = (s32) floorf((y + 78.0f) / (f32) S) - 1;
    if (by > sMaxY) {
        by = sMaxY;
    }
    for (; by >= sMinY; by--) {
        if (vox_get(bx, by, bz)) {
            f32 h = (f32)((by + 1) * S);
            struct Surface *s = new_surf();
            s16 x0 = bx * S, x1 = x0 + S, z0 = bz * S, z1 = z0 + S, hh = (s16) h;
            s->normal.y = 1.0f;
            s->originOffset = -h;
            s->lowerY = hh - 5;
            s->upperY = hh + 5;
            s->vertex1[0] = x0; s->vertex1[1] = hh; s->vertex1[2] = z0;
            s->vertex2[0] = x0; s->vertex2[1] = hh; s->vertex2[2] = z1;
            s->vertex3[0] = x1; s->vertex3[1] = hh; s->vertex3[2] = z1;
            *pfloor = s;
            return h;
        }
    }
    return VOX_NO_FLOOR;
}

f32 voxel_find_ceil(f32 x, f32 y, f32 z, struct Surface **pceil) {
    *pceil = NULL;
    if (!voxel_collision_on()) {
        return VOX_NO_CEIL;
    }
    s32 bx = to_block(x), bz = to_block(z);
    // teto aceito se altura >= y - 78 (mesma folga do jogo original)
    s32 by = (s32) ceilf((y - 78.0f) / (f32) S);
    if (by < sMinY) {
        by = sMinY;
    }
    for (; by <= sMaxY; by++) {
        if (vox_get(bx, by, bz)) {
            f32 h = (f32)(by * S);
            struct Surface *s = new_surf();
            s16 x0 = bx * S, x1 = x0 + S, z0 = bz * S, z1 = z0 + S, hh = (s16) h;
            s->normal.y = -1.0f;
            s->originOffset = h;
            s->lowerY = hh - 5;
            s->upperY = hh + 5;
            s->vertex1[0] = x0; s->vertex1[1] = hh; s->vertex1[2] = z0;
            s->vertex2[0] = x1; s->vertex2[1] = hh; s->vertex2[2] = z1;
            s->vertex3[0] = x0; s->vertex3[1] = hh; s->vertex3[2] = z1;
            *pceil = s;
            return h;
        }
    }
    return VOX_NO_CEIL;
}

static struct Surface *make_wall(f32 nx, f32 nz, f32 cx, f32 cz, s32 by) {
    struct Surface *s = new_surf();
    s->normal.x = nx;
    s->normal.z = nz;
    s->originOffset = -(nx * cx + nz * cz);
    s->lowerY = by * S;
    s->upperY = (by + 1) * S;
    if (fabsf(nx) > fabsf(nz)) {
        s->flags = SURFACE_FLAG_X_PROJECTION;
    }
    // vertices aproximados da face (usados so para referencia)
    s->vertex1[0] = (s16) cx; s->vertex1[1] = s->lowerY; s->vertex1[2] = (s16) cz;
    s->vertex2[0] = (s16) cx; s->vertex2[1] = s->upperY; s->vertex2[2] = (s16) cz;
    s->vertex3[0] = (s16)(cx - nz * S); s->vertex3[1] = s->upperY; s->vertex3[2] = (s16)(cz + nx * S);
    return s;
}

s32 voxel_find_walls(struct WallCollisionData *d) {
    if (!voxel_collision_on()) {
        return 0;
    }
    f32 r = d->radius;
    if (r > 200.0f) {
        r = 200.0f;
    }
    if (r <= 0.0f) {
        return 0;
    }
    f32 yy = d->y + d->offsetY;
    s32 by = to_block(yy);
    if (by < sMinY || by > sMaxY) {
        return 0;
    }
    s32 hits = 0;
    s32 bx0 = to_block(d->x - r), bx1 = to_block(d->x + r);
    s32 bz0 = to_block(d->z - r), bz1 = to_block(d->z + r);

    for (s32 bx = bx0; bx <= bx1; bx++) {
        for (s32 bz = bz0; bz <= bz1; bz++) {
            if (!vox_get(bx, by, bz)) {
                continue;
            }
            f32 x0 = bx * S, x1 = x0 + S, z0 = bz * S, z1 = z0 + S;
            f32 px = d->x, pz = d->z;
            f32 cx = px < x0 ? x0 : (px > x1 ? x1 : px);
            f32 cz = pz < z0 ? z0 : (pz > z1 ? z1 : pz);
            f32 dx = px - cx, dz = pz - cz;
            // ignora separacao na direcao de um vizinho solido (costura interna)
            if (dx > 0 && vox_get(bx + 1, by, bz)) dx = 0;
            else if (dx < 0 && vox_get(bx - 1, by, bz)) dx = 0;
            if (dz > 0 && vox_get(bx, by, bz + 1)) dz = 0;
            else if (dz < 0 && vox_get(bx, by, bz - 1)) dz = 0;

            f32 nx, nz, push;
            if (dx == 0 && dz == 0) {
                s32 inX = px > x0 && px < x1;
                s32 inZ = pz > z0 && pz < z1;
                if (!(inX && inZ)) {
                    continue; // canto interno: o bloco vizinho resolve
                }
                // centro dentro do bloco: empurra pela face exposta mais proxima
                f32 best = 1e9f;
                nx = 0; nz = 0;
                if (!vox_get(bx + 1, by, bz) && x1 - px < best) { best = x1 - px; nx = 1; nz = 0; }
                if (!vox_get(bx - 1, by, bz) && px - x0 < best) { best = px - x0; nx = -1; nz = 0; }
                if (!vox_get(bx, by, bz + 1) && z1 - pz < best) { best = z1 - pz; nx = 0; nz = 1; }
                if (!vox_get(bx, by, bz - 1) && pz - z0 < best) { best = pz - z0; nx = 0; nz = -1; }
                if (best > 1e8f) {
                    continue; // totalmente enterrado
                }
                push = best + r;
                cx = nx > 0 ? x1 : (nx < 0 ? x0 : px);
                cz = nz > 0 ? z1 : (nz < 0 ? z0 : pz);
            } else {
                f32 dist = sqrtf(dx * dx + dz * dz);
                if (dist >= r) {
                    continue;
                }
                nx = dx / dist;
                nz = dz / dist;
                push = r - dist;
            }
            d->x += nx * push;
            d->z += nz * push;
            if (d->numWalls < 4) {
                d->walls[d->numWalls++] = make_wall(nx, nz, cx, cz, by);
            }
            hits++;
        }
    }
    return hits;
}

/* ------------------------------------------------------------------------- */
/* Malha do mundo                                                            */
/* ------------------------------------------------------------------------- */

struct FaceRec {
    s32 x, y, z;
    u8 face, tex;
};

static struct FaceRec sFaces[VOX_MAX_FACES];
static struct FaceRec sFacesSorted[VOX_MAX_FACES];
static Vtx sWorldVtx[VOX_MAX_FACES * 4];
static Gfx sWorldGfx[VOX_MAX_FACES / 8 * 9 + TEX_COUNT * 16 + 64];
static s32 sWorldFaceCount;

// cantos de cada face (em unidades de bloco 0/1) + coordenadas de textura
static void face_corners(s32 face, s32 c[4][3], s32 uv[4][2]) {
    // ordem: para cada face definimos 4 cantos e (s,t) de cada canto
    switch (face) {
        case FACE_PX: // olhando de +X: direita = -Z
            c[0][0]=1;c[0][1]=1;c[0][2]=1; uv[0][0]=0;  uv[0][1]=0;
            c[1][0]=1;c[1][1]=0;c[1][2]=1; uv[1][0]=0;  uv[1][1]=16;
            c[2][0]=1;c[2][1]=0;c[2][2]=0; uv[2][0]=16; uv[2][1]=16;
            c[3][0]=1;c[3][1]=1;c[3][2]=0; uv[3][0]=16; uv[3][1]=0;
            break;
        case FACE_NX: // olhando de -X: direita = +Z
            c[0][0]=0;c[0][1]=1;c[0][2]=0; uv[0][0]=0;  uv[0][1]=0;
            c[1][0]=0;c[1][1]=0;c[1][2]=0; uv[1][0]=0;  uv[1][1]=16;
            c[2][0]=0;c[2][1]=0;c[2][2]=1; uv[2][0]=16; uv[2][1]=16;
            c[3][0]=0;c[3][1]=1;c[3][2]=1; uv[3][0]=16; uv[3][1]=0;
            break;
        case FACE_PZ: // olhando de +Z: direita = +X
            c[0][0]=0;c[0][1]=1;c[0][2]=1; uv[0][0]=0;  uv[0][1]=0;
            c[1][0]=0;c[1][1]=0;c[1][2]=1; uv[1][0]=0;  uv[1][1]=16;
            c[2][0]=1;c[2][1]=0;c[2][2]=1; uv[2][0]=16; uv[2][1]=16;
            c[3][0]=1;c[3][1]=1;c[3][2]=1; uv[3][0]=16; uv[3][1]=0;
            break;
        case FACE_NZ: // olhando de -Z: direita = -X
            c[0][0]=1;c[0][1]=1;c[0][2]=0; uv[0][0]=0;  uv[0][1]=0;
            c[1][0]=1;c[1][1]=0;c[1][2]=0; uv[1][0]=0;  uv[1][1]=16;
            c[2][0]=0;c[2][1]=0;c[2][2]=0; uv[2][0]=16; uv[2][1]=16;
            c[3][0]=0;c[3][1]=1;c[3][2]=0; uv[3][0]=16; uv[3][1]=0;
            break;
        case FACE_PY:
            c[0][0]=0;c[0][1]=1;c[0][2]=0; uv[0][0]=0;  uv[0][1]=0;
            c[1][0]=0;c[1][1]=1;c[1][2]=1; uv[1][0]=0;  uv[1][1]=16;
            c[2][0]=1;c[2][1]=1;c[2][2]=1; uv[2][0]=16; uv[2][1]=16;
            c[3][0]=1;c[3][1]=1;c[3][2]=0; uv[3][0]=16; uv[3][1]=0;
            break;
        default: // FACE_NY
            c[0][0]=0;c[0][1]=0;c[0][2]=1; uv[0][0]=0;  uv[0][1]=0;
            c[1][0]=0;c[1][1]=0;c[1][2]=0; uv[1][0]=0;  uv[1][1]=16;
            c[2][0]=1;c[2][1]=0;c[2][2]=0; uv[2][0]=16; uv[2][1]=16;
            c[3][0]=1;c[3][1]=0;c[3][2]=1; uv[3][0]=16; uv[3][1]=0;
            break;
    }
}

// garante ordem anti-horaria vista de fora (normal para fora)
static void orient_quad(s32 c[4][3], s32 uv[4][2], const s8 n[3]) {
    s32 ax = c[1][0] - c[0][0], ay = c[1][1] - c[0][1], az = c[1][2] - c[0][2];
    s32 bx = c[2][0] - c[0][0], by = c[2][1] - c[0][1], bz = c[2][2] - c[0][2];
    s32 cxp = ay * bz - az * by, cyp = az * bx - ax * bz, czp = ax * by - ay * bx;
    if (cxp * n[0] + cyp * n[1] + czp * n[2] < 0) {
        s32 t[3], tu[2];
        memcpy(t, c[1], sizeof(t)); memcpy(c[1], c[3], sizeof(t)); memcpy(c[3], t, sizeof(t));
        memcpy(tu, uv[1], sizeof(tu)); memcpy(uv[1], uv[3], sizeof(tu)); memcpy(uv[3], tu, sizeof(tu));
    }
}

static void set_vtx(Vtx *v, s32 x, s32 y, s32 z, s32 s, s32 t, u8 r, u8 g, u8 b, u8 a) {
    v->v.ob[0] = x;
    v->v.ob[1] = y;
    v->v.ob[2] = z;
    v->v.flag = 0;
    v->v.tc[0] = s;
    v->v.tc[1] = t;
    v->v.cn[0] = r;
    v->v.cn[1] = g;
    v->v.cn[2] = b;
    v->v.cn[3] = a;
}

static void rebuild_mesh(void) {
    u64 prefix = area_prefix();
    s32 n = 0;
    s32 counts[TEX_COUNT] = { 0 };

    for (u32 i = 0; i < VOX_TABLE_SIZE && n < VOX_MAX_FACES - 6; i++) {
        if (!sTable[i].type || (sTable[i].key & 0xFFF0000000000000ULL) != prefix) {
            continue;
        }
        s32 x, y, z;
        key_coords(sTable[i].key, &x, &y, &z);
        for (s32 f = 0; f < 6; f++) {
            if (vox_get(x + sFaceDir[f][0], y + sFaceDir[f][1], z + sFaceDir[f][2])) {
                continue; // face escondida
            }
            sFaces[n].x = x;
            sFaces[n].y = y;
            sFaces[n].z = z;
            sFaces[n].face = f;
            sFaces[n].tex = block_tex(sTable[i].type, f);
            counts[sFaces[n].tex]++;
            n++;
        }
    }

    // ordena por textura (counting sort) para trocar de textura poucas vezes
    s32 start[TEX_COUNT], pos[TEX_COUNT], acc = 0;
    for (s32 t = 0; t < TEX_COUNT; t++) {
        start[t] = pos[t] = acc;
        acc += counts[t];
    }
    for (s32 i = 0; i < n; i++) {
        sFacesSorted[pos[sFaces[i].tex]++] = sFaces[i];
    }

    Gfx *g = sWorldGfx;
    gDPPipeSync(g++);
    gSPClearGeometryMode(g++, G_LIGHTING);
    gSPSetGeometryMode(g++, G_SHADE | G_SHADING_SMOOTH | G_CULL_BACK);
    gDPSetCombineMode(g++, G_CC_MODULATERGB, G_CC_MODULATERGB);
    gDPSetTextureFilter(g++, G_TF_POINT);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);

    Vtx *v = sWorldVtx;
    for (s32 t = 0; t < TEX_COUNT; t++) {
        if (counts[t] == 0) {
            continue;
        }
        gDPLoadTextureBlock(g++, sTexNames[t], G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0,
                            G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, 4, 4, G_TX_NOLOD, G_TX_NOLOD);
        for (s32 i = start[t]; i < start[t] + counts[t]; i += 8) {
            s32 batch = start[t] + counts[t] - i;
            if (batch > 8) {
                batch = 8;
            }
            Vtx *bv = v;
            for (s32 q = 0; q < batch; q++) {
                struct FaceRec *fr = &sFacesSorted[i + q];
                s32 c[4][3], uv[4][2];
                face_corners(fr->face, c, uv);
                orient_quad(c, uv, sFaceDir[fr->face]);
                u8 sh = sFaceShade[fr->face];
                for (s32 k = 0; k < 4; k++) {
                    set_vtx(v++, (fr->x + c[k][0]) * S, (fr->y + c[k][1]) * S, (fr->z + c[k][2]) * S,
                            uv[k][0] << 5, uv[k][1] << 5, sh, sh, sh, 255);
                }
            }
            gSPVertex(g++, bv, batch * 4, 0);
            for (s32 q = 0; q < batch; q++) {
                s32 b = q * 4;
                gSP2Triangles(g++, b, b + 1, b + 2, 0, b, b + 2, b + 3, 0);
            }
        }
    }

    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetTextureFilter(g++, G_TF_BILERP);
    gDPPipeSync(g++);
    gSPSetGeometryMode(g++, G_LIGHTING);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    gSPEndDisplayList(g++);

    sWorldFaceCount = n;
    sMeshDirty = FALSE;
}

Gfx *voxel_world_dl(void) {
    if (sAreaPrefix != area_prefix() || sAreaCount == 0) {
        return NULL;
    }
    if (sMeshDirty) {
        rebuild_mesh();
    }
    return sWorldFaceCount > 0 ? sWorldGfx : NULL;
}

/* ------------------------------------------------------------------------- */
/* Contorno do bloco mirado                                                  */
/* ------------------------------------------------------------------------- */

static Vtx sHlVtx[12 * 24];
static Gfx sHlGfx[12 * 10 + 16];
static u8 sHlValid;

static void emit_box(Gfx **gp, Vtx **vp, f32 x0, f32 y0, f32 z0, f32 x1, f32 y1, f32 z1, u8 r, u8 g, u8 b) {
    Vtx *bv = *vp;
    f32 lo[3] = { x0, y0, z0 }, hi[3] = { x1, y1, z1 };
    for (s32 f = 0; f < 6; f++) {
        s32 c[4][3], uv[4][2];
        face_corners(f, c, uv);
        orient_quad(c, uv, sFaceDir[f]);
        u8 sh = sFaceShade[f];
        for (s32 k = 0; k < 4; k++) {
            set_vtx((*vp)++, (s32)(c[k][0] ? hi[0] : lo[0]), (s32)(c[k][1] ? hi[1] : lo[1]),
                    (s32)(c[k][2] ? hi[2] : lo[2]), 0, 0, r * sh / 255, g * sh / 255, b * sh / 255, 255);
        }
    }
    gSPVertex((*gp)++, bv, 24, 0);
    for (s32 f = 0; f < 6; f++) {
        s32 q = f * 4;
        gSP2Triangles((*gp)++, q, q + 1, q + 2, 0, q, q + 2, q + 3, 0);
    }
}

static void build_highlight(void) {
    sHlValid = FALSE;
    s32 *cell = sHasTarget ? sTarget : (sHasPlace ? sPlace : NULL);
    if (!cell) {
        return;
    }
    u8 col = sHasTarget ? 20 : 235;
    f32 e = 1.5f, t = 2.5f; // folga e espessura das arestas
    f32 x0 = cell[0] * S - e, y0 = cell[1] * S - e, z0 = cell[2] * S - e;
    f32 x1 = x0 + S + 2 * e, y1 = y0 + S + 2 * e, z1 = z0 + S + 2 * e;

    Gfx *g = sHlGfx;
    Vtx *v = sHlVtx;
    gDPPipeSync(g++);
    gSPClearGeometryMode(g++, G_LIGHTING);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);
    // 12 arestas como caixas finas
    for (s32 a = 0; a < 2; a++) {
        for (s32 b = 0; b < 2; b++) {
            f32 ya = a ? y1 - t : y0, za = b ? z1 - t : z0, xa = a ? x1 - t : x0;
            emit_box(&g, &v, x0, ya, za, x1, ya + t, za + t, col, col, col);        // ao longo de X
            emit_box(&g, &v, xa, y0, za, xa + t, y1, za + t, col, col, col);        // ao longo de Y
            emit_box(&g, &v, xa, ya, z0, xa + t, ya + t, z1, col, col, col);        // ao longo de Z
        }
    }
    gDPPipeSync(g++);
    gSPSetGeometryMode(g++, G_LIGHTING);
    gSPEndDisplayList(g++);
    sHlValid = TRUE;
}

Gfx *voxel_highlight_dl(void) {
    return (sHlValid && sAreaPrefix == area_prefix()) ? sHlGfx : NULL;
}

/* ------------------------------------------------------------------------- */
/* Raio de mira                                                              */
/* ------------------------------------------------------------------------- */

static void fp_dir(Vec3f d) {
    d[0] = sins(sFpYaw) * coss(sFpPitch);
    d[1] = sins(sFpPitch);
    d[2] = coss(sFpYaw) * coss(sFpPitch);
}

static f32 fp_eye_height(struct MarioState *m) {
    u32 a = m->action;
    if (a == ACT_CROUCHING || a == ACT_CRAWLING || a == ACT_START_CROUCHING || a == ACT_STOP_CROUCHING
        || a == ACT_START_CRAWLING || a == ACT_STOP_CRAWLING || (a & ACT_FLAG_SHORT_HITBOX)) {
        return 75.0f;
    }
    if (a & ACT_FLAG_SWIMMING) {
        return 60.0f;
    }
    if (gVoxelSneaking && a == ACT_BLOCO_GROUND) {
        return 122.0f;
    }
    return 140.0f;
}

static s32 mario_overlaps_cell(s32 cx, s32 cy, s32 cz) {
    struct MarioState *m = gMarioState;
    f32 r = 36.0f, h = 155.0f;
    f32 x0 = cx * S, y0 = cy * S, z0 = cz * S;
    return m->pos[0] + r > x0 && m->pos[0] - r < x0 + S && m->pos[2] + r > z0 && m->pos[2] - r < z0 + S
           && m->pos[1] + h > y0 && m->pos[1] < y0 + S;
}

static void update_target(void) {
    struct MarioState *m = gMarioState;
    Vec3f orig, dir;
    f32 maxDist;

    sHasTarget = sHasPlace = FALSE;
    if (sFpApplied) {
        orig[0] = m->pos[0];
        orig[1] = m->pos[1] + fp_eye_height(m);
        orig[2] = m->pos[2];
        fp_dir(dir);
        maxDist = VOX_REACH;
    } else {
        vec3f_copy(orig, gLakituState.pos);
        dir[0] = gLakituState.focus[0] - orig[0];
        dir[1] = gLakituState.focus[1] - orig[1];
        dir[2] = gLakituState.focus[2] - orig[2];
        f32 len = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (len < 1.0f) {
            return;
        }
        dir[0] /= len; dir[1] /= len; dir[2] /= len;
        maxDist = len + VOX_REACH;
    }

    // terreno do jogo
    f32 terrainDist = maxDist + 1.0f;
    struct Surface *surf = NULL;
    Vec3f hitPos, ray;
    ray[0] = dir[0] * maxDist; ray[1] = dir[1] * maxDist; ray[2] = dir[2] * maxDist;
    find_surface_on_ray(orig, ray, &surf, hitPos);
    if (surf) {
        f32 dx = hitPos[0] - orig[0], dy = hitPos[1] - orig[1], dz = hitPos[2] - orig[2];
        terrainDist = sqrtf(dx * dx + dy * dy + dz * dz);
    }

    // DDA pela grade de blocos (Amanatides & Woo)
    if (sAreaCount > 0) {
        s32 cell[3], step[3], prev[3];
        f32 tMax[3], tDelta[3];
        for (s32 i = 0; i < 3; i++) {
            cell[i] = to_block(orig[i]);
            prev[i] = cell[i];
            if (dir[i] > 1e-6f) {
                step[i] = 1;
                tMax[i] = ((cell[i] + 1) * S - orig[i]) / dir[i];
                tDelta[i] = S / dir[i];
            } else if (dir[i] < -1e-6f) {
                step[i] = -1;
                tMax[i] = (cell[i] * S - orig[i]) / dir[i];
                tDelta[i] = -S / dir[i];
            } else {
                step[i] = 0;
                tMax[i] = tDelta[i] = 1e30f;
            }
        }
        f32 t = 0.0f;
        while (t <= maxDist && t <= terrainDist) {
            if (vox_get(cell[0], cell[1], cell[2])) {
                sHasTarget = TRUE;
                memcpy(sTarget, cell, sizeof(sTarget));
                if (!vox_get(prev[0], prev[1], prev[2]) && !mario_overlaps_cell(prev[0], prev[1], prev[2])
                    && (prev[0] != cell[0] || prev[1] != cell[1] || prev[2] != cell[2])) {
                    sHasPlace = TRUE;
                    memcpy(sPlace, prev, sizeof(sPlace));
                }
                return;
            }
            memcpy(prev, cell, sizeof(prev));
            s32 ax = tMax[0] < tMax[1] ? (tMax[0] < tMax[2] ? 0 : 2) : (tMax[1] < tMax[2] ? 1 : 2);
            t = tMax[ax];
            tMax[ax] += tDelta[ax];
            cell[ax] += step[ax];
        }
    }

    // sem bloco: coloca encostado no terreno
    if (surf && terrainDist <= maxDist) {
        s32 c[3];
        c[0] = to_block(hitPos[0] + surf->normal.x * 2.0f);
        c[1] = to_block(hitPos[1] + surf->normal.y * 2.0f);
        c[2] = to_block(hitPos[2] + surf->normal.z * 2.0f);
        if (!vox_get(c[0], c[1], c[2]) && !mario_overlaps_cell(c[0], c[1], c[2])) {
            sHasPlace = TRUE;
            memcpy(sPlace, c, sizeof(sPlace));
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Servicos para os itens                                                    */
/* ------------------------------------------------------------------------- */

s32 voxel_is_solid(s32 bx, s32 by, s32 bz) {
    return sAreaPrefix == area_prefix() && vox_get(bx, by, bz) != 0;
}

const char *voxel_block_tex_name(u8 block, s32 face) {
    return sTexNames[block_tex(block, face)];
}

u8 voxel_held_item(void) {
    return sHotbarSel[sSlot];
}

s32 voxel_fp_active(void) {
    return sFpApplied;
}

void voxel_fp_eye(Vec3f out) {
    out[0] = gMarioState->pos[0];
    out[1] = gMarioState->pos[1] + fp_eye_height(gMarioState);
    out[2] = gMarioState->pos[2];
}

void voxel_fp_look(Vec3f out) {
    fp_dir(out);
}

// raio pela grade de blocos; retorna 1 e a distancia/celula do primeiro bloco
s32 voxel_raycast(Vec3f orig, Vec3f dir, f32 maxLen, f32 *tHit, s32 cell[3]) {
    if (sAreaCount == 0 || sAreaPrefix != area_prefix()) {
        return 0;
    }
    s32 c[3], step[3];
    f32 tMax[3], tDelta[3];
    for (s32 i = 0; i < 3; i++) {
        c[i] = to_block(orig[i]);
        if (dir[i] > 1e-6f) {
            step[i] = 1; tMax[i] = ((c[i] + 1) * S - orig[i]) / dir[i]; tDelta[i] = S / dir[i];
        } else if (dir[i] < -1e-6f) {
            step[i] = -1; tMax[i] = (c[i] * S - orig[i]) / dir[i]; tDelta[i] = -S / dir[i];
        } else {
            step[i] = 0; tMax[i] = tDelta[i] = 1e30f;
        }
    }
    f32 t = 0.0f;
    while (t <= maxLen) {
        if (vox_get(c[0], c[1], c[2])) {
            *tHit = t;
            memcpy(cell, c, sizeof(c));
            return 1;
        }
        s32 ax = tMax[0] < tMax[1] ? (tMax[0] < tMax[2] ? 0 : 2) : (tMax[1] < tMax[2] ? 1 : 2);
        t = tMax[ax];
        tMax[ax] += tDelta[ax];
        c[ax] += step[ax];
    }
    return 0;
}

// origem e direcao do tiro: em 1a pessoa, do olho; em 3a, do peito ate o ponto da mira
void voxel_shot_ray(Vec3f orig, Vec3f dir) {
    struct MarioState *m = gMarioState;
    if (sFpApplied) {
        voxel_fp_eye(orig);
        orig[1] -= 8.0f;
        fp_dir(dir);
        return;
    }
    Vec3f cam, cdir, target;
    vec3f_copy(cam, gLakituState.pos);
    vec3f_dif(cdir, gLakituState.focus, cam);
    vec3f_normalize(cdir);
    f32 far = 6000.0f, tb;
    s32 cell[3];
    struct Surface *surf = NULL;
    Vec3f hit, ray = { cdir[0] * far, cdir[1] * far, cdir[2] * far };
    find_surface_on_ray(cam, ray, &surf, hit);
    if (surf) {
        far = sqrtf((hit[0] - cam[0]) * (hit[0] - cam[0]) + (hit[1] - cam[1]) * (hit[1] - cam[1])
                    + (hit[2] - cam[2]) * (hit[2] - cam[2]));
    }
    if (voxel_raycast(cam, cdir, far, &tb, cell) && tb < far) {
        far = tb;
    }
    if (far < 300.0f) far = 300.0f;
    target[0] = cam[0] + cdir[0] * far;
    target[1] = cam[1] + cdir[1] * far;
    target[2] = cam[2] + cdir[2] * far;
    orig[0] = m->pos[0];
    orig[1] = m->pos[1] + 110.0f;
    orig[2] = m->pos[2];
    vec3f_dif(dir, target, orig);
    vec3f_normalize(dir);
}

/* ------------------------------------------------------------------------- */
/* Camera                                                                    */
/* ------------------------------------------------------------------------- */

static s32 fp_allowed(struct Camera *c) {
    struct MarioState *m = gMarioState;
    if (c == NULL || c->cutscene != 0 || m == NULL || m->marioObj == NULL) {
        return FALSE;
    }
    if (c->mode == CAMERA_MODE_INSIDE_CANNON || m->action == ACT_IN_CANNON || m->action == ACT_SHOT_FROM_CANNON) {
        return FALSE;
    }
    if (m->action & ACT_FLAG_INTANGIBLE) {
        return FALSE; // portas, pinturas, entradas de fase etc.
    }
    return TRUE;
}

void voxel_camera_override(struct Camera *c) {
    sFpApplied = FALSE;
    gVoxelHideMario = FALSE;
    // modo de teste 6: camera de retrato, de frente para o personagem
    if ((sTestMode == 6 || sTestMode == 8 || sTestMode == 9) && sTestFrame > 40 && gMarioState && gMarioState->marioObj) {
        struct MarioState *pm = gMarioState;
        const char *ry = getenv("BLOCOS_RETRATO_YAW"); // teste: angulo da camera (hex)
        s16 yaw = pm->faceAngle[1] + (ry ? (s16) strtol(ry, NULL, 16) : 0x1800);
        Vec3f e = { pm->pos[0] + sins(yaw) * 260.0f, pm->pos[1] + 150.0f, pm->pos[2] + coss(yaw) * 260.0f };
        Vec3f fo = { pm->pos[0], pm->pos[1] + 95.0f, pm->pos[2] };
        vec3f_copy(gLakituState.pos, e);
        vec3f_copy(gLakituState.focus, fo);
        return;
    }
    if (!gVoxelFirstPerson || !fp_allowed(c)) {
        return;
    }
    struct MarioState *m = gMarioState;
    Vec3f eye, focus, d;
    eye[0] = m->pos[0];
    eye[1] = m->pos[1] + fp_eye_height(m);
    eye[2] = m->pos[2];
    fp_dir(d);
    focus[0] = eye[0] + d[0] * 200.0f;
    focus[1] = eye[1] + d[1] * 200.0f;
    focus[2] = eye[2] + d[2] * 200.0f;

    vec3f_copy(gLakituState.pos, eye);
    vec3f_copy(gLakituState.curPos, eye);
    vec3f_copy(gLakituState.goalPos, eye);
    vec3f_copy(gLakituState.focus, focus);
    vec3f_copy(gLakituState.curFocus, focus);
    vec3f_copy(gLakituState.goalFocus, focus);
    vec3f_copy(c->pos, eye);
    vec3f_copy(c->focus, focus);
    gLakituState.roll = 0;
    c->yaw = sFpYaw + 0x8000;
    c->nextYaw = c->yaw;
    gLakituState.yaw = c->yaw;
    sFpApplied = TRUE;
    gVoxelHideMario = TRUE;
}

s32 voxel_wants_mouse(void) {
    return gVoxelFirstPerson && sCurrPlayMode == 0;
}

static void select_slot(s32 s);

/* ------------------------------------------------------------------------- */
/* Teste automatizado                                                        */
/* ------------------------------------------------------------------------- */

static void test_build_scene(void) {
    struct MarioState *m = gMarioState;
    s32 mx = to_block(m->pos[0]), my = to_block(m->pos[1] + 1.0f), mz = to_block(m->pos[2]);
    s32 fx = sins(m->faceAngle[1]) > 0.7f ? 1 : (sins(m->faceAngle[1]) < -0.7f ? -1 : 0);
    s32 fz = fx == 0 ? (coss(m->faceAngle[1]) >= 0 ? 1 : -1) : 0;
    s32 sx = -fz, sz = fx; // eixo lateral
    // escada de 4 degraus a frente
    for (s32 i = 0; i < 4; i++) {
        for (s32 h = 0; h <= i; h++) {
            for (s32 w = -1; w <= 1; w++) {
                vox_set(mx + fx * (3 + i) + sx * w, my + h, mz + fz * (3 + i) + sz * w,
                        h == i ? BLOCO_GRAMA : BLOCO_TERRA);
            }
        }
    }
    // mostruario de todos os blocos ao lado
    for (s32 t = BLOCO_GRAMA; t < BLOCO_COUNT; t++) {
        vox_set(mx + sx * 3 + fx * (t - 3), my, mz + sz * 3 + fz * (t - 3), t);
        vox_set(mx + sx * 3 + fx * (t - 3), my + 1, mz + sz * 3 + fz * (t - 3), t);
    }
    // torre com teto
    for (s32 h = 0; h < 4; h++) {
        vox_set(mx - sx * 3, my + h, mz - sz * 3, BLOCO_TRONCO);
    }
    for (s32 a = -1; a <= 1; a++) {
        for (s32 b = -1; b <= 1; b++) {
            vox_set(mx - sx * 3 + a, my + 4, mz - sz * 3 + b, BLOCO_FOLHAS);
        }
    }
    sFpYaw = m->faceAngle[1];
    sFpPitch = -0x0800;
}

static void test_build_stress(void) {
    struct MarioState *m = gMarioState;
    s32 mx = to_block(m->pos[0]), my = to_block(m->pos[1] + 1.0f), mz = to_block(m->pos[2]);
    // plataforma 48x48 suspensa + muralha, ~2700 blocos
    for (s32 a = -24; a < 24; a++) {
        for (s32 b = -24; b < 24; b++) {
            vox_set(mx + a, my + 6, mz + b, (a + b) & 1 ? BLOCO_PEDRA : BLOCO_TABUAS);
        }
    }
    for (s32 a = -24; a < 24; a++) {
        for (s32 h = 0; h < 4; h++) {
            vox_set(mx + a, my + h, mz - 8, BLOCO_TIJOLO);
            vox_set(mx + a, my + h, mz + 8, BLOCO_PEDREGULHO);
        }
    }
}

static u32 sTestTicks;

static void test_mode_step(void) {
    struct MarioState *m = gMarioState;
    sTestFrame++;
    if (sTestFrame == 30) {
        if (sTestMode == 4) {
            test_build_stress();
        } else if (sTestMode != 5 && sTestMode != 10 && sTestMode != 11) {
            test_build_scene();
        }
        if (sTestMode == 2 || sTestMode == 4 || sTestMode == 7) {
            gVoxelFirstPerson = TRUE;
            sFpYaw = m->faceAngle[1];
            sFpPitch = sTestMode == 4 ? 0x1000 : -0x0800;
        }
    }
    // modo 2: clique esquerdo (quebrar) no frame 70, direito (colocar) no 100 com o slot 7 (tijolo)
    if (sTestMode == 2) {
        gVoxelMouseButtons = 0;
        if (sTestFrame == 70) gVoxelMouseButtons = SDL_BUTTON_LMASK;
        if (sTestFrame == 99) select_slot(6);
        if (sTestFrame == 100) gVoxelMouseButtons = SDL_BUTTON_RMASK;
    }
    // modo 7: espada (golpe no frame 60) e depois arco (puxa 75..110, solta)
    if (sTestMode == 7) {
        gVoxelMouseButtons = 0;
        if (sTestFrame == 40) select_slot(0);
        if (sTestFrame == 60) gVoxelMouseButtons = SDL_BUTTON_LMASK;
        if (sTestFrame == 70) select_slot(1);
        if (sTestFrame >= 75 && sTestFrame < 110) gVoxelMouseButtons = SDL_BUTTON_RMASK;
        if (sTestFrame == 140) select_slot(4);
        if (sTestFrame == 150) gVoxelMouseButtons = SDL_BUTTON_RMASK;
        if (sTestFrame > 112 && (sTestFrame % 3) == 0) {
            s32 fl, st;
            voxel_items_arrow_count(&fl, &st);
            printf("[blocos] flechas voando=%d cravadas=%d\n", fl, st);
        }
    }
    if ((sTestMode == 8 || sTestMode == 9) && sTestFrame == 35) {
        select_slot(sTestMode == 8 ? 0 : 1);
    }
    // modo 10: mede andar / correr / pulo e testa agachar na beirada
    if (sTestMode == 10) {
        static f32 x0, z0, ymax;
        struct Controller *ct = gPlayer1Controller;
        if (sTestFrame == 40) { x0 = m->pos[0]; z0 = m->pos[2]; voxel_move_test_keys(0, 0); }
        if (sTestFrame >= 40 && sTestFrame < 160) { ct->stickY = 64; ct->stickMag = 64; ct->stickX = 0; }
        if (sTestFrame == 100) {
            printf("[blocos] andar: %.2f blocos/s\n", sqrtf((m->pos[0]-x0)*(m->pos[0]-x0)+(m->pos[2]-z0)*(m->pos[2]-z0)) / 100.0f / 2.0f);
            x0 = m->pos[0]; z0 = m->pos[2]; voxel_move_test_keys(1, 0);
        }
        if (sTestFrame == 160) {
            printf("[blocos] correr: %.2f blocos/s\n", sqrtf((m->pos[0]-x0)*(m->pos[0]-x0)+(m->pos[2]-z0)*(m->pos[2]-z0)) / 100.0f / 2.0f);
            voxel_move_test_keys(0, 0);
        }
        if (sTestFrame == 180) { ymax = m->pos[1]; x0 = m->pos[1]; }
        if (sTestFrame >= 180 && sTestFrame < 182) { ct->buttonDown |= A_BUTTON; ct->buttonPressed |= A_BUTTON; }
        if (sTestFrame > 180 && sTestFrame < 230 && m->pos[1] > ymax) ymax = m->pos[1];
        if (sTestFrame == 230) printf("[blocos] altura do pulo: %.2f blocos\n", (ymax - x0) / 100.0f);
        // pilar de 3 blocos e anda agachado ate a beirada
        if (sTestFrame == 235) {
            s32 bx = to_block(m->pos[0]), bz = to_block(m->pos[2]), by = to_block(m->pos[1] + 1.0f);
            for (s32 h = 0; h < 3; h++) vox_set(bx, by + h, bz, BLOCO_PEDRA);
            m->pos[0] = bx * S + 50; m->pos[2] = bz * S + 50; m->pos[1] = (by + 3) * S + 5;
            m->vel[1] = 0;
            voxel_move_test_keys(0, 1);
        }
        if (sTestFrame > 250 && sTestFrame < 330) { ct->stickY = 64; ct->stickMag = 64; ct->stickX = 0; }
        if (sTestFrame == 330) {
            printf("[blocos] agachado na beirada: y=%.0f chao=%.0f acao=%08X (%s)\n", m->pos[1], m->floorHeight,
                   m->action, m->pos[1] > m->floorHeight - 1 && m->pos[1] >= (to_block(m->pos[1]+1)) * S ? "nao caiu" : "?");
            voxel_move_test_keys(0, 0);
        }
        if (sTestFrame > 335 && sTestFrame < 380) { ct->stickY = 64; ct->stickMag = 64; ct->stickX = 0; }
        if (sTestFrame == 380) printf("[blocos] sem agachar: y=%.0f acao=%08X\n", m->pos[1], m->action);
    }
    // modo 11: introducao; aperta Espaco (A) no frame 120 para pular
    if (sTestMode == 11 && sTestFrame == 120) {
        gPlayer1Controller->buttonPressed |= A_BUTTON;
        gPlayer1Controller->buttonDown |= A_BUTTON;
        printf("[blocos] apertou Espaco na intro (acao=%08X)\n", m->action);
    }
    // modo 4: gira a camera continuamente (estresse de renderizacao)
    if (sTestMode == 4 && sTestFrame > 30) {
        sFpYaw += 0x180;
    }
    // anda para frente (sobe a escada pulando) entre os frames 90 e 260
    if (sTestMode == 3 && sTestFrame > 90 && sTestFrame < 260) {
        struct Controller *ct = gPlayer1Controller;
        ct->stickX = 0;
        ct->stickY = 64;
        ct->stickMag = 64;
        if ((sTestFrame % 24) == 0) {
            ct->buttonPressed |= A_BUTTON;
            ct->buttonDown |= A_BUTTON;
        }
    }
    if ((sTestFrame % 30) == 0) {
        u32 now = SDL_GetTicks();
        if (sTestTicks) {
            printf("[blocos] fps=%.1f\n", 30000.0f / (f32)(now - sTestTicks));
        }
        sTestTicks = now;
        printf("[blocos] f=%d pos=(%.0f,%.0f,%.0f) chao=%.0f acao=%08X blocos=%d faces=%d alvo=%d colocar=%d fp=%d\n",
               sTestFrame, m->pos[0], m->pos[1], m->pos[2], m->floorHeight, m->action, sAreaCount,
               sWorldFaceCount, sHasTarget, sHasPlace, sFpApplied);
        printf("[blocos] cam pos=(%.0f,%.0f,%.0f) foco=(%.0f,%.0f,%.0f) modo=%d yawMario=%04X\n",
               gLakituState.pos[0], gLakituState.pos[1], gLakituState.pos[2], gLakituState.focus[0],
               gLakituState.focus[1], gLakituState.focus[2], gCurrentArea ? gCurrentArea->camera->mode : -1,
               (u16) m->faceAngle[1]);
        fflush(stdout);
    }
}

/* ------------------------------------------------------------------------- */
/* Atualizacao por frame                                                     */
/* ------------------------------------------------------------------------- */

static void select_slot(s32 s) {
    s = ((s % 9) + 9) % 9;
    if (s != sSlot) {
        play_sound(SOUND_MENU_CHANGE_SELECT, gDefaultSoundArgs);
    }
    sSlot = s;
    sSlotLabelTimer = 60;
}

void voxel_update(void) {
    struct MarioState *m = gMarioState;

    if (sTestMode < 0) {
        const char *e = getenv("BLOCOS_TESTE");
        sTestMode = e ? atoi(e) : 0;
        for (s32 i = 0; i < 9; i++) {
            sHotbarSel[i] = sHotbar[i];
        }
    }
    if (!sLoaded) {
        voxel_load();
    }
    if (sAreaPrefix != area_prefix()) {
        recount_area();
    }
    if (m == NULL || m->marioObj == NULL) {
        return;
    }

    // salvamento adiado
    if (sSaveTimer > 0 && --sSaveTimer == 0) {
        voxel_save();
        sSaveTimer = -1;
    }

    if (sSlotLabelTimer > 0) {
        sSlotLabelTimer--;
    }

    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    s32 inDialog = get_dialog_id() >= 0 || (m->action & ACT_GROUP_MASK) == ACT_GROUP_CUTSCENE;

    // F5: alterna 1a / 3a pessoa
    u8 f5 = ks[SDL_SCANCODE_F5];
    if (f5 && !sPrevF5) {
        gVoxelFirstPerson = !gVoxelFirstPerson;
        if (gVoxelFirstPerson) {
            sFpYaw = calculate_yaw(gLakituState.pos, gLakituState.focus);
            sFpPitch = 0;
        }
        play_sound(SOUND_MENU_CLICK_CHANGE_VIEW, gDefaultSoundArgs);
    }
    sPrevF5 = f5;

    // F6: alterna entre a movimentacao nova e a do Mario original
    u8 f6 = ks[SDL_SCANCODE_F6];
    if (f6 && !sPrevF6) {
        gVoxelMoveEnabled = !gVoxelMoveEnabled;
        if (!gVoxelMoveEnabled) {
            if (m->action == ACT_BLOCO_GROUND) set_mario_action(m, ACT_IDLE, 0);
            if (m->action == ACT_BLOCO_AIR) set_mario_action(m, ACT_FREEFALL, 0);
        }
        play_sound(SOUND_MENU_CLICK_CHANGE_VIEW, gDefaultSoundArgs);
    }
    sPrevF6 = f6;

    // hotbar
    for (s32 i = 0; i < 9; i++) {
        u8 down = ks[SDL_SCANCODE_1 + i];
        if (down && !sPrevNum[i]) {
            if (i == sSlot && sHotbarSel[i] > BLOCO_AR && sHotbarSel[i] < BLOCO_COUNT) {
                // apertar de novo o mesmo numero troca o tipo de bloco do slot
                u8 t = sHotbarSel[i] + 1;
                sHotbarSel[i] = t >= BLOCO_COUNT ? BLOCO_GRAMA : t;
                sSlotLabelTimer = 60;
                play_sound(SOUND_MENU_CHANGE_SELECT, gDefaultSoundArgs);
            } else {
                select_slot(i);
            }
        }
        sPrevNum[i] = down;
    }
    if (gVoxelMouseWheel != 0) {
        select_slot(sSlot - gVoxelMouseWheel);
        gVoxelMouseWheel = 0;
    }

    // olhar em 1a pessoa (mouse + setas/analogico direito)
    if (gVoxelFirstPerson && !inDialog) {
        sFpYaw -= (s16)(mouse_x * 22);
        s32 p = sFpPitch - mouse_y * 22;
        u16 btn = gPlayer1Controller->buttonDown;
        if (btn & L_CBUTTONS) sFpYaw += 0x300;
        if (btn & R_CBUTTONS) sFpYaw -= 0x300;
        if (btn & U_CBUTTONS) p += 0x200;
        if (btn & D_CBUTTONS) p -= 0x200;
#ifdef BETTERCAMERA
        sFpYaw -= gPlayer1Controller->extStickX * 6;
        p += gPlayer1Controller->extStickY * 5;
#endif
        if (p > 0x3800) p = 0x3800;
        if (p < -0x3800) p = -0x3800;
        sFpPitch = p;
        if (gCurrentArea && gCurrentArea->camera && sFpApplied) {
            gCurrentArea->camera->yaw = sFpYaw + 0x8000;
        }
        // parado: Mario vira para onde voce olha (socos saem na direcao certa)
        if (sFpApplied && (m->action & ACT_FLAG_STATIONARY)) {
            m->faceAngle[1] = sFpYaw;
            m->marioObj->header.gfx.angle[1] = sFpYaw;
        }
    }
    mouse_x = mouse_y = 0;

    if (sTestMode > 0) {
        test_mode_step();
    }

    update_target();

    // quebrar / colocar / pegar
    u32 mb = gVoxelMouseButtons;
    u32 pressed = mb & ~sPrevMouse;
    u32 released = sPrevMouse & ~mb;
    sPrevMouse = mb;
    if (sBreakCooldown > 0) sBreakCooldown--;
    if (sPlaceCooldown > 0) sPlaceCooldown--;

    u8 held = sHotbarSel[sSlot];
    s32 isBlock = held > BLOCO_AR && held < BLOCO_COUNT;
    s32 swordUsedClick = voxel_items_update(held, mb, pressed, released, !inDialog);

    if (!inDialog) {
        if (!swordUsedClick && (mb & SDL_BUTTON_LMASK) && sHasTarget
            && ((pressed & SDL_BUTTON_LMASK) || sBreakCooldown == 0)) {
            if (vox_remove(sTarget[0], sTarget[1], sTarget[2])) {
                play_sound(SOUND_GENERAL_BREAK_BOX, gDefaultSoundArgs);
                voxel_items_swing();
            }
            sBreakCooldown = 7;
            update_target();
        } else if ((pressed & SDL_BUTTON_LMASK) && held != ITEM_ESPADA) {
            voxel_items_swing(); // golpe no ar, como na mao vazia
        }
        if (isBlock && (mb & SDL_BUTTON_RMASK) && sHasPlace
            && ((pressed & SDL_BUTTON_RMASK) || sPlaceCooldown == 0)) {
            if (vox_set(sPlace[0], sPlace[1], sPlace[2], held)) {
                play_sound(SOUND_ACTION_TERRAIN_LANDING, gDefaultSoundArgs);
                voxel_items_swing();
            }
            sPlaceCooldown = 7;
            update_target();
        }
        if ((pressed & SDL_BUTTON_MMASK) && sHasTarget && isBlock) {
            u8 t = vox_get(sTarget[0], sTarget[1], sTarget[2]);
            if (t) {
                sHotbarSel[sSlot] = t;
                sSlotLabelTimer = 60;
            }
        }
    }

    build_highlight();
}

/* ------------------------------------------------------------------------- */
/* HUD: mira + hotbar                                                        */
/* ------------------------------------------------------------------------- */

static void hud_rect(Gfx **gp, Vtx **vp, f32 x0, f32 y0, f32 x1, f32 y1, u8 r, u8 g, u8 b, u8 a) {
    Vtx *v = *vp;
    set_vtx(v + 0, (s32) x0, (s32) y0, 0, 0, 0, r, g, b, a);
    set_vtx(v + 1, (s32) x1, (s32) y0, 0, 0, 0, r, g, b, a);
    set_vtx(v + 2, (s32) x1, (s32) y1, 0, 0, 0, r, g, b, a);
    set_vtx(v + 3, (s32) x0, (s32) y1, 0, 0, 0, r, g, b, a);
    gSPVertex((*gp)++, v, 4, 0);
    gSP2Triangles((*gp)++, 0, 1, 2, 0, 0, 2, 3, 0);
    *vp = v + 4;
}

static void hud_tex_rect(Gfx **gp, Vtx **vp, f32 x0, f32 y0, f32 x1, f32 y1, u8 sh) {
    Vtx *v = *vp;
    set_vtx(v + 0, (s32) x0, (s32) y0, 0, 0, 16 << 5, sh, sh, sh, 255);
    set_vtx(v + 1, (s32) x1, (s32) y0, 0, 16 << 5, 16 << 5, sh, sh, sh, 255);
    set_vtx(v + 2, (s32) x1, (s32) y1, 0, 16 << 5, 0, sh, sh, sh, 255);
    set_vtx(v + 3, (s32) x0, (s32) y1, 0, 0, 0, sh, sh, sh, 255);
    gSPVertex((*gp)++, v, 4, 0);
    gSP2Triangles((*gp)++, 0, 1, 2, 0, 0, 2, 3, 0);
    *vp = v + 4;
}

void voxel_render_hud(void) {
    if (gMarioState == NULL || gMarioState->marioObj == NULL || sCurrPlayMode != 0) {
        return;
    }
    Vtx *verts = alloc_display_list(sizeof(Vtx) * 4 * 64);
    if (verts == NULL) {
        return;
    }
    Vtx *v = verts;
    Gfx *g;

    voxel_items_render_viewmodel(sHotbarSel[sSlot]);
    create_dl_ortho_matrix();
    g = gDisplayListHead;
    gDPPipeSync(g++);
    // obs: nao mexer em G_ZBUFFER aqui; religar faz o skybox do proximo
    // frame gravar profundidade e esconder a cena 3D inteira
    gSPClearGeometryMode(g++, G_LIGHTING | G_CULL_BACK);
    gDPSetRenderMode(g++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(g++, G_CC_SHADE, G_CC_SHADE);

    // mira
    f32 cx = SCREEN_WIDTH / 2.0f, cy = SCREEN_HEIGHT / 2.0f;
    hud_rect(&g, &v, cx - 5, cy - 0.75f, cx + 5, cy + 0.75f, 255, 255, 255, 200);
    hud_rect(&g, &v, cx - 0.75f, cy - 5, cx + 0.75f, cy + 5, 255, 255, 255, 200);
    if (sHotbarSel[sSlot] == ITEM_ARCO && voxel_items_charge() > 0.0f) {
        f32 c = voxel_items_charge();
        hud_rect(&g, &v, cx - 8, cy - 12, cx + 8, cy - 10, 0, 0, 0, 160);
        hud_rect(&g, &v, cx - 8, cy - 12, cx - 8 + 16 * c, cy - 10, c >= 1.0f ? 255 : 200, 230, 90, 230);
    }

    // hotbar
    const f32 slotW = 20.0f;
    f32 x0 = cx - slotW * 4.5f, y0 = 6.0f;
    hud_rect(&g, &v, x0 - 2, y0 - 2, x0 + slotW * 9 + 2, y0 + slotW + 2, 0, 0, 0, 150);
    for (s32 i = 0; i < 9; i++) {
        f32 sx = x0 + i * slotW;
        hud_rect(&g, &v, sx + 1, y0 + 1, sx + slotW - 1, y0 + slotW - 1, 90, 90, 90, 160);
    }
    f32 sx = x0 + sSlot * slotW;
    hud_rect(&g, &v, sx - 1, y0 - 1, sx + slotW + 1, y0 + 1, 255, 255, 255, 255);
    hud_rect(&g, &v, sx - 1, y0 + slotW - 1, sx + slotW + 1, y0 + slotW + 1, 255, 255, 255, 255);
    hud_rect(&g, &v, sx - 1, y0 - 1, sx + 1, y0 + slotW + 1, 255, 255, 255, 255);
    hud_rect(&g, &v, sx + slotW - 1, y0 - 1, sx + slotW + 1, y0 + slotW + 1, 255, 255, 255, 255);

    // icones dos blocos
    gDPPipeSync(g++);
    gDPSetRenderMode(g++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gDPSetCombineMode(g++, G_CC_MODULATERGBA, G_CC_MODULATERGBA);
    gDPSetTextureFilter(g++, G_TF_POINT);
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    for (s32 i = 0; i < 9; i++) {
        s32 tex = block_tex(sHotbarSel[i], FACE_PX);
        gDPLoadTextureBlock(g++, sTexNames[tex], G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0,
                            G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, 4, 4, G_TX_NOLOD, G_TX_NOLOD);
        f32 ix = x0 + i * slotW + 3;
        hud_tex_rect(&g, &v, ix, y0 + 3, ix + slotW - 6, y0 + slotW - 3, i == sSlot ? 255 : 210);
    }
    gSPTexture(g++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    gDPSetTextureFilter(g++, G_TF_BILERP);
    gDPPipeSync(g++);
    gSPSetGeometryMode(g++, G_LIGHTING | G_CULL_BACK);
    gDPSetRenderMode(g++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDisplayListHead = g;

    if (sSlotLabelTimer > 0) {
        u8 held = sHotbarSel[sSlot];
        const char *name = held == ITEM_ESPADA ? "ESPADA" : (held == ITEM_ARCO ? "ARCO" : sBlockNames[held]);
        s32 len = strlen(name);
        print_text((s32)(cx - len * 6), (s32)(y0 + slotW + 6), name);
    }
}
