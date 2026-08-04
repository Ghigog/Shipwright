#include "soh/Enhancements/SevenSages/SevenSagesAoeField.h"

#include <cmath>
#include <tuple>
#include <vector>

#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/frame_interpolation.h"

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
// Baked matrix backing each field's sphere draw command, one per slot. A fixed array rather than a
// std::vector: the draw list stores raw Mtx* pointers into this, and a vector reallocating mid-frame
// would leave those pointers dangling before the display list is even submitted.
Mtx sFieldMtx[MAX_FIELDS];

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

// The growing-radius indicator used to be a burst of EffectSsKiraKira sparkles traced around the
// field's edge every growth frame. That put up to 24 spawns a frame through EffectSs_Spawn's shared,
// priority-ranked pool (EffectSs_FindSlot, z_effect_soft_sprite.c) - when the pool is full, a spawn
// there doesn't queue, it evicts whatever's sitting in a lower-priority slot, so a field spawning
// near any other action in the room (hit sparks, dust puffs, item glitter) would visibly eat them.
// It also only lasted the sparkle's own ~16 frame life, well short of the field's actual lifetime.
//
// Real geometry sidesteps both problems: it doesn't touch the EffectSs pool at all, and it is drawn
// directly from field.radius every frame for as long as the field is active, so it can never drift
// from the collider and never disappears early. The mesh is an icosphere built once (same
// construction as the collision debug viewer's collider-shape overlay, colViewer.cpp
// CreateSphereData) and instanced per field by translate+scale, the same trick draw_ico_sphere in
// z_eff_ss_solder_srch_ball.c uses for the guard vision sphere.
constexpr int16_t SPHERE_MESH_RADIUS = 128; // vertices are baked out to this radius; scale is relative to it
constexpr uint8_t SPHERE_ALPHA = 100;       // translucent placeholder - visual polish is a follow-up
// Frames the sphere takes to ease back out to nothing at the end of the field's life, same duration
// as the grow-in (GROW_FRAMES). Only affects what's drawn (SevenSagesAoeFieldDraw's drawRadius), not
// field.radius or the collider - the hitbox holds full size until the field is actually released.
constexpr int32_t SHRINK_FRAMES = GROW_FRAMES;

// Not in gbi.h - same helpers colViewer.cpp defines locally for the same reason (a plain
// position+normal Vtx literal, no texture coordinates needed for an untextured prim-colour sphere).
#define qs105(n) ((int16_t)((n) * 0x0020))
#define gdSPDefVtxN(x, y, z, s, t, nx, ny, nz, ca) \
    { .n = { .ob = { x, y, z }, .tc = { qs105(s), qs105(t) }, .n = { nx, ny, nz }, .a = ca } }

std::vector<Vtx> sSphereVtx;
std::vector<Gfx> sSphereGfx;
bool sSphereBuilt = false;

// One subdivision of each icosahedron face into 4, on the unit sphere: http://blog.andreaskahler.com
// /2009/06/creating-icosphere-mesh-in-code.html. Mirrors colViewer.cpp's CreateSphereFace/
// CreateSphereData; duplicated rather than shared because neither is exposed outside that .cpp.
void AddSphereFace(std::vector<std::tuple<size_t, size_t, size_t>>& faces, int32_t v0Index, int32_t v1Index,
                    int32_t v2Index) {
    size_t nextIndex = sSphereVtx.size();
    size_t v01Index = nextIndex;
    size_t v12Index = nextIndex + 1;
    size_t v20Index = nextIndex + 2;

    faces.emplace_back(v0Index, v01Index, v20Index);
    faces.emplace_back(v1Index, v12Index, v01Index);
    faces.emplace_back(v2Index, v20Index, v12Index);
    faces.emplace_back(v01Index, v12Index, v20Index);

    const Vtx& v0 = sSphereVtx[v0Index];
    const Vtx& v1 = sSphereVtx[v1Index];
    const Vtx& v2 = sSphereVtx[v2Index];
    Vec3f mid[3] = {
        { (v0.n.ob[0] + v1.n.ob[0]) / 2.0f, (v0.n.ob[1] + v1.n.ob[1]) / 2.0f, (v0.n.ob[2] + v1.n.ob[2]) / 2.0f },
        { (v1.n.ob[0] + v2.n.ob[0]) / 2.0f, (v1.n.ob[1] + v2.n.ob[1]) / 2.0f, (v1.n.ob[2] + v2.n.ob[2]) / 2.0f },
        { (v2.n.ob[0] + v0.n.ob[0]) / 2.0f, (v2.n.ob[1] + v0.n.ob[1]) / 2.0f, (v2.n.ob[2] + v0.n.ob[2]) / 2.0f },
    };
    for (Vec3f& v : mid) {
        float mag = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        v.x /= mag;
        v.y /= mag;
        v.z /= mag;
        sSphereVtx.push_back(gdSPDefVtxN((short)(v.x * 127), (short)(v.y * 127), (short)(v.z * 127), 0, 0,
                                          (signed char)(v.x * 127), (signed char)(v.y * 127), (signed char)(v.z * 127),
                                          0xFF));
    }
}

void BuildSphereMesh() {
    if (sSphereBuilt) {
        return;
    }

    Vec3f base[12];
    float d = (1.0f + sqrtf(5.0f)) / 2.0f;
    base[0] = { -1, d, 0 };  base[1] = { 1, d, 0 };   base[2] = { -1, -d, 0 };  base[3] = { 1, -d, 0 };
    base[4] = { 0, -1, d };  base[5] = { 0, 1, d };   base[6] = { 0, -1, -d };  base[7] = { 0, 1, -d };
    base[8] = { d, 0, -1 };  base[9] = { d, 0, 1 };   base[10] = { -d, 0, -1 }; base[11] = { -d, 0, 1 };
    for (Vec3f& v : base) {
        float mag = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        v.x /= mag;
        v.y /= mag;
        v.z /= mag;
        sSphereVtx.push_back(gdSPDefVtxN((short)(v.x * 128), (short)(v.y * 128), (short)(v.z * 128), 0, 0,
                                          (signed char)(v.x * 127), (signed char)(v.y * 127), (signed char)(v.z * 127),
                                          0xFF));
    }

    std::vector<std::tuple<size_t, size_t, size_t>> faces;
    AddSphereFace(faces, 0, 11, 5);  AddSphereFace(faces, 0, 5, 1);   AddSphereFace(faces, 0, 1, 7);
    AddSphereFace(faces, 0, 7, 10);  AddSphereFace(faces, 0, 10, 11); AddSphereFace(faces, 1, 5, 9);
    AddSphereFace(faces, 5, 11, 4);  AddSphereFace(faces, 11, 10, 2); AddSphereFace(faces, 10, 7, 6);
    AddSphereFace(faces, 7, 1, 8);   AddSphereFace(faces, 3, 9, 4);   AddSphereFace(faces, 3, 4, 2);
    AddSphereFace(faces, 3, 2, 6);   AddSphereFace(faces, 3, 6, 8);   AddSphereFace(faces, 3, 8, 9);
    AddSphereFace(faces, 4, 9, 5);   AddSphereFace(faces, 2, 4, 11);  AddSphereFace(faces, 6, 2, 10);
    AddSphereFace(faces, 8, 6, 7);   AddSphereFace(faces, 9, 8, 1);

    size_t vtxStart = sSphereVtx.size();
    sSphereVtx.reserve(vtxStart + faces.size() * 3);
    for (size_t i = 0; i < faces.size(); i++) {
        sSphereVtx.push_back(sSphereVtx[std::get<0>(faces[i])]);
        sSphereVtx.push_back(sSphereVtx[std::get<1>(faces[i])]);
        sSphereVtx.push_back(sSphereVtx[std::get<2>(faces[i])]);
        sSphereGfx.push_back(gsSPVertex((uintptr_t)(sSphereVtx.data() + vtxStart + i * 3), 3, 0));
        sSphereGfx.push_back(gsSP1Triangle(0, 1, 2, 0));
    }
    sSphereGfx.push_back(gsSPEndDisplayList());
    sSphereBuilt = true;
}

// The one-shot part of the impact: a ground ring for horizontal extent, and Din's Fire's own
// particle at the centre for a burning patch's character, on top of the persistent sphere drawn in
// SevenSagesAoeFieldDraw. Both spawns here are one class each rather than a burst, so they're a much
// smaller draw on the shared EffectSs pool than the old shell ring was.
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

// Draws every active field as a translucent sphere sized directly from field.radius, so the visual
// is exactly the collider's current size, for the collider's entire lifetime, with no separate timer
// of its own to fall out of sync. Runs on OnPlayDrawEnd rather than through EffectSs, so it never
// touches the shared particle pool.
//
// Deliberately kept outside the anonymous namespace above: OPEN_DISPS/CLOSE_DISPS (macros.h) embed a
// forward declaration of FrameInterpolation_RecordOpenChild/CloseChild, and a bare declaration
// textually inside an anonymous namespace gets internal linkage - a copy distinct from the real
// symbol in frame_interpolation.cpp - so calling those macros from in there fails to link. Every
// other file in this codebase that calls OPEN_DISPS (nametag.cpp, colViewer.cpp) does so from
// ordinary file-scope functions for the same reason. Everything this function touches
// (sFields, sFieldMtx, FieldColors, BuildSphereMesh, sSphereGfx) is still reachable - anonymous
// namespace members are visible from the rest of this translation unit, just not from other ones.
void SevenSagesAoeFieldDraw() {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == nullptr) {
        return;
    }

    BuildSphereMesh();

    static std::vector<Gfx> dl;
    dl.clear();

    bool any = false;
    for (int32_t i = 0; i < MAX_FIELDS; i++) {
        AoeField& field = sFields[i];
        if (!field.active || field.owner != gPlayState || field.visual == SEVEN_SAGES_AOE_VISUAL_NONE ||
            field.radius <= 0.0f) {
            continue;
        }
        any = true;

        Color_RGBA8* prim;
        Color_RGBA8* env;
        FieldColors(field.visual, &prim, &env);

        // Mirrors the grow-in over the field's last SHRINK_FRAMES, so the sphere eases out instead
        // of popping away the instant the collider is released. Purely cosmetic: it scales what's
        // drawn, not field.radius, so the actual damage volume holds at full size for the field's
        // entire real lifetime and only the indicator tapers off at the very end.
        float drawRadius = field.radius;
        if (field.framesLeft <= SHRINK_FRAMES) {
            drawRadius = field.radius * (field.framesLeft / (float)SHRINK_FRAMES);
        }

        Matrix_Push();
        Matrix_Translate(field.pos.x, field.pos.y, field.pos.z, MTXMODE_NEW);
        Matrix_Scale(drawRadius / SPHERE_MESH_RADIUS, drawRadius / SPHERE_MESH_RADIUS,
                     drawRadius / SPHERE_MESH_RADIUS, MTXMODE_APPLY);
        Matrix_ToMtx(&sFieldMtx[i], (char*)__FILE__, __LINE__);
        Matrix_Pop();

        dl.push_back(gsDPSetPrimColor(0, 0, prim->r, prim->g, prim->b, SPHERE_ALPHA));
        dl.push_back(gsDPSetEnvColor(env->r, env->g, env->b, 255));
        dl.push_back(gsSPMatrix(&sFieldMtx[i], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_PUSH));
        dl.push_back(gsSPDisplayList(sSphereGfx.data()));
        dl.push_back(gsSPPopMatrix(G_MTX_MODELVIEW));
    }

    if (!any) {
        return;
    }

    OPEN_DISPS(gPlayState->state.gfxCtx);

    // Fully self-contained state, based on EffectSsSolderSrchBall_Draw's guard-vision sphere: no
    // texture, alpha-blended XLU, PRIMITIVE lerped with SHADE so the icosphere's vertex normals give
    // it some roundness under the scene's existing lighting instead of reading as a flat disc.
    //
    // That effect's combine formula outputs alpha from ENVIRONMENT rather than PRIMITIVE, which is
    // fine for it (always fully opaque, env alpha unused) but wrong here - it made the sphere render
    // fully solid no matter what SPHERE_ALPHA was set to, since PRIMITIVE's alpha channel was never
    // read. The two Ad slots below read PRIMITIVE instead, so the alpha set in gsDPSetPrimColor above
    // is actually the sphere's opacity.
    gSPLoadGeometryMode(POLY_XLU_DISP++, G_ZBUFFER | G_SHADE | G_LIGHTING);
    gSPTexture(POLY_XLU_DISP++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPPipeSync(POLY_XLU_DISP++);
    gDPSetCycleType(POLY_XLU_DISP++, G_CYC_1CYCLE);
    gDPSetRenderMode(POLY_XLU_DISP++, Z_CMP | IM_RD | CVG_DST_FULL | FORCE_BL | ZMODE_XLU | GBL_c1(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_1MA),
                     Z_CMP | IM_RD | CVG_DST_FULL | FORCE_BL | ZMODE_XLU | GBL_c2(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_1MA));
    gDPSetCombineLERP(POLY_XLU_DISP++, PRIMITIVE, 0, SHADE, 0, 0, 0, 0, PRIMITIVE, PRIMITIVE, 0, SHADE, 0, 0, 0, 0,
                      PRIMITIVE);

    dl.push_back(gsSPEndDisplayList());
    gSPDisplayList(POLY_XLU_DISP++, dl.data());

    CLOSE_DISPS(gPlayState->state.gfxCtx);
}

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
        field.active = true;
        SpawnFieldBurst(play, field.pos, visual);
        return;
    }
    // Pool full: drop the request rather than displace a live field. Eight concurrent fields is
    // already well past anything the spec asks for, so silently reusing one would hide a bug.
}

static void RegisterSevenSagesAoeField() {
    COND_HOOK(OnGameFrameUpdate, IS_RANDO, SevenSagesAoeFieldFrameUpdate);
    COND_HOOK(OnPlayDrawEnd, IS_RANDO, SevenSagesAoeFieldDraw);
}

static RegisterShipInitFunc sevenSagesAoeFieldInitFunc(RegisterSevenSagesAoeField, { "IS_RANDO" });
