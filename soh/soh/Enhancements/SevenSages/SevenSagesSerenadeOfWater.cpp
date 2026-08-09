/**
 * Seven Sages - Phase 6: Serenade of Water, declined, slow health regeneration.
 *
 * See SevenSagesMinuetOfForest.cpp's header for the shared "No" mechanism. Costs 24 magic via the
 * shared helper, then spreads a full heart bar's worth of healing over roughly three real minutes -
 * same accumulator trick SevenSagesSongOfStorms.cpp uses for its magic trickle (nudge the value one
 * unit at a time rather than a big instant jump), scaled so a full heart bar takes the same wall-clock
 * time regardless of the player's current health capacity.
 *
 * Unlike Storms' rain, this does NOT break on scene transition or room change - there's no vanilla
 * actor being driven here (Storms fights an existing En_Okarina_Effect timer; this is a plain value
 * nudge with nothing else to keep in sync), and "the song's calm stays with you" reads better than a
 * room-scoped weather effect for what's meant to feel like a personal blessing. Matches Epona's
 * Song's precedent (survives transitions) rather than Storms' (breaks on them).
 *
 * Health is nudged directly rather than through Health_ChangeBy, mirroring why Storms nudges
 * gSaveContext.magic directly instead of calling Magic_Fill - avoids re-triggering the recovery
 * sound and GameInteractor_ExecuteOnPlayerHealthChange on every single 1-unit tick.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/SevenSages/SevenSagesSongMagic.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" PlayState* gPlayState;

namespace {

constexpr s16 SERENADE_MAGIC_COST = 24;
// Three minutes at the game's ~20Hz logic tick, matching Song of Storms' magic-trickle assumption -
// confirm empirically and adjust if a full heart bar does not take roughly three real minutes.
constexpr s32 SERENADE_REGEN_FRAMES = 20 * 60 * 3;

s32 sRegenFramesRemaining = 0;
s32 sRegenAccumulator = 0;

void SevenSagesSerenadeOfWaterDeclined() {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    if (gPlayState->msgCtx.lastPlayedSong != OCARINA_SONG_SERENADE) {
        return;
    }

    SevenSagesRequestSongMagic(SERENADE_MAGIC_COST, []() {
        // Replaying while already regenerating restarts the three minutes rather than stacking a
        // second trickle on top, same refresh-not-stack pattern every other song uses.
        sRegenFramesRemaining = SERENADE_REGEN_FRAMES;
        sRegenAccumulator = 0;
    }, kNotAnOcarinaPerformance);
}

void SevenSagesSerenadeOfWaterFrameUpdate() {
    if (sRegenFramesRemaining <= 0) {
        return;
    }
    if (!GameInteractor::IsSaveLoaded(true)) {
        sRegenFramesRemaining = 0;
        sRegenAccumulator = 0;
        return;
    }

    sRegenFramesRemaining--;

    if (gSaveContext.health >= gSaveContext.healthCapacity) {
        return;
    }

    // One full heart bar spread evenly across SERENADE_REGEN_FRAMES, without fractional arithmetic -
    // same shape as Storms' magic accumulator.
    sRegenAccumulator += gSaveContext.healthCapacity;
    while (sRegenAccumulator >= SERENADE_REGEN_FRAMES && gSaveContext.health < gSaveContext.healthCapacity) {
        sRegenAccumulator -= SERENADE_REGEN_FRAMES;
        gSaveContext.health++;
    }
}

} // namespace

static void RegisterSevenSagesSerenadeOfWater() {
    COND_HOOK(OnWarpSongDeclined, IS_SEVENSAGES, SevenSagesSerenadeOfWaterDeclined);
    COND_HOOK(OnGameFrameUpdate, IS_SEVENSAGES, SevenSagesSerenadeOfWaterFrameUpdate);
}

static RegisterShipInitFunc sevenSagesSerenadeOfWaterInitFunc(RegisterSevenSagesSerenadeOfWater, { "IS_RANDO" });
