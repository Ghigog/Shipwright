/*
 * Seven Sages tunic overhaul.
 *
 * See SevenSagesTunics.h for the spec and for why these two predicates fold
 * Nayru's Love in rather than sitting beside it.
 *
 * There is deliberately no hook registration in this file. Unlike the songs
 * and the gauntlets, the tunics are not an *event* — they are a standing
 * condition read by hazard code that already exists, so the work is entirely
 * in widening conditions at the sites that ask "is Link protected right now?".
 * Those sites are:
 *
 *   Fire:  SevenSagesNayrusLoveFireWalls.cpp   flame-wall OC colliders
 *          z_bg_hidan_firewall.c:187,198       flame-wall AT colliders
 *          z_bg_hidan_fwbig.c:231,255          big flame-wall AT colliders
 *          z_bg_hidan_curtain.c:223,248        flame-curtain AT colliders
 *          z_player.c  (sink-into-lava check)  floorProperty 5 bypass
 *          z_player.c  (AC hit response)       fire attacks deal no damage
 *
 *   Frost: z_player.c  (func_80837C0C)         every freeze reaction
 *
 * The freeze case is one patch rather than three because func_80837C0C is the
 * single funnel every freeze source passes through: enemy ice contact
 * (HIT_SPECIAL_EFFECT_ICE), vanilla ice traps, and randomizer ice traps via
 * GameInteractor::RawAction::FreezePlayer, which calls it with response 3.
 */

#include "soh/Enhancements/SevenSages/SevenSagesTunics.h"

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/OTRGlobals.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
}

extern "C" PlayState* gPlayState;

namespace {

// Read from the save rather than from Player::currentTunic. z_parameter.c's
// existing hot-room and drowning checks use CUR_EQUIP_VALUE, and matching them
// keeps "the tunic protects you" answering the same way in every hazard,
// including the frames where Player::currentTunic is mid-swap.
bool WearingTunic(u8 tunicValue) {
    return CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC) == tunicValue;
}

bool NayrusLoveActive() {
    return gSaveContext.nayrusLoveTimer != 0;
}

// Goron/Zora Masks grant the matching tunic's effect (Masks section,
// docs/item-ability-overhaul.md, resolved 2026-08-05). Masks are child-only and tunics only
// matter as adult-form protection in vanilla, but Nayru's Love and the mask both work at any
// age, so this is the only route to fire/frost protection a child ever has.
bool WearingMask(s32 mask) {
    Player* player = GET_PLAYER(gPlayState);
    return player != nullptr && player->currentMask == mask;
}

} // namespace

bool SevenSagesFireProtectionActive(void) {
    if (!IS_SEVENSAGES || !GameInteractor::IsSaveLoaded(true)) {
        return false;
    }
    return NayrusLoveActive() || WearingTunic(EQUIP_VALUE_TUNIC_GORON) || WearingMask(PLAYER_MASK_GORON);
}

bool SevenSagesFrostProtectionActive(void) {
    if (!IS_SEVENSAGES || !GameInteractor::IsSaveLoaded(true)) {
        return false;
    }
    return NayrusLoveActive() || WearingTunic(EQUIP_VALUE_TUNIC_ZORA) || WearingMask(PLAYER_MASK_ZORA);
}
