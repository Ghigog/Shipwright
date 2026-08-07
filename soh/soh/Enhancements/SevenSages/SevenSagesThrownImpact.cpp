#include "soh/Enhancements/SevenSages/SevenSagesThrownImpact.h"
#include "soh/Enhancements/SevenSages/SevenSagesAoeField.h"

#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "overlays/actors/ovl_En_Ishi/z_en_ishi.h"
}

extern "C" PlayState* gPlayState;

namespace {

// See the header for why these are rows rather than numbers.
constexpr uint32_t DMG_FLAG_DEKU_STICK = 0x00000002;
constexpr uint32_t DMG_FLAG_EXPLOSIVE = 0x00000008;
constexpr uint32_t DMG_FLAG_HAMMER_SWING = 0x00000040;

// Below this the object was dropped, not thrown. Vanilla's throw leaves speedXZ around 6 (the value
// Player_Action_80846578 sets on the way out of Link's hands); an involuntary release detaches at 0.
constexpr f32 MIN_THROW_SPEED = 2.0f;

enum ThrownSize {
    THROWN_SMALL,
    THROWN_MEDIUM,
    THROWN_LARGE,
};

struct ImpactSpec {
    uint32_t dmgFlags;
    f32 radius;
    f32 height;
    uint8_t damage; // only read for targets with no damage table of their own
};

// Radii are deliberately below the hammer shockwave's: this is an object landing, not a weapon
// swing, and the small case should be barely wider than the thing that broke.
constexpr ImpactSpec sImpacts[] = {
    { DMG_FLAG_DEKU_STICK, 40.0f, 60.0f, 1 },
    { DMG_FLAG_HAMMER_SWING, 70.0f, 90.0f, 2 },
    { DMG_FLAG_EXPLOSIVE, 100.0f, 120.0f, 4 },
};

ThrownSize SizeOf(const Actor* actor) {
    switch (actor->id) {
        case ACTOR_EN_ISHI:
            // The one prop whose own params carry its size: bit 0 is ROCK_SMALL/ROCK_LARGE
            // (z_en_ishi.c:320). The large one is the silver-gauntlet rock.
            return (actor->params & 1) == ROCK_LARGE ? THROWN_MEDIUM : THROWN_SMALL;
        case ACTOR_OBJ_BOMBIWA:  // brown bombable boulder, liftable via SevenSagesGauntletLift
        case ACTOR_OBJ_HAMISHI:  // bronze boulder, likewise
        case ACTOR_BG_HEAVY_BLOCK: // the heavy stone pillar, vanilla's own golden-gauntlet lift
            return THROWN_LARGE;
        case ACTOR_EN_KUSA:   // bush
        case ACTOR_OBJ_TSUBO: // pot
        case ACTOR_OBJ_KIBAKO: // small crate
        case ACTOR_EN_NIW:    // cucco
            return THROWN_SMALL;
        default:
            break;
    }

    // Enemies, which is the case mass genuinely describes - the same split
    // SevenSagesGauntletLift.cpp uses to decide which gauntlet tier can lift one at all. MASS_HEAVY
    // is where a number stops being a weight and starts meaning "this does not get pushed around":
    // the light side is the flying and fragile enemies (Keese 30, Poe 40, Octorok 100), the heavy
    // side is everything that plants its feet.
    if (actor->category == ACTORCAT_ENEMY) {
        return actor->colChkInfo.mass >= MASS_HEAVY ? THROWN_MEDIUM : THROWN_SMALL;
    }

    // Anything else Link finds a way to carry. Small is the conservative answer, and matches what
    // vanilla's own carryables (all of them pot-sized) would deserve.
    return THROWN_SMALL;
}

// Link can carry exactly one thing, so one slot is the whole bookkeeping.
//
// Both pointers are only ever COMPARED, never dereferenced, once the actor has left Link's hands -
// the actor is handed back to us by the hook that fires for it, which is what guarantees it is still
// alive at the point we touch it. That is what makes it safe to hold a raw Actor* across frames here
// without the actor-list re-validation SevenSagesGauntletLift.cpp needs (it writes to the actor every
// frame; this does not).
Actor* sHeld = nullptr;
Actor* sFlying = nullptr;
// Backstop for a throw that never lands on anything we hear about - off a cliff, into a void, or
// simply culled out of existence mid-flight. Without it a stale pointer could be matched much later
// by a different actor allocated at the same address.
constexpr int32_t FLIGHT_WINDOW_FRAMES = 300;
int32_t sFlightFrames = 0;

void ForgetFlight() {
    sFlying = nullptr;
    sFlightFrames = 0;
}

// heldActor is maintained by Player itself, so watching it needs no cooperation from any of the
// actors involved and cannot go stale.
void SevenSagesThrownImpactPlayerUpdate() {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == nullptr) {
        sHeld = nullptr;
        ForgetFlight();
        return;
    }

    if (sFlightFrames > 0 && --sFlightFrames == 0) {
        ForgetFlight();
    }

    Player* player = GET_PLAYER(gPlayState);
    Actor* held = player->heldActor;

    if (held != nullptr) {
        sHeld = held;
        return;
    }

    if (sHeld != nullptr) {
        // It left his hands this frame. Whether that was a throw or a drop is decided at the
        // landing, from the object's own speed, rather than here - the throw's speed is set by
        // Player on the way out and is still on the actor when it lands.
        sFlying = sHeld;
        sFlightFrames = FLIGHT_WINDOW_FRAMES;
        sHeld = nullptr;
    }
}

// Fires after the actor's own update (z_actor.c:2692), so bgCheckFlags is this frame's. Covers the
// throwables that SURVIVE landing - the cucco, and anything a future item makes throwable.
void SevenSagesThrownImpactActorUpdate(void* actorPtr) {
    Actor* actor = static_cast<Actor*>(actorPtr);
    if (actor != sFlying || gPlayState == nullptr) {
        return;
    }
    if (actor->bgCheckFlags & (BGCHECKFLAG_GROUND | BGCHECKFLAG_WALL)) {
        SevenSagesThrownImpact(gPlayState, actor);
    }
}

// Covers the throwables that DESTROY themselves on landing, which is most of them: the pot, the
// bush, the crate and the rock all Actor_Kill inside their own thrown handler the moment they touch
// anything. Actor_Kill fires this hook before it clears update/draw (z_actor.c:1203), so the actor
// is still readable and still standing at the point of impact.
void SevenSagesThrownImpactActorKill(void* actorPtr) {
    Actor* actor = static_cast<Actor*>(actorPtr);
    if (actor != sFlying || gPlayState == nullptr) {
        return;
    }
    SevenSagesThrownImpact(gPlayState, actor);
}

} // namespace

void SevenSagesThrownImpact(PlayState* play, Actor* actor) {
    if (play == nullptr || actor == nullptr) {
        return;
    }

    // Whoever got here first wins, and the other route must not fire a second field on the same
    // landing. SevenSagesGauntletLift.cpp flies its own boulders and enemies and calls this directly
    // the moment they land, while the tracker below is still holding the same actor as in flight;
    // clearing it here is what keeps that from spawning a duplicate a frame or two later. It also
    // covers the reverse - a boulder that reaches us through Actor_Kill has stopped flying by
    // definition.
    if (actor == sFlying) {
        ForgetFlight();
    }

    if (actor->speedXZ < MIN_THROW_SPEED) {
        return;
    }

    const ImpactSpec& spec = sImpacts[SizeOf(actor)];
    // lifetimeFrames 1: an impact is instantaneous. Visual NONE because every throwable already
    // shows its own landing - shards, fragments, a boulder's break effect - and the field's own
    // radius indicator was removed for looking bad (SevenSagesAoeField.h).
    SevenSagesSpawnAoeField(play, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, spec.radius, spec.height,
                            1, spec.dmgFlags, spec.damage, SEVEN_SAGES_AOE_VISUAL_NONE);
}

static void RegisterSevenSagesThrownImpact() {
    COND_HOOK(OnPlayerUpdate, IS_RANDO, SevenSagesThrownImpactPlayerUpdate);
    COND_HOOK(OnActorUpdate, IS_RANDO, SevenSagesThrownImpactActorUpdate);
    COND_HOOK(OnActorKill, IS_RANDO, SevenSagesThrownImpactActorKill);
}

static RegisterShipInitFunc sevenSagesThrownImpactInitFunc(RegisterSevenSagesThrownImpact, { "IS_RANDO" });
