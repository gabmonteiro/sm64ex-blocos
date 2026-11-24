#ifndef VOXEL_H
#define VOXEL_H

/*
 * Mod "Blocos": camada de blocos (voxels) por cima das fases do jogo,
 * com quebrar/colocar, hotbar, camera em 1a pessoa e personagem quadradao.
 */

#include <PR/ultratypes.h>
#include <PR/gbi.h>
#include "types.h"

#define VOXEL_SIZE 100 // tamanho de um bloco em unidades do jogo

enum BlockType {
    BLOCO_AR = 0,
    BLOCO_GRAMA,
    BLOCO_TERRA,
    BLOCO_PEDRA,
    BLOCO_PEDREGULHO,
    BLOCO_TABUAS,
    BLOCO_TRONCO,
    BLOCO_TIJOLO,
    BLOCO_AREIA,
    BLOCO_FOLHAS,
    BLOCO_COUNT
};

// itens (ficam na hotbar junto com os blocos)
#define ITEM_ESPADA 200
#define ITEM_ARCO 201

// acoes da movimentacao nova (ids livres do jogo)
#define ACT_BLOCO_GROUND 0x04000441 // 0x041 | ACT_FLAG_MOVING | ACT_FLAG_ALLOW_FIRST_PERSON
#define ACT_BLOCO_AIR    0x0100089E // 0x09E | ACT_FLAG_AIR | ACT_FLAG_ALLOW_VERTICAL_WIND_ACTION

struct MarioState;
extern u8 gVoxelMoveEnabled;
extern u8 gVoxelSneaking;
s32 act_bloco_ground(struct MarioState *m);
s32 act_bloco_air(struct MarioState *m);
void voxel_move_convert(struct MarioState *m);
void voxel_move_test_keys(s32 sprint, s32 sneak);

struct WallCollisionData;
struct Camera;

extern u8 gVoxelFirstPerson;

// ciclo de vida / entrada (chamado uma vez por frame de jogo)
void voxel_update(void);

// colisao: retornam a altura (ou sentinela) e o Surface "falso" do bloco
f32 voxel_find_floor(f32 x, f32 y, f32 z, struct Surface **pfloor);
f32 voxel_find_ceil(f32 x, f32 y, f32 z, struct Surface **pceil);
s32 voxel_find_walls(struct WallCollisionData *data);
s32 voxel_any_blocks(void);

// renderizacao
Gfx *voxel_world_dl(void);
Gfx *voxel_highlight_dl(void);
void voxel_render_hud(void);
void voxel_camera_override(struct Camera *c);
const Gfx *voxel_char_substitute(const Gfx *dl);

// servicos usados pelos itens
s32 voxel_is_solid(s32 bx, s32 by, s32 bz);
s32 voxel_raycast(Vec3f orig, Vec3f dir, f32 maxLen, f32 *tHit, s32 cell[3]);
s32 voxel_fp_active(void);
void voxel_fp_eye(Vec3f out);
void voxel_fp_look(Vec3f out);
void voxel_fp_bob(f32 *bx, f32 *by);
void voxel_shot_ray(Vec3f orig, Vec3f dir);
const char *voxel_block_tex_name(u8 block, s32 face);
u8 voxel_held_item(void);

// itens (voxel_items.c)
s32 voxel_items_update(u8 held, u32 mb, u32 pressed, u32 released, s32 allowInput);
void voxel_items_swing(void);
f32 voxel_items_charge(void);
Gfx *voxel_items_world_dl(void);
void voxel_items_render_viewmodel(u8 held);
const Gfx *voxel_items_hand_dl(u8 item);
void voxel_items_clear_arrows(void);
s32 voxel_items_arrow_count(s32 *flying, s32 *stuck);

// entrada vinda da camada SDL
extern u32 gVoxelMouseButtons;
extern s32 gVoxelMouseWheel;
s32 voxel_wants_mouse(void);

#endif
