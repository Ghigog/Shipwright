/**
 * Seven Sages - Phase 6: Saria's Song as a temporary "climb anything" boost.
 *
 * docs/item-ability-overhaul.md: climb any surface for 20 seconds. Costs 24 magic (the
 * system-wide "songs cost magic" rule) via the shared deferred-request helper
 * (SevenSagesSongMagic.h) built for Song of Time. No proximity guard needed here, same
 * reasoning as Epona's Song - this doesn't intercept or replace any vanilla Saria's Song
 * trigger (calling Saria, opening the Sacred Forest Meadow maze door), it only additionally
 * starts the climb buff alongside whatever vanilla already does.
 *
 * Rather than new climbable-surface logic, this toggles the existing "ClimbEverything" cheat
 * (see Cheats/ClimbEverything.cpp) on for the buff's duration and back off when it expires -
 * that cheat's CVAR_CHEAT("ClimbEverything") already gates every VB hook needed (surface/angle
 * climbability, the climb-vs-door-open disambiguation, ledge-slope handling), so there is
 * nothing left to build there.
 *
 * The prior CVar value is snapshotted before the buff forces it on and restored (not just
 * zeroed) when the buff ends, so a player who already has the cheat on manually from the
 * Enhancements menu doesn't have it silently turned off by this song. Known and accepted edge
 * case: if the player manually toggles the cheat mid-buff, the buff's own end will still
 * restore to whatever the value was at cast time, not the player's later manual change -
 * guarding against that would mean intercepting the menu checkbox itself, out of scope here.
 *
 * Duration is a plain frame countdown, not an absolute target against PlayState::gameplayFrames
 * - see SevenSagesEponasSong.cpp's header comment for why. 400 frames assumes the game's ~20Hz
 * logic tick rate (20 * 20s) - confirm empirically that the buff actually lasts ~20 real
 * seconds, and adjust SARIAS_SONG_BUFF_FRAMES if not.
 *
 * Replaying the song while already buffed refreshes the duration rather than re-snapshotting -
 * re-snapshotting on an already-active buff would capture "1" (our own forced value) as the
 * "prior" value instead of the real one, which would then fail to restore correctly.
 *
 * Bug found live 2026-08-01, testing Saria: setting the CVar alone did nothing - climbing never
 * activated. ClimbEverything.cpp's VB hooks are (re)registered only when something calls
 * ShipInit::Init(CVAR_CHEAT("ClimbEverything")), which is what the Enhancements-menu checkbox
 * does after every toggle (UIWidgets.cpp's CVarCheckbox); a bare CVarSetInteger doesn't trigger
 * that. Fixed by calling ShipInit::Init ourselves right after each CVarSetInteger, both turning
 * the buff on and restoring the prior value when it ends - deliberately not also calling
 * SaveConsoleVariablesNextFrame() the way the menu checkbox does, since this is a transient
 * buff state that should never get written to the user's saved settings.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/SevenSages/SevenSagesSongMagic.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" PlayState* gPlayState;

namespace {

constexpr s16 SARIAS_SONG_MAGIC_COST = 24;
constexpr s32 SARIAS_SONG_BUFF_FRAMES = 20 * 20;

s32 sBuffFramesRemaining = 0;
s32 sPriorClimbEverythingValue = 0;

void SetClimbEverything(s32 value) {
    CVarSetInteger(CVAR_CHEAT("ClimbEverything"), value);
    ShipInit::Init(CVAR_CHEAT("ClimbEverything"));
}

void SevenSagesSariasSongPlayed() {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    if (gPlayState->msgCtx.lastPlayedSong != OCARINA_SONG_SARIAS) {
        return;
    }

    SevenSagesRequestSongMagic(SARIAS_SONG_MAGIC_COST, []() {
        if (sBuffFramesRemaining <= 0) {
            sPriorClimbEverythingValue = CVarGetInteger(CVAR_CHEAT("ClimbEverything"), 0);
            SetClimbEverything(1);
        }
        sBuffFramesRemaining = SARIAS_SONG_BUFF_FRAMES;
    });
}

void SevenSagesSariasSongFrameUpdate() {
    if (sBuffFramesRemaining <= 0) {
        return;
    }

    sBuffFramesRemaining--;
    if (sBuffFramesRemaining == 0) {
        SetClimbEverything(sPriorClimbEverythingValue);
    }
}

} // namespace

static void RegisterSevenSagesSariasSong() {
    COND_HOOK(OnOcarinaSongAction, IS_RANDO, SevenSagesSariasSongPlayed);
    COND_HOOK(OnGameFrameUpdate, IS_RANDO, SevenSagesSariasSongFrameUpdate);
}

static RegisterShipInitFunc sevenSagesSariasSongInitFunc(RegisterSevenSagesSariasSong, { "IS_RANDO" });
