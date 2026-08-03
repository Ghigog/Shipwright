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

void SevenSagesSpawnAoeField(PlayState* play, float x, float y, float z, float maxRadius, float height,
                             int32_t lifetimeFrames, uint32_t damageFlags) {
    if (play == nullptr || lifetimeFrames <= 0 || maxRadius <= 0.0f) {
        return;
    }

    for (AoeField& field : sFields) {
        if (field.active) {
            continue;
        }

        sFieldColliderInit.info.toucher.dmgFlags = damageFlags;
        Collider_InitCylinder(play, &field.collider);
        Collider_SetCylinder(play, &field.collider, nullptr, &sFieldColliderInit);

        field.pos = { x, y, z };
        field.maxRadius = maxRadius;
        field.radius = 0.0f;
        field.growthPerFrame = maxRadius / GROW_FRAMES;
        field.height = (s16)height;
        field.framesLeft = lifetimeFrames;
        field.owner = play;
        field.active = true;
        return;
    }
    // Pool full: drop the request rather than displace a live field. Eight concurrent fields is
    // already well past anything the spec asks for, so silently reusing one would hide a bug.
}

static void RegisterSevenSagesAoeField() {
    COND_HOOK(OnGameFrameUpdate, IS_RANDO, SevenSagesAoeFieldFrameUpdate);
}

static RegisterShipInitFunc sevenSagesAoeFieldInitFunc(RegisterSevenSagesAoeField, { "IS_RANDO" });
