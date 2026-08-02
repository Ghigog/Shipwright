/**
 * Ganon's Curse - Phase 6: Bolero of Fire, declined, restocks explosives.
 *
 * See GanonsCurseMinuetOfForest.cpp's header for the shared "No" mechanism (OnWarpSongDeclined,
 * fired from z_message_PAL.c) and the Inventory_ChangeAmmo restock pattern - this is the same idea
 * applied to Bombs and Bombchus instead of child ammo. Costs 24 magic via the shared helper.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/GanonsCurse/GanonsCurseSongMagic.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" PlayState* gPlayState;

namespace {

constexpr s16 BOLERO_MAGIC_COST = 24;
constexpr s16 RESTOCK_AMOUNT = 99;

void GanonsCurseBoleroOfFireDeclined() {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    if (gPlayState->msgCtx.lastPlayedSong != OCARINA_SONG_BOLERO) {
        return;
    }

    GanonsCurseRequestSongMagic(BOLERO_MAGIC_COST, []() {
        // Bombs need no ownership check - CUR_CAPACITY(UPG_BOMB_BAG) is 0 without a bag, so
        // Inventory_ChangeAmmo's clamp already makes this a no-op. Bombchus have no capacity
        // upgrade in vanilla (flat 50), so the same call would hand 50 chus to a player who has
        // never found any, ammo for an item they don't own. Gate on the inventory slot instead,
        // the same test Infinite Ammo uses. The amount still goes through Inventory_ChangeAmmo
        // rather than being set directly, so rando's progressive bombchu bag (which clamps via
        // VB_CHECK_BOMBCHU_CAPACITY) keeps working.
        Inventory_ChangeAmmo(ITEM_BOMB, RESTOCK_AMOUNT);
        if (INV_CONTENT(ITEM_BOMBCHU) != ITEM_NONE) {
            Inventory_ChangeAmmo(ITEM_BOMBCHU, RESTOCK_AMOUNT);
        }
    });
}

} // namespace

static void RegisterGanonsCurseBoleroOfFire() {
    COND_HOOK(OnWarpSongDeclined, IS_RANDO, GanonsCurseBoleroOfFireDeclined);
}

static RegisterShipInitFunc ganonsCurseBoleroOfFireInitFunc(RegisterGanonsCurseBoleroOfFire, { "IS_RANDO" });
