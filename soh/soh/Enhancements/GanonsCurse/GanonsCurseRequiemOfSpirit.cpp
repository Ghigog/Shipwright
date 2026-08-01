/**
 * Ganon's Curse - Phase 6: Requiem of Spirit, declined, free spell casts for 20 seconds.
 *
 * See GanonsCurseMinuetOfForest.cpp's header for the shared "No" mechanism. Costs 24 magic via the
 * shared helper, then waives the cost of Din's Fire, Nayru's Love, and Farore's Wind for the buff's
 * duration - the three-spell system funnels through one exact call site,
 * z_player.c's func_8083AF44 (`Magic_RequestChange(play, sMagicSpellCosts[magicSpell],
 * MAGIC_CONSUME_WAIT_PREVIEW)`), so a single new hook there covers all three rather than needing
 * per-spell wiring. VB_PLAYER_MODIFY_MAGIC_SPELL_COST (new, see GIVanillaBehavior.h) exposes the
 * about-to-be-deducted cost by pointer, same shape as Epona's Song's VB_PLAYER_MODIFY_RUN_SPEED.
 *
 * Deliberately does not touch arrow magic costs (Fire/Ice/Light) or song costs (including this
 * song's own) - those are separate call sites this hook was placed to not cover, since "spells" in
 * the spec means the three C-button magic spells specifically.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/GanonsCurse/GanonsCurseSongMagic.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" PlayState* gPlayState;

namespace {

constexpr s16 REQUIEM_MAGIC_COST = 24;
constexpr s32 REQUIEM_BUFF_FRAMES = 20 * 20;

s32 sBuffFramesRemaining = 0;

void GanonsCurseRequiemOfSpiritDeclined() {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    if (gPlayState->msgCtx.lastPlayedSong != OCARINA_SONG_REQUIEM) {
        return;
    }

    GanonsCurseRequestSongMagic(REQUIEM_MAGIC_COST, []() { sBuffFramesRemaining = REQUIEM_BUFF_FRAMES; });
}

void GanonsCurseRequiemOfSpiritFrameUpdate() {
    if (sBuffFramesRemaining > 0) {
        sBuffFramesRemaining--;
    }
}

} // namespace

static void RegisterGanonsCurseRequiemOfSpirit() {
    COND_HOOK(OnWarpSongDeclined, IS_RANDO, GanonsCurseRequiemOfSpiritDeclined);
    COND_HOOK(OnGameFrameUpdate, IS_RANDO, GanonsCurseRequiemOfSpiritFrameUpdate);
    COND_VB_SHOULD(VB_PLAYER_MODIFY_MAGIC_SPELL_COST, IS_RANDO, {
        [[maybe_unused]] Player* player = va_arg(args, Player*);
        s16* cost = va_arg(args, s16*);
        if (sBuffFramesRemaining > 0) {
            *cost = 0;
        }
    });
}

static RegisterShipInitFunc ganonsCurseRequiemOfSpiritInitFunc(RegisterGanonsCurseRequiemOfSpirit, { "IS_RANDO" });
