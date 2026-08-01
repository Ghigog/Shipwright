/**
 * Ganon's Curse - Phase 6: Minuet of Forest, declined, restocks child ammo.
 *
 * docs/item-ability-overhaul.md's Warp Songs section: the vanilla "Warp to X?" yes/no prompt is
 * kept as-is (see GanonsCurseSariasSong.cpp's sibling files for the shared design note) - "Yes" is
 * untouched, free vanilla travel; "No" triggers this instead of just closing the dialogue, via the
 * new OnWarpSongDeclined hook fired from z_message_PAL.c at the exact point the player's choice
 * resolves. Costs 24 magic through the same shared helper every other song uses.
 *
 * Restocks Sticks, Nuts, Deku Seeds (slingshot ammo), and Arrows to whatever capacity the player
 * currently has. Inventory_ChangeAmmo (z_parameter.c) is the same function real ammo pickups call -
 * it's a delta, not a set, and already clamps to capacity internally, so a large positive delta is
 * a safe "fill to full" regardless of current amount or capacity (including capacity 0, which is
 * just a no-op, same as it would be for a real pickup).
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/GanonsCurse/GanonsCurseSongMagic.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" PlayState* gPlayState;

namespace {

constexpr s16 MINUET_MAGIC_COST = 24;
constexpr s16 RESTOCK_AMOUNT = 99;

void GanonsCurseMinuetOfForestDeclined() {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    if (gPlayState->msgCtx.lastPlayedSong != OCARINA_SONG_MINUET) {
        return;
    }

    GanonsCurseRequestSongMagic(MINUET_MAGIC_COST, []() {
        Inventory_ChangeAmmo(ITEM_STICK, RESTOCK_AMOUNT);
        Inventory_ChangeAmmo(ITEM_NUT, RESTOCK_AMOUNT);
        Inventory_ChangeAmmo(ITEM_SLINGSHOT, RESTOCK_AMOUNT);
        Inventory_ChangeAmmo(ITEM_BOW, RESTOCK_AMOUNT);
    });
}

} // namespace

static void RegisterGanonsCurseMinuetOfForest() {
    COND_HOOK(OnWarpSongDeclined, IS_RANDO, GanonsCurseMinuetOfForestDeclined);
}

static RegisterShipInitFunc ganonsCurseMinuetOfForestInitFunc(RegisterGanonsCurseMinuetOfForest, { "IS_RANDO" });
