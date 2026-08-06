/**
 * Seven Sages - Phase 6: Keaton Mask, half price at shops / double resource drops.
 *
 * Spec (docs/item-ability-overhaul.md, Masks): both halves are maximal, resolved 2026-08-05.
 * "Resource" means everything that drops - rupees and hearts included, not just ammo and
 * consumables. Half price applies to every vendor, not only the EnGirlA shops.
 *
 * **Double drops** hooks VB_MODIFY_RANDOM_DROP_QUANTITY, fired from Item_DropCollectibleRandom
 * (z_en_item00.c) after dropQuantity is looked up and before the spawn loop consumes it. That
 * covers the ~47 enemy/grass actors that go through this path, and since the hook sits downstream
 * of dropId selection, it doubles whatever was chosen - rupees and hearts included - with no
 * special-casing needed here.
 *
 * **This does NOT cover Item_DropCollectible** (pots and ~29 other fixed single-drop actors,
 * e.g. z_obj_tsubo.c:92). That function spawns exactly one item and returns a single actor
 * pointer some callers depend on, so "doubling" it means spawning a second item per call, which
 * needs a restructure of a function with 29 call sites rather than a one-line multiply. Deferred
 * pending a closer look at whether any of those callers actually use the return value in a way a
 * second spawn would disturb.
 *
 * **Half price** hooks VB_MODIFY_SHOP_PRICE, fired from all three basePrice assignment sites in
 * EnGirlA_Init (z_en_girla.c) - the randomized-item override, the BetterBombchuShopping/normal
 * item-table branch, and the non-rando branch - so it covers every EnGirlA shop (Bazaar, Kakariko,
 * Zora shops, the Happy Mask Shop when RSK_MASK_QUEST opens it, etc.) from one patched field, since
 * basePrice is what both the price tag and the affordability/deduction checks read.
 *
 * Medigoron and the carpet salesman are not EnGirlA and have their own hardcoded "gSaveContext.
 * rupees < 200" affordability checks with existing rando-only hooks
 * (VB_CHECK_RANDO_PRICE_OF_MEDIGORON, VB_CHECK_RANDO_PRICE_OF_CARPET_SALESMAN) to hang a halved
 * threshold off. Only the affordability gate is halved this way, not the displayed "200 rupees"
 * dialogue text, matching the existing limitation of those two hooks.
 *
 * **Not yet covered, deliberately deferred**: bombchu bowling, the two shooting galleries, and
 * Granny's potion shop. Each is a separate price site not yet located in the codebase. Flagging
 * here rather than silently claiming full "every vendor" coverage.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" PlayState* gPlayState;

namespace {

bool IsWearingKeatonMask() {
    Player* player = GET_PLAYER(gPlayState);
    return player != nullptr && player->currentMask == PLAYER_MASK_KEATON;
}

} // namespace

static void RegisterSevenSagesKeaton() {
    COND_VB_SHOULD(VB_MODIFY_SHOP_PRICE, IS_RANDO, {
        [[maybe_unused]] Actor* shopActor = va_arg(args, Actor*);
        s16* basePrice = va_arg(args, s16*);
        if (IsWearingKeatonMask()) {
            *basePrice = *basePrice / 2;
        }
    });

    COND_VB_SHOULD(VB_MODIFY_RANDOM_DROP_QUANTITY, IS_RANDO, {
        [[maybe_unused]] Actor* fromActor = va_arg(args, Actor*);
        s16* dropQuantity = va_arg(args, s16*);
        if (IsWearingKeatonMask()) {
            *dropQuantity *= 2;
        }
    });

    COND_VB_SHOULD(VB_CHECK_RANDO_PRICE_OF_MEDIGORON, IS_RANDO, {
        [[maybe_unused]] Actor* medigoron = va_arg(args, Actor*);
        if (IsWearingKeatonMask()) {
            *should = gSaveContext.rupees < 100;
        }
    });

    COND_VB_SHOULD(VB_CHECK_RANDO_PRICE_OF_CARPET_SALESMAN, IS_RANDO, {
        [[maybe_unused]] Actor* carpetSalesman = va_arg(args, Actor*);
        if (IsWearingKeatonMask()) {
            *should = gSaveContext.rupees < 100;
        }
    });
}

static RegisterShipInitFunc sevenSagesKeatonInitFunc(RegisterSevenSagesKeaton, { "IS_RANDO" });
