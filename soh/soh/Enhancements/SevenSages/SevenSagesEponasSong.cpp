/**
 * Seven Sages - Phase 6: Epona's Song as a temporary speed boost.
 *
 * docs/item-ability-overhaul.md: "boosts speed 1.5x for 60 seconds." Costs 24 magic (the
 * system-wide "songs cost magic" rule) via the shared deferred-request helper
 * (SevenSagesSongMagic.h) built for Song of Time. No proximity guard needed here, unlike Song of
 * Time - playing Epona's Song has no vanilla puzzle interaction this could hijack, only "try to
 * call Epona if she's nearby," which this doesn't touch.
 *
 * Duration is a plain frame countdown, not an absolute target against PlayState::gameplayFrames -
 * that field resets to 0 on every scene transition (it's a PlayState member, not save-persistent),
 * so an absolute end-frame computed before a warp would misread after one. A relative countdown,
 * decremented once per OnGameFrameUpdate tick, is immune to that. 1200 frames assumes the game's
 * ~20Hz logic tick rate (PlayState::gameplayFrames increments once per unpaused Play_Update call,
 * same cadence OnGameFrameUpdate fires at) - confirm empirically that the buff actually lasts
 * ~60 real seconds, and adjust EPONAS_SONG_BUFF_FRAMES if not.
 *
 * Speed multiplier reuses the same hook SoH's own Bunny Hood/SpeedModifier enhancements use
 * (VB_PLAYER_MODIFY_RUN_SPEED, see SpeedModifiers.cpp) rather than touching player movement code
 * directly - it already exists precisely to let something multiply the ground run speed target in
 * place.
 *
 * Replaying the song while already buffed refreshes the duration rather than stacking - two
 * 1.5x buffs would compound multiplicatively through the same VB hook otherwise, since each active
 * source multiplies speedTarget independently.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/SevenSages/SevenSagesSongMagic.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" PlayState* gPlayState;

namespace {

constexpr s16 EPONAS_SONG_MAGIC_COST = 24;
constexpr s32 EPONAS_SONG_BUFF_FRAMES = 20 * 60;
constexpr f32 EPONAS_SONG_SPEED_MULTIPLIER = 1.5f;

s32 sBuffFramesRemaining = 0;

void SevenSagesEponasSongPlayed() {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    if (gPlayState->msgCtx.lastPlayedSong != OCARINA_SONG_EPONAS) {
        return;
    }

    SevenSagesRequestSongMagic(EPONAS_SONG_MAGIC_COST, []() { sBuffFramesRemaining = EPONAS_SONG_BUFF_FRAMES; });
}

void SevenSagesEponasSongFrameUpdate() {
    if (sBuffFramesRemaining > 0) {
        sBuffFramesRemaining--;
    }
}

} // namespace

static void RegisterSevenSagesEponasSong() {
    COND_HOOK(OnOcarinaSongAction, IS_RANDO, SevenSagesEponasSongPlayed);
    COND_HOOK(OnGameFrameUpdate, IS_RANDO, SevenSagesEponasSongFrameUpdate);
    COND_VB_SHOULD(VB_PLAYER_MODIFY_RUN_SPEED, IS_RANDO, {
        [[maybe_unused]] Player* player = va_arg(args, Player*);
        f32* speedTarget = va_arg(args, f32*);
        if (sBuffFramesRemaining > 0) {
            *speedTarget *= EPONAS_SONG_SPEED_MULTIPLIER;
        }
    });
}

static RegisterShipInitFunc sevenSagesEponasSongInitFunc(RegisterSevenSagesEponasSong, { "IS_RANDO" });
