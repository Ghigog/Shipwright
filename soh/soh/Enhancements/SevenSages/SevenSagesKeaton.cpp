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
 * **Pots and other fixed drops are covered too** as of 2026-08-06, through the new
 * VB_MODIFY_FIXED_DROP_QUANTITY fired from Item_DropCollectible (pots, crates, bushes, ~29 callers).
 * That function had no quantity to scale because it spawns exactly one item, so the hook supplies
 * one and the spawn is looped.
 *
 * The worry that deferred this - that callers depend on the single returned actor - was resolved by
 * keeping the return value exactly as it was: it is always the *first* spawn, and the extras are
 * spawned and let go, so no caller can observe the difference. Vanilla already randomizes each
 * drop's launch angle, which scatters the extras without any added offset.
 *
 * Two things stop this being a blunt multiply. The hook is not fired for the internal
 * `params|0x8000` calls Item_DropCollectibleRandom makes into the same function, which have already
 * been scaled once and would otherwise compound to 4x. And IsDuplicableDrop below refuses the item
 * types where a duplicate would be a genuine bug rather than a bonus - keys, heart pieces and
 * containers, fairies, and the ITEM00_SOH_GIVE_ITEM_ENTRY* types that carry randomizer checks.
 *
 * **Half price** hooks VB_MODIFY_SHOP_PRICE, fired from all three basePrice assignment sites in
 * EnGirlA_Init (z_en_girla.c) - the randomized-item override, the BetterBombchuShopping/normal
 * item-table branch, and the non-rando branch - so it covers every EnGirlA shop (Bazaar, Kakariko,
 * Zora shops, the Happy Mask Shop when RSK_MASK_QUEST opens it, etc.) from one patched field, since
 * basePrice is what both the price tag and the affordability/deduction checks read.
 *
 * **The price follows the mask on and off while standing in the shop** (2026-08-06). Init alone was
 * not enough: basePrice is assigned once at scene load, so equipping the mask at the counter left
 * every tag at full price. The player's framing for wanting this is haggling, and it is the natural
 * reading anyway - a mask you can put on and take off should do something when you do.
 *
 * Patching at the point of *use* was not an option: basePrice is read from more than twenty
 * affordability sites in z_en_girla.c plus the tag rendering plus the deduction, and they are not
 * funnelled through anything. So the field itself is kept correct instead. The VB hook records each
 * shop actor's undiscounted price as it is assigned, and a per-actor update hook re-derives
 * basePrice from that record every frame.
 *
 * Recording the full price rather than toggling in place is deliberate: halving is integer division,
 * so a 105-rupee item halves to 52 and doubling that back gives 104. Re-deriving from the stored
 * original means the price is exact in both directions however many times the mask goes on and off.
 *
 * The record is keyed by Actor* and cleared on scene init and play teardown, so a pointer can never
 * outlive its actor and be reused by a later shop.
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
 *
 * **The shopkeeper says so** (2026-08-06). The discount is otherwise silent - the tags just read
 * lower - so talking to a shop's owner while wearing the mask replaces their line with a
 * half-price greeting. Chosen over per-item custom text precisely because it costs one message
 * instead of one per shop item, and it lands at the moment the player is deciding whether to buy.
 *
 * The full-price *dialogue* of Medigoron and the carpet salesman still says 200 rupees even though
 * the gate is halved, and they are not EnOssan so this greeting does not reach them either. Both
 * are the same pre-existing limitation of those two VB hooks, noted above.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "overlays/actors/ovl_En_GirlA/z_en_girla.h"
#include "overlays/actors/ovl_En_Ossan/z_en_ossan.h"

#include "SevenSagesKeaton.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"

#include <unordered_map>

extern "C" PlayState* gPlayState;

namespace {

bool IsWearingKeatonMask() {
    Player* player = GET_PLAYER(gPlayState);
    return player != nullptr && player->currentMask == PLAYER_MASK_KEATON;
}

// Undiscounted price per shop actor, captured as EnGirlA_Init assigns it. See the header comment:
// this exists so the discount can be re-derived exactly rather than toggled in place, which integer
// division would make lossy.
std::unordered_map<Actor*, s16> sFullPrices;

s16 DiscountedPrice(s16 fullPrice) {
    return IsWearingKeatonMask() ? (s16)(fullPrice / 2) : fullPrice;
}

void OnShopItemUpdate(void* actorPtr) {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }

    EnGirlA* shopItem = static_cast<EnGirlA*>(actorPtr);
    auto entry = sFullPrices.find(&shopItem->actor);
    if (entry == sFullPrices.end()) {
        return;
    }

    shopItem->basePrice = DiscountedPrice(entry->second);
}

void ForgetShopPrices() {
    sFullPrices.clear();
}

// May this ITEM00_* type be duplicated by the mask? The spec's "resource" is generous - rupees and
// hearts count, not just ammo - so this is an exclusion list rather than an allowlist, and it errs
// towards refusing anything that isn't obviously a consumable.
//
// Duplicating any of these would be a real bug, not just odd:
//   - SMALL_KEY duplicates a dungeon key, which the key counter and the logic solver both track.
//   - HEART_PIECE / HEART_CONTAINER are unique progression, and heart pieces additionally feed
//     Sun's Song's temporary-heart cap through the lifetime counter.
//   - SOH_GIVE_ITEM_ENTRY / _GI are how SoH delivers a randomizer *check* as a collectible, so
//     doubling one duplicates a check - the same class of failure as gauntlet case 57.
//   - FLEXIBLE spawns En_Elf (a fairy) down a different branch entirely, not an EnItem00 at all.
//   - SOH_DUMMY is a placeholder with no pickup.
// Shields and tunics are excluded as equipment rather than resources: a second one is inert.
bool IsDuplicableDrop(s16 item00Type) {
    switch (item00Type) {
        case ITEM00_SMALL_KEY:
        case ITEM00_HEART_PIECE:
        case ITEM00_HEART_CONTAINER:
        case ITEM00_FLEXIBLE:
        case ITEM00_SOH_DUMMY:
        case ITEM00_SOH_GIVE_ITEM_ENTRY:
        case ITEM00_SOH_GIVE_ITEM_ENTRY_GI:
        case ITEM00_SHIELD_DEKU:
        case ITEM00_SHIELD_HYLIAN:
        case ITEM00_TUNIC_ZORA:
        case ITEM00_TUNIC_GORON:
            return false;
        default:
            return true;
    }
}

// The greeting itself. `&` is a line break once AutoFormat has run; the budget is roughly three
// lines of 35 characters, which is a readability guideline rather than a correctness one - see
// docs/custom-text-safety.md in the seven-sages repo.
//
// Phrased as the shopkeeper choosing to give the discount rather than as a system notice, since it
// arrives in their voice and every other line in the shop is in character. One line covers every
// EnOssan shop; per-shop variants would be nice and are not worth the table.
const char* kKeatonShopGreeting = "Hey, nice mask!&Tell you what - everything in here&is half price for you today.";

// True only while an EnOssan is delivering its talk-to-owner line.
//
// EnOssan_ChooseTalkToOwner() sets stateFlag *before* calling into sShopkeeperTalkOwner[], so by the
// time that function's Message_ContinueTextbox reaches an OnOpenText hook the flag already reads
// OSSAN_STATE_TALKING_TO_SHOPKEEPER. Every other shop message - purchase prompts, refusals, milk
// fanfare - is sent by a helper that sets its state *after* the textbox call, so none of them can be
// mistaken for this one. That matters: those carry choice control codes, and replacing one with a
// plain message softlocks the shop.
//
// Same predicate as IsShopOwnerTalking() in SevenSagesNpcHints.cpp, deliberately duplicated rather
// than shared - it is six lines, and hoisting it into a header would make one feature's delivery
// detail part of another's interface.
bool IsShopOwnerTalking() {
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr || player->talkActor == nullptr || player->talkActor->id != ACTOR_EN_OSSAN) {
        return false;
    }
    return ((EnOssan*)player->talkActor)->stateFlag == OSSAN_STATE_TALKING_TO_SHOPKEEPER;
}

void SpeakShopGreeting(uint16_t* textId, bool* loadFromMessageTable) {
    if (!SevenSagesKeatonClaimsShopGreeting()) {
        return;
    }

    CustomMessage msg(kKeatonShopGreeting);
    // AutoFormat() before LoadIntoFont() is mandatory - it is what appends the terminator and turns
    // `&` into a real line break. Skipping it leaves the message unterminated and Message_Decode
    // runs off the end of the buffer, crashing later in Interface_Draw. See docs/custom-text-safety.md.
    msg.AutoFormat();
    // A shop owner's line has to end with the EVENT control code, not the plain END that AutoFormat
    // appends. EnOssan_State_TalkingToShopkeeper only advances on Message_GetState() ==
    // TEXT_STATE_EVENT, and the message system will not close the box itself either because the shop
    // sets YREG(31) while browsing - so END here leaves the textbox up with every input dead. 0x0B is
    // also a decoder stop byte, so the swap is safe. Spelled out because message_data_fmt.h is not in
    // this TU and CustomMessageManager.h #undefs the MESSAGE_* macros it would provide.
    constexpr char kMessageEvent = '\x0B';
    msg.Replace(CustomMessage::MESSAGE_END(), std::string(1, kMessageEvent));
    msg.LoadIntoFont();
    *loadFromMessageTable = false;
}

} // namespace

bool SevenSagesKeatonClaimsShopGreeting() {
    return IS_RANDO && gPlayState != nullptr && IsWearingKeatonMask() && IsShopOwnerTalking();
}

static void RegisterSevenSagesKeaton() {
    COND_HOOK(OnOpenText, IS_RANDO, SpeakShopGreeting);

    COND_VB_SHOULD(VB_MODIFY_SHOP_PRICE, IS_RANDO, {
        Actor* shopActor = va_arg(args, Actor*);
        s16* basePrice = va_arg(args, s16*);
        // Record before discounting, so what is stored is always the undiscounted price even when
        // the mask is already on at scene load.
        sFullPrices[shopActor] = *basePrice;
        *basePrice = DiscountedPrice(*basePrice);
    });

    COND_ID_HOOK(OnActorUpdate, ACTOR_EN_GIRLA, IS_RANDO, OnShopItemUpdate);
    COND_HOOK(OnSceneInit, IS_RANDO, [](int16_t sceneNum) { ForgetShopPrices(); });
    COND_HOOK(OnPlayDestroy, IS_RANDO, ForgetShopPrices);

    COND_VB_SHOULD(VB_MODIFY_RANDOM_DROP_QUANTITY, IS_RANDO, {
        [[maybe_unused]] Actor* fromActor = va_arg(args, Actor*);
        s16* dropQuantity = va_arg(args, s16*);
        if (IsWearingKeatonMask()) {
            *dropQuantity *= 2;
        }
    });

    COND_VB_SHOULD(VB_MODIFY_FIXED_DROP_QUANTITY, IS_RANDO, {
        // va_arg promotes s16 to int; read it as int and narrow, or this reads garbage.
        s16 item00Type = (s16)va_arg(args, int);
        s16* dropQuantity = va_arg(args, s16*);
        if (IsWearingKeatonMask() && IsDuplicableDrop(item00Type)) {
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
