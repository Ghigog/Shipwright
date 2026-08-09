/**
 * Seven Sages - Phase 6: Skull Mask, undead ignore you while worn.
 *
 * Spec (docs/item-ability-overhaul.md, Masks): the wide definition, resolved 2026-08-05 - every
 * undead ignores you, required fights included. Same shape as VB_GUARD_DETECT_PLAYER
 * (SevenSagesNocturneOfShadow.cpp): vanilla has no shared "is this undead" detection code, every
 * actor inlines its own distance/facing test, so VB_UNDEAD_DETECT_PLAYER sits on each one
 * individually rather than one shared choke point.
 *
 * **Eight of the nine documented actors are covered here.** Each detection site was read in full
 * before gating it, because several of the doc's cited line numbers turned out to be *disengage*
 * checks ("player left range, stop attacking") rather than detection checks - gating those would
 * have inverted the effect, making the enemy MORE persistent while masked. Only the actual
 * become-aggressive triggers are gated:
 *   - En_Rd (ReDead/Gibdo) - one site, the freeze trigger.
 *   - En_Skb (Stalchild) - two sites, both the wake-and-chase trigger.
 *   - En_Wallmas (Wallmaster) - one site, the drop-and-grab trigger.
 *   - En_Test (Stalfos) - three of the doc's four cited sites (the jumpslash/approach triggers).
 *     The fourth (z_en_test.c ~474, EnTest_WaitAbove -> EnTest_Fall, the ceiling-Stalfos drop) is
 *     deliberately left alone: it governs whether a ceiling Stalfos ever becomes reachable at all,
 *     not just whether it's aggressive, and this mod requires at least one Stalfos fight (Forest
 *     Temple). Permanently suppressing the drop could soft-lock that room. Two more similar-shaped
 *     distance checks turned up outside the doc's four cited sites (around 517 and 664 in
 *     EnTest_Idle/EnTest_WalkAndBlock) - not gated here, flagged for a follow-up look.
 *   - En_Dh (Dead Hand) - three of the doc's four cited sites. The one skipped
 *     (z_en_dh.c ~309, inside EnDh_Attack) is the "player left range or turned away, abort the
 *     attack" check - gating it would keep Dead Hand locked in its attack animation instead of
 *     backing off, the opposite of "passive."
 *   - En_Dha (Dead Hand's hands) - one site, the grab trigger. Needed its own
 *     GameInteractor_Hooks.h include, which the file didn't have yet.
 *   - En_Poh (graveyard Poe) - three of the doc's four cited sites, same shape as En_Dh: the one
 *     skipped (z_en_poh.c ~541) is "player moved away, go idle," a disengage check.
 *   - En_Po_Sisters (Forest Temple Poe Sisters) - three sites. One (z_en_po_sisters.c:614) is an
 *     OR of a wait-timer and a proximity check; only the proximity half is gated, so this required
 *     elevator fight can never stall waiting on a detection that will never come.
 *
 * **En_Po_Field (Big Poe) is deliberately NOT touched, contrary to the original task scoping.**
 * The doc's cited z_en_po_field.c:403-407 (`EnPoField_SetFleeSpeed`) turned out on inspection to
 * be exactly the appear/fade speed bands the doc says must be left alone for Big Poe farming
 * (`RSK_BIG_POE_COUNT`) - not the attack path. En_Po_Field has no explicit "detect and attack"
 * function to hang a distance-gated hook on; its damage is passive contact damage through its
 * always-on collider, a different mechanism VB_UNDEAD_DETECT_PLAYER doesn't reach. Left as a
 * follow-up rather than guessed at.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" PlayState* gPlayState;

namespace {

bool IsWearingSkullMask() {
    Player* player = GET_PLAYER(gPlayState);
    return player != nullptr && player->currentMask == PLAYER_MASK_SKULL;
}

} // namespace

static void RegisterSevenSagesSkullMask() {
    COND_VB_SHOULD(VB_UNDEAD_DETECT_PLAYER, IS_SEVENSAGES, {
        [[maybe_unused]] Actor* undead = va_arg(args, Actor*);
        if (IsWearingSkullMask()) {
            *should = false;
        }
    });
}

static RegisterShipInitFunc sevenSagesSkullMaskInitFunc(RegisterSevenSagesSkullMask, { "IS_RANDO" });
