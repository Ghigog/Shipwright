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
    s32 emitFrame; // counts up through the growth phase, picks which shell directions to emit
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

// 24 directions over a sphere: 6 axes, 8 cube diagonals, 10 more spread between them.
const float kShell[][3] = {
    {  1.000f,  0.000f,  0.000f }, { -1.000f,  0.000f,  0.000f },
    {  0.000f,  1.000f,  0.000f }, {  0.000f, -1.000f,  0.000f },
    {  0.000f,  0.000f,  1.000f }, {  0.000f,  0.000f, -1.000f },
    {  0.577f,  0.577f,  0.577f }, { -0.577f,  0.577f,  0.577f },
    {  0.577f, -0.577f,  0.577f }, { -0.577f, -0.577f,  0.577f },
    {  0.577f,  0.577f, -0.577f }, { -0.577f,  0.577f, -0.577f },
    {  0.577f, -0.577f, -0.577f }, { -0.577f, -0.577f, -0.577f },
    {  0.707f,  0.707f,  0.000f }, { -0.707f,  0.707f,  0.000f },
    {  0.707f, -0.707f,  0.000f }, { -0.707f, -0.707f,  0.000f },
    {  0.707f,  0.000f,  0.707f }, { -0.707f,  0.000f,  0.707f },
    {  0.000f,  0.707f,  0.707f }, {  0.000f,  0.707f, -0.707f },
    {  0.000f, -0.707f,  0.707f }, {  0.000f, -0.707f, -0.707f },
};
constexpr int32_t SHELL_POINTS = (int32_t)(sizeof(kShell) / sizeof(kShell[0]));

// Emitted per growth frame rather than all at once. The whole shell every frame would be 24 x 6 =
// 144 sprites against a game-wide pool of 85 (EffectSs_InitInfo(play, 0x55) in z_play.c), which
// would starve every hit mark and dust puff in the scene. Four per frame traces the expansion
// instead, and the eye integrates the sweep into a sphere.
constexpr int32_t SHELL_EMIT_PER_FRAME = 4;

// Scale and life are calibrated against vanilla: the call vanilla itself names "SpawnSmallYellow"
// passes scale 1000, Demo_Kekkai passes 3000. life feeds alphaStep as -(255/life)*2, so 32 stays
// legible for roughly 16 frames - long enough to judge a radius by, which 16 was not.
constexpr int16_t SHELL_SCALE = 2000;
constexpr int32_t SHELL_LIFE = 32;

void SpawnShellRing(PlayState* play, const Vec3f& pos, float radius, SevenSagesAoeVisual visual, s32 emitFrame) {
    if (visual == SEVEN_SAGES_AOE_VISUAL_NONE || radius <= 0.0f) {
        return;
    }
    Color_RGBA8* prim;
    Color_RGBA8* env;
    FieldColors(visual, &prim, &env);

    Vec3f zero = { 0.0f, 0.0f, 0.0f };
    for (int32_t i = 0; i < SHELL_EMIT_PER_FRAME; i++) {
        const auto& dir = kShell[(emitFrame * SHELL_EMIT_PER_FRAME + i) % SHELL_POINTS];
        Vec3f p = { pos.x + radius * dir[0], pos.y + radius * dir[1], pos.z + radius * dir[2] };
        EffectSsKiraKira_SpawnDispersed(play, &p, &zero, &zero, prim, env, SHELL_SCALE, SHELL_LIFE);
    }
}

// The one-shot part: a ground ring for horizontal extent, and Din's Fire's own particle at the
// centre for a burning patch's character. EffectSsDFire_Spawn takes no colour at all - its palette
// is baked in - which is why the shell above uses KiraKira and not this.
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

        // Emitted while the field is still expanding, at the radius it currently has, so the
        // sparkles trace the growth instead of marking the final size before it exists.
        if (field.emitFrame < GROW_FRAMES) {
            SpawnShellRing(gPlayState, field.pos, field.radius, field.visual, field.emitFrame);
            field.emitFrame++;
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

// Uses vanilla's own coloured-shockwave call rather than hand-rolled EffectSsBlast_Spawn
// parameters. The first attempt passed scaleStep 16 and scaleStepDecay 0 where every vanilla caller
// passes 375 and 35 (z_effect_soft_sprite_old_init.c:341), so the ring barely grew - and since the
// effect is gEffShockwaveDL, a flat ground ring, a barely-grown one viewed edge-on from a wall hit
// is invisible. Proven parameters first; tune only if it renders and is the wrong size.
//
// Ring particles are placed around the field's edge rather than one at the centre, because a single
// ground ring cannot show a field that reaches as far above and below the impact point as this one
// does.
void SevenSagesSpawnAoeField(PlayState* play, float x, float y, float z, float maxRadius, float height,
                             int32_t lifetimeFrames, uint32_t damageFlags, uint8_t damage,
                             SevenSagesAoeVisual visual) {
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
        field.growthPerFrame = maxRadius / GROW_FRAMES;
        field.height = (s16)height;
        field.framesLeft = lifetimeFrames;
        field.owner = play;
        field.visual = visual;
        field.emitFrame = 0;
        field.active = true;
        SpawnFieldBurst(play, field.pos, visual);
        return;
    }
    // Pool full: drop the request rather than displace a live field. Eight concurrent fields is
    // already well past anything the spec asks for, so silently reusing one would hide a bug.
}

static void RegisterSevenSagesAoeField() {
    COND_HOOK(OnGameFrameUpdate, IS_RANDO, SevenSagesAoeFieldFrameUpdate);
}

static RegisterShipInitFunc sevenSagesAoeFieldInitFunc(RegisterSevenSagesAoeField, { "IS_RANDO" });
