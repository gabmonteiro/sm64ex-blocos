/*
 * Mod "Blocos": movimentacao no estilo de jogos de blocos em primeira pessoa.
 *
 * Implementacao propria a partir dos valores publicos de fisica desse estilo:
 *  - caminhar ~4,3 blocos/s, correr (Ctrl) x1,3, agachar (Shift) x0,3
 *  - pulo de ~1,25 bloco (velocidade inicial 0,42 bloco/tique)
 *  - gravidade 0,08 bloco/tique^2 com arrasto vertical de 0,98 por tique
 *  - atrito no chao 0,6*0,91 por tique, no ar 0,91; aceleracao no ar 0,02
 *  - pulo correndo ganha um impulso de 0,2 bloco/tique para frente
 *  - agachado nao cai das beiradas
 *  - dano de queda acima de 3 blocos
 *
 * A fisica roda em tiques de 20 Hz; a posicao e integrada a cada frame de
 * 30 fps com a velocidade do ultimo tique (movimento suave, velocidades certas).
 * Colisao, agua, portas, dialogos, inimigos e estrelas continuam sendo do jogo:
 * so as acoes de locomocao basicas (parado, andar, pular, cair) sao trocadas.
 */

#include <math.h>

#include <SDL.h>

#include "sm64.h"
#include "voxel.h"
#include "mario.h"
#include "mario_step.h"
#include "level_update.h"
#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "audio/external.h"

#define TICKS_PER_FRAME (20.0f / 30.0f)
#define UNITS_PER_BLOCK 100.0f
#define TICK_TO_FRAME (UNITS_PER_BLOCK * TICKS_PER_FRAME) // bloco/tique -> unidades/frame

#define WALK_ACCEL 0.1f
#define SPRINT_MULT 1.3f
#define SNEAK_MULT 0.3f
#define AIR_ACCEL 0.02f
#define AIR_ACCEL_SPRINT 0.026f
#define GROUND_FRICTION (0.6f * 0.91f)
#define AIR_FRICTION 0.91f
#define GRAVITY 0.08f
#define VERT_DRAG 0.98f
#define JUMP_VEL 0.405f // 0,42 por tique; compensado pela integracao 20 Hz -> 30 fps (~1,25 bloco)
#define SPRINT_JUMP_BOOST 0.2f

u8 gVoxelMoveEnabled = TRUE;
u8 gVoxelSneaking = FALSE;

static f32 sVx, sVy, sVz;           // velocidade guardada (apos atrito), blocos/tique
static f32 sMoveX, sMoveY, sMoveZ;  // deslocamento do tique atual, blocos/tique
static f32 sTickAcc;
static u8 sSprinting;
static f32 sFallStartY;
static s8 sTestSprint = -1, sTestSneak = -1;
static u8 sJumpQueued;

void voxel_move_test_keys(s32 sprint, s32 sneak) {
    sTestSprint = sprint;
    sTestSneak = sneak;
}

static s32 key_sneak(void) {
    if (sTestSneak >= 0) return sTestSneak;
    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    return ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT];
}

static s32 key_sprint(void) {
    if (sTestSprint >= 0) return sTestSprint;
    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    return ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_RCTRL];
}

// direcao desejada no mundo (magnitude 0..1), relativa a camera
static void read_input(struct MarioState *m, f32 *ix, f32 *iz, f32 *mag) {
    f32 mg = m->intendedMag / 32.0f;
    if (mg > 1.0f) mg = 1.0f;
    if (mg < 0.05f) mg = 0.0f;
    *mag = mg;
    *ix = sins(m->intendedYaw) * mg;
    *iz = coss(m->intendedYaw) * mg;
}

// um tique de fisica; retorna TRUE se pulou neste tique
static s32 physics_tick(struct MarioState *m, s32 onGround) {
    f32 ix, iz, mag;
    read_input(m, &ix, &iz, &mag);
    s32 sneak = key_sneak() && onGround;
    gVoxelSneaking = key_sneak();

    // correr: Ctrl + andando com forca; para ao soltar, agachar ou quase parar
    if (key_sprint() && mag > 0.8f && !gVoxelSneaking) {
        sSprinting = TRUE;
    } else if (mag < 0.3f || gVoxelSneaking) {
        sSprinting = FALSE;
    }
    if (sneak) {
        ix *= SNEAK_MULT;
        iz *= SNEAK_MULT;
    }

    s32 jumped = FALSE;
    if (onGround && (sJumpQueued || (m->controller->buttonDown & A_BUTTON))) {
        sJumpQueued = FALSE;
        sVy = JUMP_VEL;
        jumped = TRUE;
        if (sSprinting && mag > 0.0f) {
            sVx += ix / mag * SPRINT_JUMP_BOOST;
            sVz += iz / mag * SPRINT_JUMP_BOOST;
        }
    }

    f32 accel;
    if (onGround && !jumped) {
        accel = WALK_ACCEL * (sSprinting ? SPRINT_MULT : 1.0f)
                * (0.16277136f / (GROUND_FRICTION * GROUND_FRICTION * GROUND_FRICTION));
    } else {
        accel = sSprinting ? AIR_ACCEL_SPRINT : AIR_ACCEL;
    }

    // deslocamento deste tique = velocidade guardada + aceleracao da entrada
    sMoveX = sVx + ix * accel;
    sMoveZ = sVz + iz * accel;
    sMoveY = onGround && !jumped ? 0.0f : sVy;

    f32 fric = (onGround && !jumped) ? GROUND_FRICTION : AIR_FRICTION;
    sVx = sMoveX * fric;
    sVz = sMoveZ * fric;
    if (fabsf(sVx) < 0.003f) sVx = 0;
    if (fabsf(sVz) < 0.003f) sVz = 0;
    if (!onGround || jumped) {
        sVy = (sMoveY - GRAVITY) * VERT_DRAG;
    } else {
        sVy = 0;
    }
    return jumped;
}

static s32 run_ticks(struct MarioState *m, s32 onGround) {
    s32 jumped = FALSE;
    sTickAcc += TICKS_PER_FRAME;
    while (sTickAcc >= 1.0f) {
        sTickAcc -= 1.0f;
        if (physics_tick(m, onGround && !jumped)) {
            jumped = TRUE;
        }
    }
    return jumped;
}

static void face_movement(struct MarioState *m, f32 vx, f32 vz) {
    if (vx * vx + vz * vz > 4.0f) {
        s16 diff = (s16)(atan2s(vz, vx) - m->faceAngle[1]);
        if (diff > 0x1800) diff = 0x1800;
        if (diff < -0x1800) diff = -0x1800;
        m->faceAngle[1] += diff;
    }
}

// tira a componente da velocidade que entra na parede (desliza ao longo dela)
static void slide_along_wall(struct MarioState *m) {
    if (m->wall == NULL) return;
    f32 nx = m->wall->normal.x, nz = m->wall->normal.z;
    f32 d = sVx * nx + sVz * nz;
    if (d < 0) {
        sVx -= d * nx;
        sVz -= d * nz;
    }
    d = sMoveX * nx + sMoveZ * nz;
    if (d < 0) {
        sMoveX -= d * nx;
        sMoveZ -= d * nz;
    }
}

static s32 floor_drop_too_far(struct MarioState *m, f32 x, f32 z) {
    struct Surface *f;
    f32 h = find_floor(x, m->pos[1] + 20.0f, z, &f);
    return f == NULL || h < m->pos[1] - 60.0f;
}

static void set_locomotion_anim(struct MarioState *m, f32 speed) {
    if (speed < 1.0f) {
        set_mario_animation(m, gVoxelSneaking ? MARIO_ANIM_CROUCHING : MARIO_ANIM_IDLE_HEAD_LEFT);
    } else if (gVoxelSneaking) {
        set_mario_anim_with_accel(m, MARIO_ANIM_TIPTOE, (s32)(speed / 2.0f * 0x10000));
    } else if (sSprinting) {
        set_mario_anim_with_accel(m, MARIO_ANIM_RUNNING, (s32)(speed / 5.0f * 0x10000));
    } else {
        set_mario_anim_with_accel(m, MARIO_ANIM_WALKING, (s32)(speed / 3.5f * 0x10000));
    }
}

s32 act_bloco_ground(struct MarioState *m) {
    if (mario_floor_is_steep(m)) {
        return set_mario_action(m, ACT_BEGIN_SLIDING, 0);
    }
    // um toque rapido no pulo vale ate o proximo tique de fisica
    if (m->controller->buttonPressed & A_BUTTON) {
        sJumpQueued = TRUE;
    }
    if (run_ticks(m, TRUE)) {
        sFallStartY = m->pos[1];
        m->action = ACT_BLOCO_AIR;
        m->actionTimer = 0;
        play_sound(SOUND_ACTION_TERRAIN_JUMP, m->marioObj->header.gfx.cameraToObject);
        return TRUE;
    }

    m->vel[0] = sMoveX * TICK_TO_FRAME;
    m->vel[1] = 0.0f;
    m->vel[2] = sMoveZ * TICK_TO_FRAME;

    // agachado: nao anda para fora da beirada (testa cada eixo, como no estilo original)
    if (gVoxelSneaking) {
        if (m->vel[0] != 0 && floor_drop_too_far(m, m->pos[0] + m->vel[0] * 1.5f, m->pos[2])) {
            m->vel[0] = 0; sVx = 0; sMoveX = 0;
        }
        if (m->vel[2] != 0 && floor_drop_too_far(m, m->pos[0], m->pos[2] + m->vel[2] * 1.5f)) {
            m->vel[2] = 0; sVz = 0; sMoveZ = 0;
        }
        if (floor_drop_too_far(m, m->pos[0] + m->vel[0] * 1.5f, m->pos[2] + m->vel[2] * 1.5f)) {
            m->vel[0] = m->vel[2] = 0; sVx = sVz = sMoveX = sMoveZ = 0;
        }
    }

    f32 speed = sqrtf(m->vel[0] * m->vel[0] + m->vel[2] * m->vel[2]);
    m->forwardVel = speed;
    face_movement(m, m->vel[0], m->vel[2]);

    switch (perform_ground_step(m)) {
        case GROUND_STEP_LEFT_GROUND:
            sFallStartY = m->pos[1];
            sVy = 0;
            m->action = ACT_BLOCO_AIR;
            m->actionTimer = 0;
            return TRUE;
        case GROUND_STEP_HIT_WALL:
            slide_along_wall(m);
            break;
    }
    set_locomotion_anim(m, speed);
    return FALSE;
}

s32 act_bloco_air(struct MarioState *m) {
    sJumpQueued = FALSE;
    m->vel[0] = sMoveX * TICK_TO_FRAME;
    m->vel[1] = sMoveY * TICK_TO_FRAME;
    m->vel[2] = sMoveZ * TICK_TO_FRAME;
    m->forwardVel = sqrtf(m->vel[0] * m->vel[0] + m->vel[2] * m->vel[2]);
    face_movement(m, m->vel[0], m->vel[2]);

    f32 velYBefore = m->vel[1];
    switch (perform_air_step(m, 0)) {
        case AIR_STEP_LANDED: {
            // dano de queda: acima de 3 blocos; 20 de vida <-> 8 fatias de vida
            f32 fall = (sFallStartY - m->pos[1]) / UNITS_PER_BLOCK;
            if (fall > 3.0f && !(m->flags & MARIO_METAL_CAP)) {
                s32 hp = (s32) ceilf(fall - 3.0f);
                s32 wedges = (hp * 8 + 19) / 20;
                m->hurtCounter += wedges * 4;
                play_sound(SOUND_MARIO_ATTACKED, m->marioObj->header.gfx.cameraToObject);
            } else {
                play_sound(SOUND_ACTION_TERRAIN_LANDING, m->marioObj->header.gfx.cameraToObject);
            }
            sVy = 0;
            m->vel[1] = 0;
            m->action = ACT_BLOCO_GROUND;
            m->actionTimer = 0;
            return TRUE;
        }
        case AIR_STEP_HIT_WALL:
            slide_along_wall(m);
            break;
    }
    // bateu a cabeca no teto
    if (velYBefore > 0 && m->vel[1] <= 0) {
        sVy = 0;
        sMoveY = 0;
    }
    if (m->pos[1] > sFallStartY) {
        sFallStartY = m->pos[1];
    }
    run_ticks(m, FALSE);
    set_mario_animation(m, sMoveY > 0 ? MARIO_ANIM_SINGLE_JUMP : MARIO_ANIM_GENERAL_FALL);
    return FALSE;
}

// troca as acoes de locomocao do Mario pelas novas (chamado no laco de acoes)
void voxel_move_convert(struct MarioState *m) {
    if (!gVoxelMoveEnabled) {
        return;
    }
    u32 a = m->action;
    s32 ground = FALSE, air = FALSE;
    switch (a) {
        case ACT_IDLE: case ACT_PANTING: case ACT_WALKING: case ACT_DECELERATING: case ACT_BRAKING:
        case ACT_BRAKING_STOP: case ACT_TURNING_AROUND: case ACT_FINISH_TURNING_AROUND:
        case ACT_STANDING_AGAINST_WALL: case ACT_JUMP_LAND_STOP: case ACT_DOUBLE_JUMP_LAND_STOP:
        case ACT_FREEFALL_LAND_STOP: case ACT_SIDE_FLIP_LAND_STOP: case ACT_TRIPLE_JUMP_LAND_STOP:
        case ACT_BACKFLIP_LAND_STOP: case ACT_LONG_JUMP_LAND_STOP: case ACT_JUMP_LAND: case ACT_FREEFALL_LAND:
        case ACT_DOUBLE_JUMP_LAND: case ACT_SIDE_FLIP_LAND: case ACT_TRIPLE_JUMP_LAND: case ACT_BACKFLIP_LAND:
        case ACT_LONG_JUMP_LAND: case ACT_CROUCHING: case ACT_START_CROUCHING: case ACT_STOP_CROUCHING:
        case ACT_CRAWLING: case ACT_START_CRAWLING: case ACT_STOP_CRAWLING: case ACT_LAVA_BOOST_LAND:
            ground = TRUE;
            break;
        case ACT_FREEFALL: case ACT_JUMP: case ACT_DOUBLE_JUMP: case ACT_TRIPLE_JUMP: case ACT_BACKFLIP:
        case ACT_SIDE_FLIP: case ACT_LONG_JUMP: case ACT_WALL_KICK_AIR: case ACT_TOP_OF_POLE_JUMP:
            air = TRUE;
            break;
    }
    if (!ground && !air) {
        return;
    }
    // velocidade do jogo (unidades/frame) -> blocos/tique
    sVx = sMoveX = m->vel[0] / TICK_TO_FRAME;
    sVz = sMoveZ = m->vel[2] / TICK_TO_FRAME;
    if (ground) {
        sVx = sVz = sMoveX = sMoveZ = 0;
        if (a == ACT_WALKING || a == ACT_DECELERATING) {
            sVx = sMoveX = sins(m->faceAngle[1]) * m->forwardVel / TICK_TO_FRAME;
            sVz = sMoveZ = coss(m->faceAngle[1]) * m->forwardVel / TICK_TO_FRAME;
        }
        sVy = sMoveY = 0;
    } else {
        sVy = sMoveY = m->vel[1] / TICK_TO_FRAME;
        sFallStartY = m->pos[1];
    }
    m->prevAction = a;
    m->action = ground ? ACT_BLOCO_GROUND : ACT_BLOCO_AIR;
    m->actionState = 0;
    m->actionArg = 0;
    m->actionTimer = 0;
}
