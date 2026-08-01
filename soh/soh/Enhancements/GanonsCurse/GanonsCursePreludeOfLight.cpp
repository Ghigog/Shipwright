/**
 * Ganon's Curse - Phase 6: Prelude of Light, declined, prevents death for 20 seconds.
 *
 * See GanonsCurseMinuetOfForest.cpp's header for the shared "No" mechanism. Costs 24 magic via the
 * shared helper, then for 20 seconds any hit that would drop health to 0 or below instead leaves
 * Link at 1 HP and the hit counts as survived, not lethal - a fairy-style save. Interpretation note:
 * the spec ("stops you from dying within the next 20s ... like a fairy") reads as duration-based
 * protection, not a single-use token, so this can save the player more than once inside the window
 * if they take multiple near-lethal hits in a row. Revisit if playtesting says that's too generous.
 *
 * Mechanically this is NOT the same as blanket damage immunity (invincibilityTimer, which several
 * SoH cheats already use, e.g. GameInteractor::RawAction::SetPlayerInvincibility) - health still
 * drops normally on every hit during the window, this only intercepts the specific hit that would
 * have been lethal. New VB_PREVENT_PLAYER_DEATH hook (see GIVanillaBehavior.h) fires from
 * Health_ChangeBy (z_parameter.c) at the exact point health would clamp to 0, before the death path
 * is taken - the one function every source of damage to Link already funnels through.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/GanonsCurse/GanonsCurseSongMagic.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" PlayState* gPlayState;

namespace {

constexpr s16 PRELUDE_MAGIC_COST = 24;
constexpr s32 PRELUDE_BUFF_FRAMES = 20 * 20;

s32 sBuffFramesRemaining = 0;

void GanonsCursePreludeOfLightDeclined() {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    if (gPlayState->msgCtx.lastPlayedSong != OCARINA_SONG_PRELUDE) {
        return;
    }

    GanonsCurseRequestSongMagic(PRELUDE_MAGIC_COST, []() { sBuffFramesRemaining = PRELUDE_BUFF_FRAMES; });
}

void GanonsCursePreludeOfLightFrameUpdate() {
    if (sBuffFramesRemaining > 0) {
        sBuffFramesRemaining--;
    }
}

} // namespace

static void RegisterGanonsCursePreludeOfLight() {
    COND_HOOK(OnWarpSongDeclined, IS_RANDO, GanonsCursePreludeOfLightDeclined);
    COND_HOOK(OnGameFrameUpdate, IS_RANDO, GanonsCursePreludeOfLightFrameUpdate);
    COND_VB_SHOULD(VB_PREVENT_PLAYER_DEATH, IS_RANDO, { *should = sBuffFramesRemaining > 0; });
}

static RegisterShipInitFunc ganonsCursePreludeOfLightInitFunc(RegisterGanonsCursePreludeOfLight, { "IS_RANDO" });
