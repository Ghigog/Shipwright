/**
 * Seven Sages - Phase 6: Song of Time as an on-demand adult/child toggle.
 *
 * docs/item-ability-overhaul.md picked option 2 of three Song of Time proposals: instead of
 * vanilla's fixed Temple-of-Time-pedestal-only age switch, playing Song of Time anywhere lets the
 * player toggle age on demand, at the cost of magic (the "songs cost magic" system-wide rule,
 * resolved to 24 units - the same cost as Nayru's Love).
 *
 * The age-swap mechanism itself is not new. SwitchAge() (soh/soh/Enhancements/SwitchAge.cpp)
 * already flips linkAgeOnLoad and re-enters the current scene through the vanilla age-transition
 * pipeline, so equipment/inventory swap comes for free. OcarinaTimeTravel.cpp
 * (soh/soh/Enhancements/QoL/) already proved the "cast Song of Time -> SwitchAge()" wiring works,
 * gated behind a disabled-by-default QoL cheat CVar with a configurable equipment-requirement
 * dial. This file is the built-in Seven Sages version: always on under the randomizer (no CVar,
 * no equipment requirement - only knowing the song matters), gated on magic instead.
 *
 * Kept from OcarinaTimeTravel: the proximity guard against vanilla time-mechanic actors/props
 * (Door of Time, time blocks, the frog choir, gossip stones, and any other ocarina-triggered
 * scripted spot). Without it, playing Song of Time to solve one of those vanilla puzzles would
 * also silently burn magic and swap age underneath the puzzle, which is not what "songs cost
 * magic" was meant to gate.
 *
 * If magic is unavailable (not yet acquired, or too low), Magic_RequestChange itself declines and
 * plays the vanilla insufficient-magic error sound - that decline IS the fallback to "current/
 * default vanilla behavior" the system-wide magic-cost rule calls for: nothing happens beyond
 * whatever vanilla Song of Time already does at that spot.
 *
 * Found live 2026-08-01: Magic_RequestChange(..., MAGIC_CONSUME_NOW) also silently declines (with
 * that same error sound) whenever gSaveContext.magicState isn't MAGIC_STATE_IDLE or
 * MAGIC_STATE_CONSUME_LENS - regardless of how much magic is actually available, and doesn't apply
 * its own deduction before an immediately-following scene transition can cut the drain animation
 * short. Both fixes are now shared plumbing - see SevenSagesSongMagic.h/.cpp, extracted here after
 * the same race was expected to recur for every other magic-costing song.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/SwitchAge.h"
#include "soh/Enhancements/SevenSages/SevenSagesSongMagic.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" PlayState* gPlayState;

namespace {

constexpr s16 SONG_OF_TIME_MAGIC_COST = 24;

bool NearVanillaTimeMechanic(Actor* player) {
    return Actor_FindNearby(gPlayState, player, ACTOR_OBJ_WARP2BLOCK, ACTORCAT_ITEMACTION, 300.0f) != NULL ||
           Actor_FindNearby(gPlayState, player, ACTOR_OBJ_TIMEBLOCK, ACTORCAT_ITEMACTION, 300.0f) != NULL ||
           Actor_FindNearby(gPlayState, player, ACTOR_EN_OKARINA_TAG, ACTORCAT_PROP, 120.0f) != NULL ||
           Actor_FindNearby(gPlayState, player, ACTOR_DOOR_TOKI, ACTORCAT_BG, 500.0f) != NULL ||
           Actor_FindNearby(gPlayState, player, ACTOR_EN_FR, ACTORCAT_NPC, 300.0f) != NULL ||
           Actor_FindNearby(gPlayState, player, ACTOR_EN_GS, ACTORCAT_NPC, 300.0f) != NULL;
}

void SevenSagesSongOfTimePlayed() {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    if (gPlayState->msgCtx.lastPlayedSong != OCARINA_SONG_TIME) {
        return;
    }

    Actor* player = &GET_PLAYER(gPlayState)->actor;
    if (NearVanillaTimeMechanic(player)) {
        return;
    }

    SevenSagesRequestSongMagic(SONG_OF_TIME_MAGIC_COST, []() { SwitchAge(); });
}

} // namespace

static void RegisterSevenSagesSongOfTime() {
    COND_HOOK(OnOcarinaSongAction, IS_SEVENSAGES, SevenSagesSongOfTimePlayed);
}

static RegisterShipInitFunc sevenSagesSongOfTimeInitFunc(RegisterSevenSagesSongOfTime, { "IS_RANDO" });
