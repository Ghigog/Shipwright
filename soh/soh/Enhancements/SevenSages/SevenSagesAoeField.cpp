#include "soh/Enhancements/SevenSages/SevenSagesAoeField.h"

#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
}

extern "C" PlayState* gPlayState;

namespace {

constexpr int32_t MAX_FIELDS = 8;

// Frames from nothing to full size. The bomb grows its blast by a similar handful of frames; the
// point is that the expansion is visible rather than the field popping into existence at full size.
constexpr int32_t GROW_FRAMES = 6;

// Copied from Din's Fire (z_magic_fire.c). AT_TYPE_PLAYER because these are player-caused effects,
// so enemies treat them as player damage and Link is not hit by his own field. dmgFlags is patched
// per spawn from the caller's value.
ColliderCylinderInit sFieldColliderInit = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_PLAYER,
        AC_NONE,
        OC1_NONE,
        OC2_TYPE_1,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK0,
        { 0x00020000, 0x00, 0x01 },
        { 0x00000000, 0x00, 0x00 },
        TOUCH_ON | TOUCH_SFX_NONE,
        BUMP_NONE,
        OCELEM_NONE,
    },
    { 9, 9, 0, { 0, 0, 0 } },
};

struct AoeField {
    bool active;
    ColliderCylinder collider;
    Vec3f pos;
    f32 maxRadius;
    f32 radius;
    f32 growthPerFrame;
    s16 height;
    s32 framesLeft;
    SevenSagesAoeVisual visual;
    // The PlayState the collider was initialised against. Colliders allocate from the play arena,
    // so a field must not survive a scene change - see the check in the frame update.
    PlayState* owner;
};

AoeField sFields[MAX_FIELDS];

void ReleaseField(AoeField& field) {
    if (!field.active) {
        return;
    }
    if (field.owner != nullptr) {
        Collider_DestroyCylinder(field.owner, &field.collider);
    }
    field.active = false;
    field.owner = nullptr;
}

void ReleaseAllFields() {
    for (AoeField& field : sFields) {
        ReleaseField(field);
    }
}

void FieldColors(SevenSagesAoeVisual visual, Color_RGBA8** prim, Color_RGBA8** env) {
    static Color_RGBA8 firePrim = { 255, 200, 60, 255 };
    static Color_RGBA8 fireEnv = { 255, 80, 0, 255 };
    static Color_RGBA8 icePrim = { 170, 230, 255, 255 };
    static Color_RGBA8 iceEnv = { 40, 120, 255, 255 };
    const bool isFire = (visual == SEVEN_SAGES_AOE_VISUAL_FIRE);
    *prim = isFire ? &firePrim : &icePrim;
    *env = isFire ? &fireEnv : &iceEnv;
}

// The whole visual, since the persistent radius sphere was removed 2026-08-07 for looking bad: a
// ground ring at the impact point, and Din's Fire's own particle at the centre for a burning patch's
// character. One spawn each rather than a burst, so this is a light draw on the shared EffectSs pool.
// The field's actual extent is deliberately not drawn any more - see SevenSagesAoeField.h.
void SpawnFieldBurst(PlayState* play, const Vec3f& pos, SevenSagesAoeVisual visual) {
    if (visual == SEVEN_SAGES_AOE_VISUAL_NONE) {
        return;
    }
    Color_RGBA8* prim;
    Color_RGBA8* env;
    FieldColors(visual, &prim, &env);

    Vec3f zero = { 0.0f, 0.0f, 0.0f };
    Vec3f centre = pos;
    EffectSsBlast_SpawnShockwave(play, &centre, &zero, &zero, prim, env, 10);
    if (visual == SEVEN_SAGES_AOE_VISUAL_FIRE) {
        EffectSsDFire_SpawnFixedScale(play, &centre, &zero, &zero, 255, 8);
    }
}

void SevenSagesAoeFieldFrameUpdate() {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == nullptr) {
        ReleaseAllFields();
        return;
    }

    for (AoeField& field : sFields) {
        if (!field.active) {
            continue;
        }

        // A scene change reuses the arena the collider was allocated from, so a field that outlived
        // its PlayState is dropped rather than updated. Destroying it against the new PlayState
        // would be worse than leaking it.
        if (field.owner != gPlayState) {
            field.active = false;
            field.owner = nullptr;
            continue;
        }

        if (field.framesLeft <= 0) {
            ReleaseField(field);
            continue;
        }
        field.framesLeft--;

        if (field.radius < field.maxRadius) {
            field.radius += field.growthPerFrame;
            if (field.radius > field.maxRadius) {
                field.radius = field.maxRadius;
            }
        }

        field.collider.dim.radius = (s16)field.radius;
        field.collider.dim.height = field.height;
        // A cylinder spans [pos.y + yShift, pos.y + yShift + height] (sys_math3d.c:1615), so with
        // the default yShift of 0 it only ever extends UPWARD from the impact point. That is right
        // for a bomb, which is always at your feet, and wrong for an arrow: one shot into a wall
        // above an enemy never reached back down to it. Centring on the impact point instead makes
        // the field reach equally up and down.
        field.collider.dim.yShift = (s16)(-field.height / 2);
        field.collider.dim.pos.x = (s16)field.pos.x;
        field.collider.dim.pos.y = (s16)field.pos.y;
        field.collider.dim.pos.z = (s16)field.pos.z;

        // Submitted every frame the field is alive. This lands after CollisionCheck_ClearContext
        // and before the next frame's CollisionCheck_AT - the same window Actor_UpdateAll submits
        // in - because OnGameFrameUpdate fires from GameState_Update after Play_Update returns.
        CollisionCheck_SetAT(gPlayState, &gPlayState->colChkCtx, &field.collider.base);
    }
}

} // namespace

// The burst uses vanilla's own coloured-shockwave call rather than hand-rolled EffectSsBlast_Spawn
// parameters. The first attempt passed scaleStep 16 and scaleStepDecay 0 where every vanilla caller
// passes 375 and 35 (z_effect_soft_sprite_old_init.c:341), so the ring barely grew - and since the
// effect is gEffShockwaveDL, a flat ground ring, a barely-grown one viewed edge-on from a wall hit
// is invisible. Proven parameters first; tune only if it renders and is the wrong size.
void SevenSagesSpawnAoeField(PlayState* play, float x, float y, float z, float maxRadius, float height,
                             int32_t lifetimeFrames, uint32_t damageFlags, uint8_t damage, SevenSagesAoeVisual visual) {
    if (play == nullptr || lifetimeFrames <= 0 || maxRadius <= 0.0f) {
        return;
    }

    for (AoeField& field : sFields) {
        if (field.active) {
            continue;
        }

        sFieldColliderInit.info.toucher.dmgFlags = damageFlags;
        sFieldColliderInit.info.toucher.damage = damage;
        Collider_InitCylinder(play, &field.collider);
        Collider_SetCylinder(play, &field.collider, nullptr, &sFieldColliderInit);

        field.pos = { x, y, z };
        field.maxRadius = maxRadius;
        field.radius = 0.0f;
        // Clamped to the lifetime, and this is load-bearing rather than defensive. A field is only
        // ever submitted to CollisionCheck from the update below, which grows it BEFORE submitting,
        // so a field that lives fewer frames than the grow-in takes never reaches its own radius: at
        // lifetimeFrames 1 it got exactly one submission at maxRadius/GROW_FRAMES. That is a sixth of
        // the documented size, and it is silent - the field still spawns, still hits whatever is
        // close enough, and only the reach is wrong.
        //
        // Found 2026-08-07 by reading, then confirmed in play: the Megaton Hammer's 120-unit
        // shockwave was landing as 20 and breaking no pots at all, and every thrown impact (also
        // lifetime 1) was a sixth of its tier's radius, which collapsed the whole size scale the
        // thrown-item feature exists to express.
        const int32_t growFrames = lifetimeFrames < GROW_FRAMES ? lifetimeFrames : GROW_FRAMES;
        field.growthPerFrame = maxRadius / growFrames;
        field.height = (s16)height;
        field.framesLeft = lifetimeFrames;
        field.owner = play;
        field.visual = visual;
        field.active = true;
        SpawnFieldBurst(play, field.pos, visual);
        return;
    }
    // Pool full: drop the request rather than displace a live field. Eight concurrent fields is
    // already well past anything the spec asks for, so silently reusing one would hide a bug.
}

static void RegisterSevenSagesAoeField() {
    COND_HOOK(OnGameFrameUpdate, IS_SEVENSAGES, SevenSagesAoeFieldFrameUpdate);
}

static RegisterShipInitFunc sevenSagesAoeFieldInitFunc(RegisterSevenSagesAoeField, { "IS_RANDO" });
