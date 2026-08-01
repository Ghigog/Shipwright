/**
 * Ganon's Curse - Phase 6: Song of Time as an on-demand adult/child toggle.
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
 * dial. This file is the built-in Ganon's Curse version: always on under the randomizer (no CVar,
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
 * MAGIC_STATE_CONSUME_LENS - regardless of how much magic is actually available. The ocarina
 * song-recognition callback (OnOcarinaSongAction) fires from inside the message system, before
 * magicState has ticked back to IDLE from the MAGIC_STATE_RESET that Player_Destroy/scene-init
 * code leaves it in - a one-frame race, confirmed via a live playtest log showing magic=96/96,
 * acquired=1, magicState=5 (RESET) at the exact moment the request was declined. Fixed by
 * deferring the actual magic spend + age swap to OnGameFrameUpdate, once magicState has settled.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/SwitchAge.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" PlayState* gPlayState;

namespace {

constexpr s16 SONG_OF_TIME_MAGIC_COST = 24;
// Safety cap only - magicState settling back to IDLE is normally a 1-frame wait. Never observed
// to matter in practice; exists so a pending toggle can't theoretically hang forever.
constexpr s32 PENDING_TOGGLE_FRAME_LIMIT = 60;

bool sPendingToggle = false;
s32 sPendingToggleFrames = 0;

bool NearVanillaTimeMechanic(Actor* player) {
    return Actor_FindNearby(gPlayState, player, ACTOR_OBJ_WARP2BLOCK, ACTORCAT_ITEMACTION, 300.0f) != NULL ||
           Actor_FindNearby(gPlayState, player, ACTOR_OBJ_TIMEBLOCK, ACTORCAT_ITEMACTION, 300.0f) != NULL ||
           Actor_FindNearby(gPlayState, player, ACTOR_EN_OKARINA_TAG, ACTORCAT_PROP, 120.0f) != NULL ||
           Actor_FindNearby(gPlayState, player, ACTOR_DOOR_TOKI, ACTORCAT_BG, 500.0f) != NULL ||
           Actor_FindNearby(gPlayState, player, ACTOR_EN_FR, ACTORCAT_NPC, 300.0f) != NULL ||
           Actor_FindNearby(gPlayState, player, ACTOR_EN_GS, ACTORCAT_NPC, 300.0f) != NULL;
}

void GanonsCurseSongOfTimePlayed() {
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

    sPendingToggle = true;
    sPendingToggleFrames = 0;
}

void GanonsCurseSongOfTimeFrameUpdate() {
    if (!sPendingToggle) {
        return;
    }

    // Mirrors the states Magic_RequestChange itself accepts for MAGIC_CONSUME_NOW - waiting for
    // one of these is what avoids racing the message system's own magicState management.
    bool magicStateSettled =
        gSaveContext.magicState == MAGIC_STATE_IDLE || gSaveContext.magicState == MAGIC_STATE_CONSUME_LENS;

    if (!magicStateSettled && sPendingToggleFrames < PENDING_TOGGLE_FRAME_LIMIT) {
        sPendingToggleFrames++;
        return;
    }

    sPendingToggle = false;

    if (!magicStateSettled) {
        // Never observed; give up rather than spend magic against a state Magic_RequestChange
        // would've refused anyway.
        return;
    }

    if (!Magic_RequestChange(gPlayState, SONG_OF_TIME_MAGIC_COST, MAGIC_CONSUME_NOW)) {
        return;
    }

    // Magic_RequestChange doesn't apply the deduction itself - MAGIC_CONSUME_NOW only arms
    // magicTarget and moves to MAGIC_STATE_CONSUME_SETUP, then drains gSaveContext.magic toward
    // that target by 2/frame over several frames of normal gameplay (see MAGIC_STATE_CONSUME in
    // z_parameter.c). SwitchAge() below triggers an instant scene transition, which tears down
    // and rebuilds Play before that drain ever gets a frame to run, so the deduction was silently
    // lost. Fast-forward it here instead of waiting on an animation that won't get to play out.
    gSaveContext.magic = gSaveContext.magicTarget;
    gSaveContext.magicState = MAGIC_STATE_IDLE;

    SwitchAge();
}

} // namespace

static void RegisterGanonsCurseSongOfTime() {
    COND_HOOK(OnOcarinaSongAction, IS_RANDO, GanonsCurseSongOfTimePlayed);
    COND_HOOK(OnGameFrameUpdate, IS_RANDO, GanonsCurseSongOfTimeFrameUpdate);
}

static RegisterShipInitFunc ganonsCurseSongOfTimeInitFunc(RegisterGanonsCurseSongOfTime, { "IS_RANDO" });
