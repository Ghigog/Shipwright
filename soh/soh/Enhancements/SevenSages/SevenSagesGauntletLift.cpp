/**
 * Seven Sages - Phase 6: the gauntlets pick things up and throw them.
 *
 * Spec (docs/item-ability-overhaul.md, "Tunics, gauntlets, and boots"):
 *   - Silver Gauntlets - pick up and throw/destroy all basic rocks, and pick up small stunned
 *     enemies (stunned via Deku Nuts or other shock-capable new items).
 *   - Golden Gauntlets - pick up/throw *any* rock or *any* stunned enemy. A strict superset.
 *
 * **This is the half of the gauntlets that was never built.** The magic door bypass shipped on
 * 2026-08-03 (`SevenSagesGoldenGauntlets.cpp`) and is untouched by this file; the spec's "BUILT"
 * markers sat on the clause labelled *"Bonus, if feasible"*, and the headline lift/throw spec had
 * no code anywhere. This file is that code. The two files share nothing but the tier predicate,
 * which is small enough that duplicating it beats exporting a header for two lines - see
 * CanUseGauntlets below, which is a deliberate copy of the one over there.
 *
 * **What was already vanilla, and therefore is not here.** Two of the four rock tiers already
 * work without a line of new code:
 *   - Small grey rocks (`En_Ishi`, `params & 1 == ROCK_SMALL`) are liftable barehanded.
 *   - Large silver rocks (`En_Ishi`, `ROCK_LARGE`) already require Silver Gauntlets, gated in
 *     `z_player.c` (`Player_ActionHandler_2`: `id == ACTOR_EN_ISHI && (params & 0xF) == 1 &&
 *     strength < PLAYER_STR_SILVER_G` refuses the lift), and `En_Ishi` already implements the
 *     whole carry/throw/shatter state machine.
 *   - The heavy stone pillar (`Bg_Heavy_Block`) already requires Golden Gauntlets, likewise in
 *     `z_player.c`.
 * So "all basic rocks" and "any rock" only had to reach the two boulders vanilla never lets you
 * touch at all: `Obj_Bombiwa` (the brown bombable boulder) at the Silver tier and `Obj_Hamishi`
 * (the bronze hammer boulder) at the Golden tier. That mapping is the natural one - Silver takes
 * everything up to the bomb tier, Golden adds the hammer tier - and it keeps each tier's promise
 * strictly larger than the one below it. `Bg_Jya_Bombiwa`, the Spirit Temple bombable rock *wall*,
 * is deliberately excluded: it is scenery you break through, not an object with a position you can
 * carry somewhere.
 *
 * **"Stunned" is defined here as vanilla's blue colour filter.** There is no stun status field in
 * OoT; what every stun source shares is `Actor_SetColorFilter(actor, 0, ...)`, which sets
 * `colorFilterParams` with neither the white (0x8000) nor the red (0x4000) bit - the blue tint,
 * documented as such in z64actor.h. A deku nut stun, an ice arrow freeze and a Blue Fire field all
 * land there, and a plain damage flash (red) or a light-arrow hit (white) do not, which is exactly
 * the discrimination the spec wants. `freezeTimer` is accepted as a second, rarer source (Sun's
 * Song's `Actor_FreezeAllEnemies`, and the ice/0x80000 branches of `Actor_SetDropFlag`) because it
 * is literally "this actor is not allowed to act", which is a stun by any reading.
 *
 * **"Small" is defined as `colChkInfo.mass`,** vanilla's own weight number, with the boundary at
 * `MASS_HEAVY`. This falls out much better than it sounds: the light enemies are the flying and
 * fragile ones - Keese (30), Poe (40), Guay (30), Biri (30), Stingers and Octoroks (100),
 * Wallmasters and Floormasters (150) - and everything that plants its feet declares itself
 * `MASS_HEAVY` or `MASS_IMMOVABLE` in its own Init: Tektites, Stalchildren, Lizalfos, ReDeads,
 * Dodongos, Wolfos, Iron Knuckles, Dead Hands, Freezards, Like-Likes, Deku Babas, Deku Scrubs. So
 * Silver gets the little airborne things and Golden gets the heavyweights, which is what the spec
 * describes without ever having to keep a hand-written list of "small" actors up to date. Bosses
 * (`ACTORCAT_BOSS`) are excluded at both tiers - see the note on that below.
 *
 * **How the carry works, and why almost none of it is new code.** `Actor_OfferGetItem` with
 * `GI_NONE` is vanilla's own "Link may lift this" signal; it is what puts the Grab prompt on
 * screen and what `En_Ishi` uses. Once the player presses A, `func_8083A0F4` falls through to the
 * ordinary small-object carry (`Player_Action_80846050`), which attaches the actor and sets
 * `PLAYER_STATE1_CARRYING_ACTOR`, and `Player_PostLimbDrawGameplay` pins the held actor's world
 * position to Link's hand every frame. **None of that needed a change in z_player.c** - the only
 * strength gate there is the `En_Ishi` silver-rock one, and we are not touching `En_Ishi`.
 *
 * The one thing vanilla will not do for us is stop the actor's own update from fighting the carry.
 * `ObjHamishi_Update` rewrites `world.pos` toward `home.pos` every frame (its idle shake), which
 * would drag a carried boulder back to where it was standing, and a carried enemy would simply
 * keep attacking. So while this file owns an actor it holds `freezeTimer` up: z_actor.c skips
 * `actor->update` entirely while that is non-zero, but keeps drawing the actor and - because the
 * colour-filter countdown lives inside the same skipped branch - keeps the blue stun tint frozen
 * too, so a carried enemy still reads as stunned. That is also why the driver here is
 * `OnGameFrameUpdate` rather than `OnActorUpdate`: a frozen actor's per-actor hooks do not fire.
 *
 * **Nothing is remembered across a frame boundary without being re-validated.** The owned actor is
 * a raw pointer, and an actor can be killed and its memory reused, so every frame starts by
 * walking the live actor lists for that exact pointer and id. If it is gone - killed, or the scene
 * changed - ownership is simply dropped and nothing is dereferenced.
 *
 * **A thrown boulder shatters; a thrown enemy does not die.** The boulder case calls the actor's
 * own `_Break` effect and its own `VB_ROCK_DROP_ITEM` hook, so a shuffled boulder still pays out
 * its randomizer check exactly as bombing it would. The enemy case deliberately stops short of
 * inventing a kill: enemies are killed through their own damage handlers, and forcing health to 0
 * from outside skips death animations, drops and `OnEnemyDefeat`. Instead the landing spawns a
 * one-frame-ish AOE field (the mod's own primitive, see SevenSagesAoeField.h) carrying the deku
 * nut flag, which re-stuns the thrown enemy and stuns whatever it was thrown at, and does it
 * through each actor's real damage table. Throwing an enemy is therefore crowd control and
 * repositioning - over a ledge, into lava - not a new instant-kill.
 *
 * **Bosses are excluded** even though "any stunned enemy" would literally include them. A boss is
 * a scripted, often multi-actor fight whose phases assume the boss is where it put itself; picking
 * one up is at best comic and at worst a softlock in a seed's required dungeon. This is the one
 * place this file knowingly under-delivers against the spec text, and it is flagged in the spec.
 *
 * **No refusal feedback, deliberately.** When the tier is not met, no offer is made, so no Grab
 * prompt ever appears and the player sees exactly what vanilla shows for a rock they cannot lift.
 * The door bypass needed its `NA_SE_SY_ERROR` because there the prompt appeared anyway and the
 * refusal was invisible; here the absence of the prompt *is* the feedback.
 */
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/SevenSages/SevenSagesAoeField.h"
#include "soh/Enhancements/SevenSages/SevenSagesRoomAoe.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "overlays/actors/ovl_Obj_Bombiwa/z_obj_bombiwa.h"
#include "overlays/actors/ovl_Obj_Hamishi/z_obj_hamishi.h"
}

extern "C" PlayState* gPlayState;

// The two boulders' shatter effects. Both are file-scope-but-not-`static` in their own overlays,
// and SoH links every actor overlay into the one binary, so they can be called directly instead of
// being reimplemented here - the same trick ShuffleTrees.cpp and friends use for
// EnItem00_DrawRandomizedItem. Declared locally rather than added to the actors' headers so that
// no vanilla file has to change for this feature at all.
extern "C" void ObjBombiwa_Break(ObjBombiwa* boulder, PlayState* play);
extern "C" void ObjHamishi_Break(ObjHamishi* boulder, PlayState* play);

namespace {

constexpr u8 STRENGTH_SILVER_GAUNTLETS = 2;
constexpr u8 STRENGTH_GOLDEN_GAUNTLETS = 3;

// Deliberately a copy of SevenSagesGoldenGauntlets.cpp's predicate rather than a shared export.
// Same reasoning as over there: CUR_UPG_VALUE(UPG_STRENGTH) is a save value, not an equip state, so
// it survives an age change - without the age test a child who had been adult (or who was handed
// the upgrade by the debug console) would be hauling bronze boulders around barehanded.
bool CanUseGauntlets(u8 tier) {
    return LINK_IS_ADULT && CUR_UPG_VALUE(UPG_STRENGTH) >= tier;
}

// Reach for the Grab offer. The boulder numbers are the silver rock's (En_Ishi's large branch
// offers at 80/20) widened vertically, because both boulders sit with their origin above the floor
// Link stands on. Enemies get a taller box than either: a stunned Keese keeps hovering at whatever
// height it was stunned at, and 30 units would put most of them out of reach.
constexpr f32 BOULDER_RANGE_XZ = 80.0f;
constexpr f32 BOULDER_RANGE_Y = 40.0f;
constexpr f32 ENEMY_RANGE_XZ = 60.0f;
constexpr f32 ENEMY_RANGE_Y = 60.0f;

// Flight. Matches En_Ishi's large rock (gravity -2.5, floor -20) closely enough that a thrown
// boulder arcs like a thrown silver rock; the frame cap is a backstop for a throw that finds
// neither floor nor wall (off the edge of a cliff, into a bottomless pit) so ownership can never
// be held forever.
constexpr f32 THROWN_GRAVITY = -2.5f;
constexpr f32 THROWN_MIN_VELOCITY_Y = -20.0f;
constexpr s16 THROWN_MAX_FRAMES = 300;

// The landing shock for a thrown enemy. Small and short - it is an impact, not a spell.
constexpr f32 IMPACT_RADIUS = 60.0f;
constexpr f32 IMPACT_HEIGHT = 80.0f;
constexpr int32_t IMPACT_FRAMES = 6;
constexpr uint8_t IMPACT_DAMAGE = 0; // the effect is the stun, not the hit - see SevenSagesAoeField.h

// Held while the actor is in Link's hands or in flight. Any value above 1 survives the DECR that
// Actor_UpdateAll does before testing it, and this is re-applied every frame we own the actor.
constexpr u16 OWNED_FREEZE_TIMER = 5;

enum LiftPhase {
    LIFT_PHASE_NONE,
    LIFT_PHASE_HELD,
    LIFT_PHASE_FLYING,
};

// Link can only carry one thing, so one slot is the whole bookkeeping. The id is stored alongside
// the pointer purely so ActorIsStillLoaded can tell "still there" from "freed and a different
// actor was allocated over it".
Actor* sOwnedActor = nullptr;
s16 sOwnedActorId = 0;
LiftPhase sPhase = LIFT_PHASE_NONE;
s16 sFlyFrames = 0;

// Only the bits TakeOwnership actually turned on, so ReleaseOwnership can put the actor back exactly
// as it found it. Not a constant mask: 169 enemy overlays touch ACTOR_FLAG_UPDATE_CULLING_DISABLED,
// several of them setting it for their own reasons, and clearing it unconditionally would leave such
// an enemy permanently culled when vanilla wants it always-updating.
u32 sAddedFlags = 0;

bool IsBoulder(const Actor* actor) {
    return actor->id == ACTOR_OBJ_BOMBIWA || actor->id == ACTOR_OBJ_HAMISHI;
}

u8 RequiredTierForBoulder(const Actor* actor) {
    return actor->id == ACTOR_OBJ_HAMISHI ? STRENGTH_GOLDEN_GAUNTLETS : STRENGTH_SILVER_GAUNTLETS;
}

// See the header comment: blue colour filter, or an outright update freeze.
bool IsStunned(const Actor* actor) {
    return (actor->colorFilterTimer != 0 && (actor->colorFilterParams & 0xC000) == 0) || actor->freezeTimer != 0;
}

// See the header comment: mass is vanilla's own weight, and MASS_HEAVY is where it stops being a
// number and starts meaning "Link doesn't move this".
u8 RequiredTierForEnemy(const Actor* actor) {
    return actor->colChkInfo.mass >= MASS_HEAVY ? STRENGTH_GOLDEN_GAUNTLETS : STRENGTH_SILVER_GAUNTLETS;
}

// `update == NULL` is an actor that has been killed and is only waiting to be deleted; offering it
// would hand Link a corpse that vanishes out of his hands next frame.
bool EnemyIsLiftable(const Actor* actor) {
    return actor->category == ACTORCAT_ENEMY && actor->update != nullptr && IsStunned(actor) &&
           CanUseGauntlets(RequiredTierForEnemy(actor));
}

bool BoulderIsLiftable(const Actor* actor) {
    return IsBoulder(actor) && actor->update != nullptr && CanUseGauntlets(RequiredTierForBoulder(actor));
}

// The only two lists anything liftable by this feature can live in. Walking them is cheap (tens of
// entries) and is what makes it safe to keep a raw Actor* across frames at all.
bool ActorIsStillLoaded(const Actor* actor, s16 id) {
    static const int categories[] = { ACTORCAT_ENEMY, ACTORCAT_PROP };
    for (int category : categories) {
        for (Actor* it = gPlayState->actorCtx.actorLists[category].head; it != nullptr; it = it->next) {
            if (it == actor) {
                return it->id == id;
            }
        }
    }
    return false;
}

void TakeOwnership(Actor* actor) {
    sOwnedActor = actor;
    sOwnedActorId = actor->id;
    sPhase = LIFT_PHASE_HELD;
    sFlyFrames = 0;

    // THROW_ONLY makes the carry button always throw rather than set down gently, which is what
    // En_Ishi declares in its own ActorInit and what "pick up and throw" means. UPDATE_CULLING and
    // room -1 both mirror EnIshi_SetupLiftedUp: a carried actor follows Link out of the volume it
    // was spawned in, and must not be culled or room-despawned out of his hands.
    sAddedFlags = ~actor->flags & (ACTOR_FLAG_THROW_ONLY | ACTOR_FLAG_UPDATE_CULLING_DISABLED);
    actor->flags |= sAddedFlags;
    actor->room = -1;

    if (IsBoulder(actor)) {
        SoundSource_PlaySfxAtFixedWorldPos(gPlayState, &actor->world.pos, 20, NA_SE_PL_PULL_UP_BIGROCK);
    }
}

void ReleaseOwnership(Actor* actor) {
    if (actor != nullptr) {
        actor->freezeTimer = 0;
        // A boulder is killed immediately after this and never notices, but a thrown enemy survives
        // by design and would otherwise carry UPDATE_CULLING_DISABLED for the rest of its life -
        // updating at any distance, never despawning with its culling volume. En_Ishi leaves the same
        // flag set on release and gets away with it only because its rocks always shatter on landing.
        actor->flags &= ~sAddedFlags;
        // Back into the room Link is standing in now, not the one it was picked up from - carrying
        // something through a door and dropping it there should leave it there.
        actor->room = gPlayState->roomCtx.curRoom.num;
    }
    sOwnedActor = nullptr;
    sOwnedActorId = 0;
    sAddedFlags = 0;
    sPhase = LIFT_PHASE_NONE;
    sFlyFrames = 0;
}

// Player_Action_80846578 has already set world.rot.y, speedXZ and velocity.y on the way out of
// Link's hands, so all that is left is turning speedXZ into an XZ velocity - the same two lines
// EnIshi_SetupFly runs. Note this also covers the *involuntary* releases (Link takes a hit, walks
// into water): those detach with speed 0, so the actor drops straight down and lands at his feet,
// which is exactly what a dropped silver rock does in vanilla.
void BeginFlight(Actor* actor) {
    actor->velocity.x = Math_SinS(actor->world.rot.y) * actor->speedXZ;
    actor->velocity.z = Math_CosS(actor->world.rot.y) * actor->speedXZ;
    actor->gravity = THROWN_GRAVITY;
    actor->minVelocityY = THROWN_MIN_VELOCITY_Y;
    sPhase = LIFT_PHASE_FLYING;
    sFlyFrames = 0;
}

// Returns true on the frame the flight ends. The bg-check arguments are En_Ishi's.
bool StepFlight(Actor* actor) {
    actor->velocity.y += actor->gravity;
    if (actor->velocity.y < actor->minVelocityY) {
        actor->velocity.y = actor->minVelocityY;
    }
    Actor_UpdatePos(actor);
    Actor_UpdateBgCheckInfo(gPlayState, actor, 7.5f, 35.0f, 0.0f, 0xC5);

    sFlyFrames++;
    return (actor->bgCheckFlags & (BGCHECKFLAG_GROUND | BGCHECKFLAG_WALL)) || sFlyFrames > THROWN_MAX_FRAMES;
}

// A deliberate replay of the tail of ObjBombiwa_Update / ObjHamishi_Update's break branch, in the
// same order, because that branch is unreachable from here - it fires on an explosion or a hammer
// hit, and this boulder was thrown.
void ShatterBoulder(Actor* actor) {
    if (actor->id == ACTOR_OBJ_BOMBIWA) {
        // ObjBombiwa_Break scatters its fragments around home.pos, which is still the spot the
        // boulder was standing on before it was picked up - so without this the shards would appear
        // back where it came from rather than where it landed. ObjHamishi_Break already uses
        // world.pos and needs no equivalent.
        Math_Vec3f_Copy(&actor->home.pos, &actor->world.pos);
        ObjBombiwa_Break(reinterpret_cast<ObjBombiwa*>(actor), gPlayState);
    } else {
        ObjHamishi_Break(reinterpret_cast<ObjHamishi*>(actor), gPlayState);
    }

    Flags_SetSwitch(gPlayState, actor->params & 0x3F);
    SoundSource_PlaySfxAtFixedWorldPos(gPlayState, &actor->world.pos, 80, NA_SE_EV_WALL_BROKEN);
    if (actor->id == ACTOR_OBJ_BOMBIWA && ((actor->params >> 0xF) & 1) != 0) {
        Sfx_PlaySfxCentered(NA_SE_SY_CORRECT_CHIME);
    }
    // Not optional bookkeeping: this is the hook ShuffleRocks registers for, and it is how a
    // shuffled boulder pays out its randomizer check. Skipping it would silently eat a check for
    // any player who threw the boulder instead of bombing it. `false` matches both actors' own
    // calls - vanilla drops nothing from a boulder, the drop is entirely the randomizer's.
    GameInteractor_Should(VB_ROCK_DROP_ITEM, false, actor);
    Actor_Kill(actor);
}

// See the header comment for why this stops short of killing the enemy.
void ImpactEnemy(Actor* actor) {
    SoundSource_PlaySfxAtFixedWorldPos(gPlayState, &actor->world.pos, 20, NA_SE_PL_BODY_HIT);
    SevenSagesSpawnAoeField(gPlayState, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, IMPACT_RADIUS,
                            IMPACT_HEIGHT, IMPACT_FRAMES, SEVEN_SAGES_AOE_DMG_STUN, IMPACT_DAMAGE,
                            SEVEN_SAGES_AOE_VISUAL_NONE);
    // Zeroed so the enemy doesn't inherit the throw's momentum and skate away the moment its own
    // update resumes.
    actor->speedXZ = 0.0f;
    actor->velocity.x = 0.0f;
    actor->velocity.y = 0.0f;
    actor->velocity.z = 0.0f;
}

void OfferLifts(Player* player) {
    // Vanilla refuses the offer itself while the player is already carrying something, but there is
    // no point walking two actor lists to find that out.
    if (player->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) {
        return;
    }
    // Cheapest possible early out for the overwhelmingly common case: no gauntlets at all, or a
    // child. Silver is the lower of the two tiers, so failing it fails both.
    if (!CanUseGauntlets(STRENGTH_SILVER_GAUNTLETS)) {
        return;
    }

    // Actor_OfferGetItem does its own range and facing tests (and refuses outright in most of the
    // player states where a lift would be nonsense - swimming, climbing, mid-jump), so this only
    // has to decide *whether the item is liftable at all* and let vanilla decide *now*.
    SevenSagesForEachActorInRoom(gPlayState, ACTORCAT_PROP, [](Actor* actor) {
        if (BoulderIsLiftable(actor)) {
            Actor_OfferGetItem(actor, gPlayState, GI_NONE, BOULDER_RANGE_XZ, BOULDER_RANGE_Y);
        }
    });
    SevenSagesForEachActorInRoom(gPlayState, ACTORCAT_ENEMY, [](Actor* actor) {
        if (EnemyIsLiftable(actor)) {
            Actor_OfferGetItem(actor, gPlayState, GI_NONE, ENEMY_RANGE_XZ, ENEMY_RANGE_Y);
        }
    });
}

void SevenSagesGauntletLiftFrameUpdate() {
    if (gPlayState == nullptr || !GameInteractor::IsSaveLoaded(true)) {
        // Not a release - there is no live actor to hand back to. Just forget the pointer, and the
        // flags we would have restored along with it.
        sOwnedActor = nullptr;
        sOwnedActorId = 0;
        sAddedFlags = 0;
        sPhase = LIFT_PHASE_NONE;
        return;
    }

    // Re-validate before touching it. See the header comment: the actor may have been killed or the
    // whole scene swapped out since last frame.
    if (sOwnedActor != nullptr && !ActorIsStillLoaded(sOwnedActor, sOwnedActorId)) {
        sOwnedActor = nullptr;
        sOwnedActorId = 0;
        sAddedFlags = 0;
        sPhase = LIFT_PHASE_NONE;
    }

    if (sOwnedActor != nullptr) {
        Actor* actor = sOwnedActor;
        // Ownership *is* the freeze - see the header comment. Re-applied every frame rather than set
        // once, because Actor_UpdateAll decrements it.
        actor->freezeTimer = OWNED_FREEZE_TIMER;

        if (sPhase == LIFT_PHASE_HELD) {
            // Player_DetachHeldActor clears parent, and it is the one signal common to every way an
            // actor can leave Link's hands: a thrown throw, a hit taken, a scene-forced release.
            if (actor->parent == nullptr) {
                BeginFlight(actor);
            }
        } else if (sPhase == LIFT_PHASE_FLYING && StepFlight(actor)) {
            if (IsBoulder(actor)) {
                ReleaseOwnership(actor);
                ShatterBoulder(actor);
            } else {
                ImpactEnemy(actor);
                ReleaseOwnership(actor);
            }
        }
        return;
    }

    Player* player = GET_PLAYER(gPlayState);

    // Did the player just lift one of ours? heldActor is maintained by Player itself, so claiming it
    // here needs no bookkeeping on the offer side and cannot go stale. Nothing in vanilla lets Link
    // carry an ACTORCAT_ENEMY actor, so anything in his hands from that category is necessarily
    // ours; the cucco, the pots and the crates are all ACTORCAT_PROP and are matched by id, not
    // category, so they are never claimed.
    Actor* held = player->heldActor;
    if (held != nullptr && (IsBoulder(held) || held->category == ACTORCAT_ENEMY)) {
        TakeOwnership(held);
        return;
    }

    OfferLifts(player);
}

} // namespace

static void RegisterSevenSagesGauntletLift() {
    COND_HOOK(OnGameFrameUpdate, IS_RANDO, SevenSagesGauntletLiftFrameUpdate);
}

static RegisterShipInitFunc sevenSagesGauntletLiftInitFunc(RegisterSevenSagesGauntletLift, { "IS_RANDO" });
