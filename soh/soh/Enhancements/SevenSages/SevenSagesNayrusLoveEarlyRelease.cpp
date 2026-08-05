/**
 * Seven Sages - Magic system rework: Nayru's Love releases the magic meter as soon as its own cast
 * cost finishes draining, instead of holding it hostage for the shield's whole ~20 second duration.
 *
 * gSaveContext.magicState has no exit from MAGIC_STATE_METER_FLASH_1/2/3 except Magic_Reset() -
 * confirmed by reading z_parameter.c's state machine directly: those three cases just cycle a
 * border-color animation forever with nothing that ever changes magicState on its own. Every other
 * magic-consuming action (spells, arrows, songs, the gauntlet door bypass) reads a non-idle
 * magicState as "busy" and refuses.
 *
 * Din's Fire, Farore's Wind, and the elemental arrows never hit this problem: each spawns a
 * short-lived effect actor (Magic_Fire, Magic_Wind, Arrow_Fire/Ice/Light) whose own Destroy() calls
 * Magic_Reset() within a second or two of casting/firing - confirmed by reading each of those
 * Destroy functions directly, not assumed. Nayru's Love is the one deliberate exception:
 * MagicDark_Destroy (z_magic_dark.c) only calls Magic_Reset() `if (nayrusLoveTimer == 0)` - true
 * only once the shield has already run its full course. That is vanilla's actual, intentional
 * behavior (you cannot chain-cast in original OoT while the shield is up); this file is the one
 * deliberate deviation from it for this overhaul, per direct instruction.
 *
 * The fix hooks the shield's own actor (ACTOR_MAGIC_DARK) rather than the cast code in
 * z_player.c, because that is the actor category umbrella that already correctly spans both the
 * ~1s orb-flying-in intro (MagicDark_OrbUpdate) and the diamond/shield phase
 * (MagicDark_DiamondUpdate) - the two prior attempts at this fix failed because they hooked a single
 * point in the cast sequence in z_player.c that either ran before the real drain was armed, or got
 * overwritten a frame later when Player_Action_808507F4 re-armed MAGIC_STATE_CONSUME_SETUP.
 * Checking "is magicState stuck in a flash state" on every frame the actor is alive, instead of a
 * one-shot check tied to a specific line in the cast sequence, sidesteps needing to know exactly
 * which frame the drain finishes on.
 *
 * Deliberately does not gate on gSaveContext.nayrusLoveTimer: that field stays 0 for the first ~55
 * frames of the cast (the orb-intro phase, before MagicDark_OrbUpdate hands off to
 * MagicDark_DiamondUpdate), but the magic drain (2/frame) finishes well before that - so gating on
 * it would miss the exact moment this needs to fire. Actor liveness has no such lag.
 */
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
}

extern "C" PlayState* gPlayState;

namespace {

void OnMagicDarkUpdate(void* actorPtr) {
    if (!GameInteractor::IsSaveLoaded(true) || gPlayState == nullptr) {
        return;
    }
    // Reaching any of these three states at all means the drain already finished - the state
    // machine's own MAGIC_STATE_CONSUME case only ever transitions here once
    // gSaveContext.magic == gSaveContext.magicTarget, so there is nothing left to check.
    if (gSaveContext.magicState == MAGIC_STATE_METER_FLASH_1 || gSaveContext.magicState == MAGIC_STATE_METER_FLASH_2 ||
        gSaveContext.magicState == MAGIC_STATE_METER_FLASH_3) {
        Magic_Reset(gPlayState);
    }
}

} // namespace

static void RegisterSevenSagesNayrusLoveEarlyRelease() {
    COND_ID_HOOK(OnActorUpdate, ACTOR_MAGIC_DARK, IS_RANDO, OnMagicDarkUpdate);
}

static RegisterShipInitFunc sevenSagesNayrusLoveEarlyReleaseInitFunc(RegisterSevenSagesNayrusLoveEarlyRelease,
                                                                     { "IS_RANDO" });
