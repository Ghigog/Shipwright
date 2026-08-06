/**
 * Seven Sages - Phase 6: Mask of Truth. While worn, chests and grottos in the current scene are
 * marked with a floating gem drawn through walls.
 *
 * Spec (docs/item-ability-overhaul.md, Masks, resolved 2026-08-05): reading (a) of three - world
 * markers on everything loaded in the current scene - widened from chests to include grottos,
 * everywhere, Hyrule Field's included. The other two readings (minimap dots; every chest in the
 * region regardless of load state) were priced and rejected: neither has any vanilla precedent to
 * build on, and the second needs static per-scene chest data that does not exist in this build.
 *
 * Enumeration is two actor-category walks and no new data at all:
 *
 *   - Chests are ACTORCAT_CHEST (En_Box is the category's only member).
 *   - Grottos are Door_Ana, which lives in ACTORCAT_ITEMACTION alongside other things, so that walk
 *     is filtered by actor id.
 *
 * The grotto half is the better half, and the reason is how vanilla hides them. A hidden grotto is
 * not spawned on reveal - it spawns with the room and sits in DoorAna_WaitClosed scaled to nothing
 * (`Actor_SetScale(&this->actor, 0)`, z_door_ana.c:73-86). So it is already in the actor list,
 * already at its final position, and simply invisible. Better still, the Song of Storms variants
 * (`!(params & 0x200)`) set ACTOR_FLAG_UPDATE_CULLING_DISABLED, so they keep updating at any
 * distance - exactly the Hyrule Field case, and exactly the grottos a player cannot otherwise find
 * without playing the song at every patch of dirt in the province. The bomb/hammer variants carry a
 * collider instead and are normally culled, but culling governs an actor's *update*, not its
 * presence in the list, so this walk still sees them.
 *
 * Two things the spec is explicit about, both honoured here:
 *
 *   - Hidden and revealed grottos are marked IDENTICALLY. A different marker for "still hidden"
 *     would leak how it opens, which is a hint the mask was never meant to give.
 *   - Opened chests keep their marker. The spec says positions, and a player who already looted a
 *     chest is not harmed by seeing where it was.
 *
 * Chests and grottos do get different colours. That distinguishes two kinds of thing the player can
 * already tell apart on sight once they arrive, so it leaks nothing, and it stops a field full of
 * grotto markers from reading as a field full of chests.
 *
 * The mask's two vanilla behaviours are untouched: gossip stones still read, and the Deku Scrub
 * reaction still runs through its own VB_DEKU_SCRUBS_REACT_TO_MASK_OF_TRUTH hook.
 */
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

// Enough for any real room. A scene with more marked objects than this loses the overflow rather
// than growing the per-frame draw unboundedly; the biggest vanilla rooms are nowhere near it.
constexpr int32_t MAX_MARKERS = 64;

// Height above the actor's origin. Chests and Door_Ana both have their origin on the ground, so one
// offset suits both - high enough to clear a big chest's lid, low enough to still read as "this
// spot" rather than floating free of it.
constexpr float MARKER_HEIGHT = 70.0f;

constexpr float MARKER_HALF_WIDTH = 16.0f;
constexpr float MARKER_HALF_HEIGHT = 26.0f;

// Vertices are baked out to this, so the scale applied per marker is relative to it. Same trick as
// SevenSagesAoeField.cpp's icosphere and draw_ico_sphere in z_eff_ss_solder_srch_ball.c.
constexpr int16_t MARKER_MESH_EXTENT = 128;

constexpr uint8_t MARKER_ALPHA = 170;

constexpr Color_RGBA8 CHEST_COLOR = { 255, 205, 80, 255 };
constexpr Color_RGBA8 GROTTO_COLOR = { 120, 255, 160, 255 };

struct Marker {
    Vec3f pos;
    Color_RGBA8 color;
};

// Baked matrix backing each marker's draw command, one per slot. A fixed array rather than a
// std::vector for the reason SevenSagesAoeField.cpp gives: the display list stores raw Mtx*
// pointers into this, and a vector reallocating mid-frame would dangle them before the list is
// even submitted.
Mtx sMarkerMtx[MAX_MARKERS];

// Not in gbi.h. The colour variant of the vertex literal (SevenSagesAoeField.cpp defines the normal
// variant for the same reason): these markers are drawn with lighting off, so SHADE comes from the
// vertex colours below and gives the gem its top-lit ramp with no light source involved.
#define gdSPDefVtxC(x, y, z, r, g, b, a) \
    { .v = { .ob = { x, y, z }, .flag = 0, .tc = { 0, 0 }, .cn = { r, g, b, a } } }

std::vector<Vtx> sMarkerVtx;
std::vector<Gfx> sMarkerGfx;
bool sMarkerMeshBuilt = false;

// An octahedron: apex, four points around the equator, nadir. Eight triangles, emitted as three
// vertices each so the display list is a flat run of vertex-then-triangle pairs with no index
// bookkeeping - the same shape SevenSagesAoeField.cpp's sphere builder produces.
void BuildMarkerMesh() {
    if (sMarkerMeshBuilt) {
        return;
    }

    const int16_t e = MARKER_MESH_EXTENT;
    // Brightest at the top, dimmest underneath, so the gem reads as a solid object rather than a
    // flat silhouette even with no lighting and no texture.
    const Vtx apex = gdSPDefVtxC(0, e, 0, 255, 255, 255, 255);
    const Vtx nadir = gdSPDefVtxC(0, -e, 0, 90, 90, 90, 255);
    const Vtx equator[4] = {
        gdSPDefVtxC(e, 0, 0, 190, 190, 190, 255),
        gdSPDefVtxC(0, 0, e, 190, 190, 190, 255),
        gdSPDefVtxC(-e, 0, 0, 190, 190, 190, 255),
        gdSPDefVtxC(0, 0, -e, 190, 190, 190, 255),
    };

    sMarkerVtx.reserve(24);
    for (int32_t i = 0; i < 4; i++) {
        const Vtx& a = equator[i];
        const Vtx& b = equator[(i + 1) % 4];
        // Upper cap, then lower cap with the equator pair reversed so both wind the same way out.
        sMarkerVtx.push_back(apex);
        sMarkerVtx.push_back(a);
        sMarkerVtx.push_back(b);
        sMarkerVtx.push_back(nadir);
        sMarkerVtx.push_back(b);
        sMarkerVtx.push_back(a);
    }

    for (size_t i = 0; i < sMarkerVtx.size(); i += 3) {
        sMarkerGfx.push_back(gsSPVertex((uintptr_t)(sMarkerVtx.data() + i), 3, 0));
        sMarkerGfx.push_back(gsSP1Triangle(0, 1, 2, 0));
    }
    sMarkerGfx.push_back(gsSPEndDisplayList());
    sMarkerMeshBuilt = true;
}

bool IsWearingMaskOfTruth() {
    Player* player = GET_PLAYER(gPlayState);
    return player != nullptr && player->currentMask == PLAYER_MASK_TRUTH;
}

void CollectMarkers(std::vector<Marker>& markers) {
    for (Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_CHEST].head;
         actor != NULL && markers.size() < (size_t)MAX_MARKERS; actor = actor->next) {
        markers.push_back({ actor->world.pos, CHEST_COLOR });
    }

    for (Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_ITEMACTION].head;
         actor != NULL && markers.size() < (size_t)MAX_MARKERS; actor = actor->next) {
        // ACTORCAT_ITEMACTION is a mixed bag - Door_Ana shares it with dropped-item and effect
        // actors - so unlike the chest walk this one has to filter.
        if (actor->id == ACTOR_DOOR_ANA) {
            markers.push_back({ actor->world.pos, GROTTO_COLOR });
        }
    }
}

} // namespace

// Deliberately at file scope rather than inside the anonymous namespace above, for the linkage
// reason SevenSagesAoeField.cpp documents at length: OPEN_DISPS/CLOSE_DISPS embed a forward
// declaration of FrameInterpolation_RecordOpenChild/CloseChild, and a bare declaration textually
// inside an anonymous namespace gets internal linkage, so the call fails to link.
void SevenSagesMaskOfTruthDraw() {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == nullptr || !IsWearingMaskOfTruth()) {
        return;
    }

    std::vector<Marker> markers;
    CollectMarkers(markers);
    if (markers.empty()) {
        return;
    }

    BuildMarkerMesh();

    static std::vector<Gfx> dl;
    dl.clear();

    // One slow revolution keeps the marker legible from any angle - the octahedron's silhouette is
    // narrow edge-on - and the bob stops a row of them reading as scenery. Driven from
    // gameplayFrames rather than a counter of our own so it stays in step with the paused game.
    const float spin = gPlayState->gameplayFrames * 0.03f;
    const float bob = Math_SinS((s16)(gPlayState->gameplayFrames * 900)) * 4.0f;

    for (size_t i = 0; i < markers.size(); i++) {
        const Marker& marker = markers[i];

        Matrix_Push();
        Matrix_Translate(marker.pos.x, marker.pos.y + MARKER_HEIGHT + bob, marker.pos.z, MTXMODE_NEW);
        Matrix_RotateY(spin, MTXMODE_APPLY);
        Matrix_Scale(MARKER_HALF_WIDTH / MARKER_MESH_EXTENT, MARKER_HALF_HEIGHT / MARKER_MESH_EXTENT,
                     MARKER_HALF_WIDTH / MARKER_MESH_EXTENT, MTXMODE_APPLY);
        Matrix_ToMtx(&sMarkerMtx[i], (char*)__FILE__, __LINE__);
        Matrix_Pop();

        dl.push_back(gsDPSetPrimColor(0, 0, marker.color.r, marker.color.g, marker.color.b, MARKER_ALPHA));
        dl.push_back(gsSPMatrix(&sMarkerMtx[i], G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_PUSH));
        dl.push_back(gsSPDisplayList(sMarkerGfx.data()));
        dl.push_back(gsSPPopMatrix(G_MTX_MODELVIEW));
    }

    OPEN_DISPS(gPlayState->state.gfxCtx);

    // No Z_CMP and no G_ZBUFFER: the whole point is seeing a grotto through the hill it is buried
    // in. Otherwise this is SevenSagesAoeField.cpp's self-contained XLU state, with lighting off -
    // SHADE is the vertex colour ramp baked into the mesh, and the two Ad slots read PRIMITIVE so
    // the alpha set above is actually the marker's opacity.
    gSPLoadGeometryMode(POLY_XLU_DISP++, G_SHADE | G_SHADING_SMOOTH);
    gSPTexture(POLY_XLU_DISP++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gDPPipeSync(POLY_XLU_DISP++);
    gDPSetCycleType(POLY_XLU_DISP++, G_CYC_1CYCLE);
    gDPSetRenderMode(POLY_XLU_DISP++,
                     IM_RD | CVG_DST_FULL | FORCE_BL | ZMODE_XLU | GBL_c1(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_1MA),
                     IM_RD | CVG_DST_FULL | FORCE_BL | ZMODE_XLU | GBL_c2(G_BL_CLR_IN, G_BL_A_IN, G_BL_CLR_MEM, G_BL_1MA));
    gDPSetCombineLERP(POLY_XLU_DISP++, PRIMITIVE, 0, SHADE, 0, 0, 0, 0, PRIMITIVE, PRIMITIVE, 0, SHADE, 0, 0, 0, 0,
                      PRIMITIVE);

    dl.push_back(gsSPEndDisplayList());
    gSPDisplayList(POLY_XLU_DISP++, dl.data());

    CLOSE_DISPS(gPlayState->state.gfxCtx);
}

static void RegisterSevenSagesMaskOfTruth() {
    COND_HOOK(OnPlayDrawEnd, IS_RANDO, SevenSagesMaskOfTruthDraw);
}

static RegisterShipInitFunc sevenSagesMaskOfTruthInitFunc(RegisterSevenSagesMaskOfTruth, { "IS_RANDO" });
