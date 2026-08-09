/**
 * Seven Sages - Phase 6: blue fire freezes what stands near it, burns until re-bottled, melts red
 * ice reliably, and imbues arrows shot through it.
 *
 * Spec (docs/item-ability-overhaul.md, "Blue Fire"): a dropped blue flame leaves an area that
 * applies an ice trap to anything nearby, and the patch persists and can be re-bottled.
 *
 * The freeze is a SevenSagesAoeField using the deku nut's damage flag with **0 damage** - vanilla's
 * own way of saying "the effect is the stun, not the hit" - so enemies caught near the flame lock
 * up rather than take chip damage. Same mechanism as the ice arrow, different source.
 *
 * **Re-bottling is not ours.** SoH already ships it as `RebottleBlueFire`
 * (`Enhancements/QoL/RebottleBlueFire.cpp`), which offers the flame as a bottle catch whenever the
 * player is in range. It has been switched on in the Seven Sages enhancements preset rather than
 * reimplemented. Note the preset lives inside `soh.o2r`, so enabling it needs `GenerateSohOtr`, not
 * just a build. The same goes for `FastBottles`, which is what removes the "you got Blue Fire!"
 * hold-up every time you scoop a flame (`z_player.c:14802`) - also a preset flag, not our code.
 *
 * **Every blue flame gets the aura, not only dropped ones.** The naturally placed flames in the Ice
 * Cavern and the Shadow Temple are the same actor, so they freeze things too. That is deliberate:
 * a blue flame behaving differently depending on where it came from would be the surprising
 * outcome, and the player can already bottle those flames and re-drop them anyway.
 *
 * The field is refreshed on a shared frame boundary rather than per flame. A field is a pooled
 * collider allocation, so spawning one every frame would churn the play arena for no benefit; a
 * 25-frame field renewed every 20 frames overlaps enough to read as continuous, and stops within
 * about a second of the flame being bottled or destroyed.
 *
 * ---------------------------------------------------------------------------------------------
 * ## Why the red ice melt is ours and not vanilla's
 *
 * Vanilla melts red ice by winning a **single-slot** hit record. `BgIceShelter_Idle`
 * (`z_bg_ice_shelter.c:330`) reads `cylinder1.base.acFlags & AC_HIT`, clears that flag
 * unconditionally, and only then asks whether `cylinder1.base.ac->id == ACTOR_EN_ICE_HONO`. One
 * slot, one writer, and a wrong writer costs the frame outright.
 *
 * Our own freeze field was that wrong writer, deterministically:
 *
 *   1. `BlueFireArrows` (on in the Seven Sages seed preset) adds `AC_TYPE_PLAYER` to `cylinder1`
 *      so ice arrows can hit red ice (`Items/BlueFireArrows.cpp:19`).
 *   2. The freeze field is `AT_ON | AT_TYPE_PLAYER` with dmgFlags `0x00000001`, and `cylinder1`'s
 *      bumper is `0xFFCFFFFF`. Type matches, bit 0 is shared, so the field hits red ice.
 *   3. The field's collider is built with a **null actor** (`SevenSagesAoeField.cpp:183`), and
 *      `CollisionCheck_SetATvsAC` does `ac->ac = at->actor` (`z_collision_check.c:1742`). So it
 *      sets `AC_HIT` and writes `base.ac = NULL`.
 *   4. It always writes last. The frame order is `CollisionCheck_AT -> ClearContext ->
 *      Actor_UpdateAll` (`z_play.c:1180`); the field submits from `OnGameFrameUpdate`, after
 *      `Play_Update` has returned, so it lands after every actor in `colAT`.
 *
 * Net effect: the field set `AC_HIT`, the ice consumed it, saw `ac == NULL`, and **threw away the
 * flame's genuine hit for that frame**.
 *
 * That produced a symptom that reads as a radius bug and is not one. The field grows 150/6 = 25
 * units per frame; the flame's own melt collider is `scale.x * 6000`, about 27-44
 * (`z_en_ice_hono.c:283`). Ice a few units past the cylinder edge is inside the field on its very
 * first frame, so every frame is blocked and it never melts. Ice ~40 units out is missed by the
 * field's first frame but caught by the flame's 44, so one unblocked frame gets through. Melting
 * survived only in the band the field had not grown into yet - the outer edge - which is exactly
 * the "works at the edge, fails up close" the bug was reported as. Widening the flame's collider
 * would have made it strictly worse, since a bigger collider sits even further inside the field
 * that is suppressing it.
 *
 * So the melt is decided here by **proximity**, not by the collider record. It cannot be stolen,
 * it does not care which AT collider won the frame, and the reach is a number in this file rather
 * than an emergent product of two scale-interpolation curves.
 *
 * **Both VB handlers here are additive - they only ever set `true`.** An earlier draft also set
 * `false` to stop a null-actor hit from eating `AC_HIT`, which looked like the tidy fix for the
 * steal itself. It is not safe: `BlueFireArrows` registers on the same two VB ids, several
 * registrations share one `bool*` (`GameInteractor_Hooks.cpp:270`), and the order between
 * `RegisterShipInitFunc`s is not something either file states. A handler that can veto would have
 * silently broken the ice-arrow melt whenever it happened to run last. Additive handlers compose
 * regardless of order, which is the convention every other VB handler in the tree follows.
 *
 * Nothing is lost by that, because the arrow path and the field never contend in the same frame:
 * the field only exists where a flame is, and a flame near red ice now melts it by proximity
 * anyway. The broader hazard - a null-actor collider writing `base.ac = NULL` into every AC it
 * touches, for every Seven Sages AoE feature - is deliberately NOT fixed here. Giving the field a
 * real owner actor would fix it at the source, but a field outlives its spawner by up to 5 frames,
 * so the collider would hold a dangling `Actor*` into freed arena memory. That needs its own
 * design pass, not a drive-by.
 *
 * ## Persistence
 *
 * A dropped flame is held mid-burn instead of counting down. `EnIceHono_SpreadFlames` reads its
 * timer at three thresholds (`z_en_ice_hono.c:266`): `> 20` keeps growing toward full size, `< 25`
 * starts fading alpha, `< 40` submits the melt collider. A timer parked in **[25, 39]** therefore
 * means grown, unfaded, and permanently able to melt - the flame simply burns. Releasing the clamp
 * hands the actor straight back to its own countdown, so it shrinks and fades over about 30 frames
 * on its own, with no special-case death path.
 *
 * The clamp is released the moment the flame has a parent, which is what `Player_Action_SwingBottle`
 * sets when the catch actually happens (`z_player.c:14851`) - not when the catch is merely offered.
 * So a flame you walk past keeps burning and a flame you scoop fades out.
 *
 * ## Arrow imbue
 *
 * An arrow flown through a flame becomes an ice arrow: `params = ARROW_ICE` plus the matching
 * toucher dmgFlag. Vanilla does the rest - `EnArrow_Update` spawns the `ACTOR_ARROW_ICE` child for
 * any arrow whose params are in the elemental range and whose child is null
 * (`z_en_arrow.c:456`), which is also precisely the condition `BlueFireArrows::CheckAC` tests, so
 * an imbued arrow melts red ice and breaks the Ice Cavern / King Zora walls with no extra wiring.
 *
 * The test is **swept**, against the segment from the arrow's previous position to its current one.
 * A flying arrow covers far more ground per frame than a flame is wide, so a per-frame point test
 * would tunnel straight through the fire and miss most shots.
 */
#include "soh/Enhancements/SevenSages/SevenSagesAoeField.h"

#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

#include <cmath>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "overlays/actors/ovl_Bg_Ice_Shelter/z_bg_ice_shelter.h"
#include "overlays/actors/ovl_En_Arrow/z_en_arrow.h"
#include "overlays/actors/ovl_En_Ice_Hono/z_en_ice_hono.h"

void EnArrow_Fly(EnArrow* thisx, PlayState* play);
void EnIceHono_SpreadFlames(EnIceHono* thisx, PlayState* play);
}

extern "C" PlayState* gPlayState;

namespace {

constexpr float BLUE_FIRE_RADIUS = 150.0f;
constexpr float BLUE_FIRE_HEIGHT = 150.0f; // total, centred

// Renew every REFRESH frames with a field that lives slightly longer, so there is no gap between
// one field expiring and the next appearing.
constexpr uint32_t BLUE_FIRE_REFRESH_FRAMES = 20;
constexpr int32_t BLUE_FIRE_FIELD_FRAMES = 25;

constexpr uint8_t BLUE_FIRE_DAMAGE = 0; // the effect is the freeze, not a hit

// EnIceHono params: -1 is a capturable flame (the fixed ones in the Ice Cavern and Shadow Temple),
// 0 is a flame the player dropped from a bottle, 1 and 2 are the short-lived sparks that a landing
// flame throws off (`z_en_ice_hono.c:152`).
constexpr int16_t FLAME_PARAMS_CAPTURABLE = -1;
constexpr int16_t FLAME_PARAMS_DROPPED = 0;

// Anywhere in [25, 39] holds the flame grown, unfaded and melt-capable; see the header. 30 sits
// clear of both edges and of 46, the frame `SpreadFlames` uses to throw its second burst of sparks.
constexpr int16_t FLAME_HELD_TIMER = 30;

// --- Red ice -----------------------------------------------------------------------------------

// Copied from `BgIceShelter_InitColliders` (`z_bg_ice_shelter.c:80`), indexed by RedIceType. The
// melt test is measured from the same cylinder vanilla uses so the reach reads consistently across
// the five wildly different red ice sizes.
constexpr int16_t sRedIceRadii[] = { 47, 33, 44, 41, 100 };
constexpr int16_t sRedIceHeights[] = { 80, 54, 90, 60, 200 };

// Reach past the ice's own cylinder. Vanilla's effective extra reach was the flame's melt collider
// at its peak, about 44, and only for the ~30 frames it was submitted at all. 80 against a
// permanently burning flame is deliberately forgiving: dropping fire near red ice should melt it,
// full stop, rather than being a placement puzzle.
constexpr float MELT_REACH_XZ = 80.0f;

// Vertical slack above and below the ice's own cylinder. Wide enough that a flame on a sloped or
// stepped floor still counts, tight enough that one on the floor above does not melt through.
constexpr float MELT_REACH_Y = 60.0f;

int32_t RedIceType(BgIceShelter* ice) {
    return (ice->dyna.actor.params >> 8) & 7;
}

// Is a blue flame close enough to this block to melt it? Only player-dropped flames count.
// Including the fixed capturable flames would melt the red ice that shares a room with them in the
// Ice Cavern the instant the room loads, which is a puzzle the player is meant to solve by
// carrying fire to it.
bool DroppedFlameNearIce(BgIceShelter* ice) {
    const int32_t type = RedIceType(ice);
    if (type >= (int32_t)ARRAY_COUNT(sRedIceRadii)) {
        return false;
    }

    const Vec3f& icePos = ice->dyna.actor.world.pos;
    const float reachXZ = sRedIceRadii[type] + MELT_REACH_XZ;

    for (Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_ITEMACTION].head; actor != nullptr;
         actor = actor->next) {
        if (actor->id != ACTOR_EN_ICE_HONO || actor->params != FLAME_PARAMS_DROPPED) {
            continue;
        }

        const float dx = actor->world.pos.x - icePos.x;
        const float dz = actor->world.pos.z - icePos.z;
        if (SQ(dx) + SQ(dz) > SQ(reachXZ)) {
            continue;
        }

        // The ice's cylinder runs upward from the actor's own position, so the band is asymmetric.
        const float dy = actor->world.pos.y - icePos.y;
        if (dy < -MELT_REACH_Y || dy > sRedIceHeights[type] + MELT_REACH_Y) {
            continue;
        }

        return true;
    }

    return false;
}

// --- Arrow imbue -------------------------------------------------------------------------------

// How close the arrow's flight path has to pass to the centre of a flame. The flame's own visual is
// roughly this wide at full size.
constexpr float IMBUE_RADIUS = 70.0f;

// Vertical half-band, measured from the flame's position, which sits at its base.
constexpr float IMBUE_REACH_Y = 70.0f;

// EnArrow's dmgFlags[] table is indexed by ArrowType directly (`z_en_arrow.c:83`, read at
// `z_en_arrow.c:186`). ARROW_ICE is index 4. That table is only consulted during Init, so an arrow
// imbued in flight has to be given the flag explicitly or it keeps hitting enemies as a plain one.
constexpr uint32_t ICE_ARROW_DMG_FLAG = 0x00001000;

// Squared distance from `p` to the segment `a`->`b`. Used rather than a point test because an
// arrow travels much further per frame than a flame is wide.
float DistSqToSegment(const Vec3f& p, const Vec3f& a, const Vec3f& b) {
    const Vec3f ab = { b.x - a.x, b.y - a.y, b.z - a.z };
    const Vec3f ap = { p.x - a.x, p.y - a.y, p.z - a.z };
    const float lenSq = SQ(ab.x) + SQ(ab.y) + SQ(ab.z);

    float t = 0.0f;
    if (lenSq > 0.0f) {
        t = (ap.x * ab.x + ap.y * ab.y + ap.z * ab.z) / lenSq;
        t = CLAMP(t, 0.0f, 1.0f);
    }

    const float dx = ap.x - ab.x * t;
    const float dy = ap.y - ab.y * t;
    const float dz = ap.z - ab.z * t;
    return SQ(dx) + SQ(dy) + SQ(dz);
}

// Only plain arrows are convertible. An arrow that is already elemental keeps whatever the player
// paid magic for - a light arrow flown through a flame must stay a light arrow, since light arrows
// are required to finish a seed (see SevenSagesElementalArrows.cpp).
bool IsPlainArrow(int16_t params) {
    return params == ARROW_NORMAL || params == ARROW_NORMAL_LIT || params == ARROW_NORMAL_HORSE ||
           params == ARROW_NORMAL_SILENT;
}

bool ArrowPassedThroughFlame(EnArrow* arrow) {
    const Vec3f& to = arrow->actor.world.pos;
    const Vec3f& from = arrow->actor.prevPos;

    for (Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_ITEMACTION].head; actor != nullptr;
         actor = actor->next) {
        if (actor->id != ACTOR_EN_ICE_HONO) {
            continue;
        }
        if (actor->params != FLAME_PARAMS_DROPPED && actor->params != FLAME_PARAMS_CAPTURABLE) {
            continue;
        }

        // Cheap vertical reject before the segment maths.
        const float lowest = (from.y < to.y) ? from.y : to.y;
        const float highest = (from.y < to.y) ? to.y : from.y;
        if (highest < actor->world.pos.y - IMBUE_REACH_Y || lowest > actor->world.pos.y + IMBUE_REACH_Y) {
            continue;
        }

        if (DistSqToSegment(actor->world.pos, from, to) <= SQ(IMBUE_RADIUS)) {
            return true;
        }
    }

    return false;
}

// --- Hooks -------------------------------------------------------------------------------------

void SevenSagesBlueFireUpdate(void* actorPtr) {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == nullptr || actorPtr == nullptr) {
        return;
    }

    EnIceHono* flame = static_cast<EnIceHono*>(actorPtr);

    // Hold a dropped flame mid-burn. `parent` is set only once the bottle catch actually happens,
    // so scooping it releases the clamp and it fades out under its own countdown.
    //
    // Restricted to the burning state on purpose. The falling state (`EnIceHono_DropFlame`) has its
    // own 200-frame timer that is the only thing killing a flame which never lands, so clamping
    // there would leave a flame dropped into a pit falling and allocated forever.
    if (flame->actor.params == FLAME_PARAMS_DROPPED && flame->actionFunc == EnIceHono_SpreadFlames &&
        flame->actor.parent == nullptr && flame->timer < FLAME_HELD_TIMER) {
        flame->timer = FLAME_HELD_TIMER;
    }

    if ((gPlayState->gameplayFrames % BLUE_FIRE_REFRESH_FRAMES) != 0) {
        return;
    }

    const Vec3f& pos = flame->actor.world.pos;
    SevenSagesSpawnAoeField(gPlayState, pos.x, pos.y, pos.z, BLUE_FIRE_RADIUS, BLUE_FIRE_HEIGHT, BLUE_FIRE_FIELD_FRAMES,
                            SEVEN_SAGES_AOE_DMG_STUN, BLUE_FIRE_DAMAGE, SEVEN_SAGES_AOE_VISUAL_NONE);
}

void SevenSagesArrowImbueUpdate(void* actorPtr) {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == nullptr || actorPtr == nullptr) {
        return;
    }

    EnArrow* arrow = static_cast<EnArrow*>(actorPtr);

    // Only while actually flying. A nocked arrow sits on the bow inside the flame's radius if the
    // player stands over their own fire, and converting there would let one flame imbue every shot
    // without ever passing through it.
    if (arrow->actionFunc != EnArrow_Fly || !IsPlainArrow(arrow->actor.params)) {
        return;
    }

    if (!ArrowPassedThroughFlame(arrow)) {
        return;
    }

    // Explicit narrowing: params is s16 and ArrowType is a plain enum, which MSVC treats as C4244.
    // Windows CI builds with /WX, so an implicit conversion here is a fatal error there and a
    // silent success locally.
    arrow->actor.params = static_cast<int16_t>(ARROW_ICE);
    arrow->collider.info.toucher.dmgFlags = ICE_ARROW_DMG_FLAG;
    Audio_PlayActorSound2(&arrow->actor, NA_SE_EV_ICE_FREEZE);
}

} // namespace

static void RegisterSevenSagesBlueFire() {
    COND_ID_HOOK(OnActorUpdate, ACTOR_EN_ICE_HONO, IS_SEVENSAGES, SevenSagesBlueFireUpdate);
    COND_ID_HOOK(OnActorUpdate, ACTOR_EN_ARROW, IS_SEVENSAGES, SevenSagesArrowImbueUpdate);

    // Melt on proximity rather than on the single-slot hit record. Both halves are needed: vanilla
    // only reaches the MELT question if HIT said yes, and it clears AC_HIT on the way past.
    COND_VB_SHOULD(VB_BG_ICE_SHELTER_HIT, IS_SEVENSAGES, {
        BgIceShelter* ice = va_arg(args, BgIceShelter*);

        if (DroppedFlameNearIce(ice)) {
            *should = true;
        }
    });

    COND_VB_SHOULD(VB_BG_ICE_SHELTER_MELT, IS_SEVENSAGES, {
        BgIceShelter* ice = va_arg(args, BgIceShelter*);

        if (DroppedFlameNearIce(ice)) {
            *should = true;
        }
    });
}

static RegisterShipInitFunc sevenSagesBlueFireInitFunc(RegisterSevenSagesBlueFire, { "IS_RANDO" });
