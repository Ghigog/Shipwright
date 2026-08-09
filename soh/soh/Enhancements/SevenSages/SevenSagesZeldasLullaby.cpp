/**
 * Seven Sages - Phase 6: Zelda's Lullaby as a sleep-type room stun.
 *
 * docs/item-ability-overhaul.md: "a sleep-type spell" - resolved 2026-08-01 to carry the
 * "stuns all enemies in the room for 15 seconds" effect originally drafted for Sun's Song, moved
 * here since Sun's Song already incapacitates enemies via burning (the stun was redundant there).
 * "Sleep" is the theme for this song's version of the room-wide stun, not a second, separate
 * mechanic - there is one effect: every enemy currently loaded (i.e. in this room - see
 * SevenSagesForEachActorInRoom in SevenSagesRoomAoe.h) stops acting for 15 seconds.
 *
 * The stun itself needs no new engine mechanic: Actor::freezeTimer (z64actor.h) is already a
 * universal per-actor field the main actor-update loop checks before calling an actor's update
 * function at all (z_actor.c:2674, Actor_UpdateAll) - the same mechanism Deku Nuts/Boomerang/ice
 * traps already use. Setting it on every enemy in the room is the entire implementation; no
 * per-enemy-type code needed.
 *
 * Costs 24 magic through the shared deferred-request helper (SevenSagesSongMagic.h), same as
 * every other song. No proximity guard needed - Zelda's Lullaby's several vanilla puzzle triggers
 * (Impa's roof interaction, Hyrule Castle, etc.) are location-script based, not enemy-AI based, so
 * a room-wide enemy stun has nothing to collide with.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/SevenSages/SevenSagesSongMagic.h"
#include "soh/Enhancements/SevenSages/SevenSagesRoomAoe.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" PlayState* gPlayState;

namespace {

constexpr s16 ZELDAS_LULLABY_MAGIC_COST = 24;
constexpr u16 ZELDAS_LULLABY_STUN_FRAMES = 20 * 15;

void SevenSagesZeldasLullabyPlayed() {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    if (gPlayState->msgCtx.lastPlayedSong != OCARINA_SONG_LULLABY) {
        return;
    }

    SevenSagesRequestSongMagic(ZELDAS_LULLABY_MAGIC_COST, []() {
        SevenSagesForEachActorInRoom(gPlayState, ACTORCAT_ENEMY,
                                      [](Actor* enemy) { enemy->freezeTimer = ZELDAS_LULLABY_STUN_FRAMES; });
    });
}

} // namespace

static void RegisterSevenSagesZeldasLullaby() {
    COND_HOOK(OnOcarinaSongAction, IS_SEVENSAGES, SevenSagesZeldasLullabyPlayed);
}

static RegisterShipInitFunc sevenSagesZeldasLullabyInitFunc(RegisterSevenSagesZeldasLullaby, { "IS_RANDO" });
