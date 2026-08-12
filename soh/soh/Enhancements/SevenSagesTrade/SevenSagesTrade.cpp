/*
 * Seven Sages trade box - classification and transfer.
 *
 * See SevenSagesTrade.h for what the box is for and why it carries progression items. This file
 * owns the table of what can move and the four inverse operations that move it.
 *
 * ── The four storage shapes, and why each needs its own inverse ──────────────────────────────
 *
 * "Remove an item" does not exist anywhere in vanilla or in rando - the game only ever gives. So
 * every take below is written against how Item_Give (z_parameter.c:1900+) and Inventory_ChangeUpgrade
 * put the item there in the first place:
 *
 *   EQUIP    inventory.equipment bitfield, one bit per owned sword/shield/tunic/boots.
 *            Take = clear the bit. Guarded by the equip-lock so the worn one is never removed.
 *   QUEST    questItems bitfield. Only two entries here are quest bits rather than Knowledge -
 *            the Stone of Agony and the Gerudo Card, both real held objects (SevenSagesCoop.h
 *            makes the same distinction for what crosses in co-op). Take = clear the bit.
 *   SLOT     inventory.items[SLOT(item)], one item id. Take = set the slot to ITEM_NONE.
 *   UPGRADE  a TIER in inventory.upgrades, not an object. Take = decrement by one.
 *
 * ── Progressive chains are tiers, and that is what makes them safe to trade ──────────────────
 *
 * An earlier version of this file excluded every progressive chain on the grounds that "deposit a
 * Hookshot" is ambiguous for someone holding the Longshot. That was wrong, and the reason is worth
 * keeping because it is the same reason the transfers are conservative at all.
 *
 * The two chains are stored differently and both have an obvious inverse:
 *
 *   - Hookshot/Longshot and Fairy Ocarina/Ocarina of Time are SLOT items. The slot holds exactly
 *     one of the two ids. Hand over what is in the slot and clear it. Nothing is ambiguous: you
 *     give away the one you have.
 *   - Scale, Strength, Wallet, Quiver, Bullet Bag, Bomb Bag, sticks and nuts are UPGRADE tiers,
 *     and a tier is really a COUNT of progressive items collected. Golden Scale means two were
 *     picked up. So the inverse of "collect one" is "decrement one", and a transfer moves exactly
 *     one tier: the giver drops from Golden to Silver, the receiver rises from nothing to Silver.
 *     Total tiers across the team are unchanged, which is the invariant that matters.
 *
 * Decrementing rather than zeroing is what keeps that true. Zeroing the giver would destroy a tier
 * whenever they held more than one, which is exactly the kind of silent non-conservation this
 * module is built to avoid.
 *
 * ── Families that share one slot ────────────────────────────────────────────────────────────
 *
 * gItemSlots (z_inventory.c:205) maps all eight masks onto SLOT_TRADE_CHILD, all adult trade-quest
 * items onto SLOT_TRADE_ADULT, and every bottle type onto SLOT_BOTTLE_1. So these are not
 * independently owned items at all - they are the CONTENTS of one slot, and the player holds
 * whichever one is in there. That makes them the easiest things in the file to move correctly:
 * the slot value is the object.
 *
 * Bottles are the one case needing more than the SLOT macro, because there are four bottle slots
 * and INV_CONTENT only ever addresses the first. FindBottleSlot scans all four.
 */

#include "SevenSagesTrade.h"

#include "soh/Enhancements/randomizer/savefile.h"
#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/OTRGlobals.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"

// Not declared in any header the C++ side sees - same one-line extern the Anchor
// files use. It has to sit inside the extern "C" block: MSVC mangles global
// variable names, so a C++-linkage declaration here emits ?gPlayState@@3PEAU...
// and fails to link against the C definition. The Itanium ABI does not mangle
// variables, which is why a macOS or Linux build links either way.
extern PlayState* gPlayState;
}

namespace {

enum StorageKind {
    STORE_EQUIP,   // inventory.equipment bit
    STORE_QUEST,   // questItems bit
    STORE_SLOT,    // inventory.items[SLOT(itemId)] == itemId
    STORE_BOTTLE,  // one of the four bottle slots holds itemId
    STORE_UPGRADE, // a tier in inventory.upgrades
};

// One row per tradeable item.
//
// `a` and `b` carry the two indices a row needs, and what they mean depends on `kind`. They are
// kept as one pair rather than a union of named fields because every row needs exactly two and the
// table stays readable in a single column layout.
//
//   STORE_EQUIP    a = EQUIP_TYPE_*, b = EQUIP_INV_*        (plus equipValue, see below)
//   STORE_QUEST    a = QUEST_*,      b = unused
//   STORE_SLOT     a = ITEM_*,       b = unused
//   STORE_BOTTLE   a = ITEM_*,       b = unused
//   STORE_UPGRADE  a = UPG_*,        b = the tier this row represents
//
// equipValue is separate and only meaningful for STORE_EQUIP, because the two equipment macros
// take DIFFERENT enums and they are off by one in a way that compiles silently either way:
//
//   CHECK_OWNED_EQUIP (Inventory.equipment)  -> EQUIP_INV_*,   0-based, no "none" member
//   CUR_EQUIP_VALUE   (ItemEquips.equipment) -> EQUIP_VALUE_*, 1-based, EQUIP_*_NONE is 0
//
// Passing an EQUIP_VALUE_ to CHECK_OWNED_EQUIP tests the next item up in the category - asking
// about the Goron tunic and being told about the Zora one.
struct TradeableItem {
    int16_t randomizerGet;
    StorageKind kind;
    int16_t a;
    int16_t b;
    int8_t equipValue; // EQUIP_VALUE_*, STORE_EQUIP only
};

constexpr TradeableItem kTradeableItems[] = {
    // ── Equipment ───────────────────────────────────────────────────────────────────────────
    // The Master Sword is included, and it is the one item here that changes what a player IS
    // rather than what they can do: it drives age switching, and age is the dimension you can see
    // teammates across. Handing it over is a real decision with real consequences, which is the
    // same reading decision 7 took of Ganon's Tower - the team consciously deciding who holds what
    // is the intended texture, not a hazard to design out. The last-in-category rule still stops
    // it being an accident.
    //
    // The Giant's Knife is absent on purpose: it shares EQUIP_VALUE_SWORD_BIGGORON with the
    // Biggoron Sword and has a second inventory state of its own once snapped
    // (EQUIP_INV_SWORD_BROKENGIANTKNIFE), so a row for it would double-count the same equip bit
    // and quietly defeat the last-in-category rule for swords.
    { RG_KOKIRI_SWORD, STORE_EQUIP, EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_KOKIRI, EQUIP_VALUE_SWORD_KOKIRI },
    { RG_MASTER_SWORD, STORE_EQUIP, EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_MASTER, EQUIP_VALUE_SWORD_MASTER },
    { RG_BIGGORON_SWORD, STORE_EQUIP, EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_BIGGORON, EQUIP_VALUE_SWORD_BIGGORON },
    { RG_DEKU_SHIELD, STORE_EQUIP, EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_DEKU, EQUIP_VALUE_SHIELD_DEKU },
    { RG_HYLIAN_SHIELD, STORE_EQUIP, EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_HYLIAN, EQUIP_VALUE_SHIELD_HYLIAN },
    { RG_MIRROR_SHIELD, STORE_EQUIP, EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_MIRROR, EQUIP_VALUE_SHIELD_MIRROR },
    // Kokiri tunic and Kokiri boots are absent deliberately: they are the default state rather
    // than acquirable items, and are never absent from an inventory.
    { RG_GORON_TUNIC, STORE_EQUIP, EQUIP_TYPE_TUNIC, EQUIP_INV_TUNIC_GORON, EQUIP_VALUE_TUNIC_GORON },
    { RG_ZORA_TUNIC, STORE_EQUIP, EQUIP_TYPE_TUNIC, EQUIP_INV_TUNIC_ZORA, EQUIP_VALUE_TUNIC_ZORA },
    { RG_IRON_BOOTS, STORE_EQUIP, EQUIP_TYPE_BOOTS, EQUIP_INV_BOOTS_IRON, EQUIP_VALUE_BOOTS_IRON },
    { RG_HOVER_BOOTS, STORE_EQUIP, EQUIP_TYPE_BOOTS, EQUIP_INV_BOOTS_HOVER, EQUIP_VALUE_BOOTS_HOVER },

    // ── Quest-bit objects ───────────────────────────────────────────────────────────────────
    // Real held objects that happen to live in the questItems bitfield, unlike the medallions,
    // songs and stones around them which are Knowledge and never trade.
    { RG_STONE_OF_AGONY, STORE_QUEST, QUEST_STONE_OF_AGONY, 0, 0 },
    { RG_GERUDO_MEMBERSHIP_CARD, STORE_QUEST, QUEST_GERUDO_CARD, 0, 0 },

    // ── Single-item slots ───────────────────────────────────────────────────────────────────
    { RG_BOOMERANG, STORE_SLOT, ITEM_BOOMERANG, 0, 0 },
    { RG_LENS_OF_TRUTH, STORE_SLOT, ITEM_LENS, 0, 0 },
    { RG_MEGATON_HAMMER, STORE_SLOT, ITEM_HAMMER, 0, 0 },
    { RG_DINS_FIRE, STORE_SLOT, ITEM_DINS_FIRE, 0, 0 },
    { RG_FARORES_WIND, STORE_SLOT, ITEM_FARORES_WIND, 0, 0 },
    { RG_NAYRUS_LOVE, STORE_SLOT, ITEM_NAYRUS_LOVE, 0, 0 },
    { RG_FIRE_ARROWS, STORE_SLOT, ITEM_ARROW_FIRE, 0, 0 },
    { RG_ICE_ARROWS, STORE_SLOT, ITEM_ARROW_ICE, 0, 0 },
    { RG_LIGHT_ARROWS, STORE_SLOT, ITEM_ARROW_LIGHT, 0, 0 },

    // The two slot-based progressive chains. Each row is one concrete tier, and the slot holds
    // exactly one of them, so "give away what you have" needs no interpretation.
    { RG_HOOKSHOT, STORE_SLOT, ITEM_HOOKSHOT, 0, 0 },
    { RG_LONGSHOT, STORE_SLOT, ITEM_LONGSHOT, 0, 0 },
    { RG_FAIRY_OCARINA, STORE_SLOT, ITEM_OCARINA_FAIRY, 0, 0 },
    { RG_OCARINA_OF_TIME, STORE_SLOT, ITEM_OCARINA_TIME, 0, 0 },

    // Masks. All eight share SLOT_TRADE_CHILD, so the slot value is the object.
    { RG_KEATON_MASK, STORE_SLOT, ITEM_MASK_KEATON, 0, 0 },
    { RG_SKULL_MASK, STORE_SLOT, ITEM_MASK_SKULL, 0, 0 },
    { RG_SPOOKY_MASK, STORE_SLOT, ITEM_MASK_SPOOKY, 0, 0 },
    { RG_BUNNY_HOOD, STORE_SLOT, ITEM_MASK_BUNNY, 0, 0 },
    { RG_GORON_MASK, STORE_SLOT, ITEM_MASK_GORON, 0, 0 },
    { RG_ZORA_MASK, STORE_SLOT, ITEM_MASK_ZORA, 0, 0 },
    { RG_GERUDO_MASK, STORE_SLOT, ITEM_MASK_GERUDO, 0, 0 },
    { RG_MASK_OF_TRUTH, STORE_SLOT, ITEM_MASK_TRUTH, 0, 0 },

    // ── Bottles ─────────────────────────────────────────────────────────────────────────────
    // The bottle and its contents move together, because they are one slot value. Handing someone
    // a bottle of blue fire is a single transfer, which is what makes it worth doing at all.
    { RG_EMPTY_BOTTLE, STORE_BOTTLE, ITEM_BOTTLE, 0, 0 },
    { RG_BOTTLE_WITH_MILK, STORE_BOTTLE, ITEM_MILK_BOTTLE, 0, 0 },
    { RG_BOTTLE_WITH_RED_POTION, STORE_BOTTLE, ITEM_POTION_RED, 0, 0 },
    { RG_BOTTLE_WITH_GREEN_POTION, STORE_BOTTLE, ITEM_POTION_GREEN, 0, 0 },
    { RG_BOTTLE_WITH_BLUE_POTION, STORE_BOTTLE, ITEM_POTION_BLUE, 0, 0 },
    { RG_BOTTLE_WITH_FAIRY, STORE_BOTTLE, ITEM_FAIRY, 0, 0 },
    { RG_BOTTLE_WITH_FISH, STORE_BOTTLE, ITEM_FISH, 0, 0 },
    { RG_BOTTLE_WITH_BLUE_FIRE, STORE_BOTTLE, ITEM_BLUE_FIRE, 0, 0 },
    { RG_BOTTLE_WITH_BUGS, STORE_BOTTLE, ITEM_BUG, 0, 0 },
    { RG_BOTTLE_WITH_POE, STORE_BOTTLE, ITEM_POE, 0, 0 },
    { RG_BOTTLE_WITH_BIG_POE, STORE_BOTTLE, ITEM_BIG_POE, 0, 0 },

    // ── Upgrade tiers ───────────────────────────────────────────────────────────────────────
    // One row per tier. Depositing moves exactly one tier, so a Golden Scale holder drops to
    // Silver rather than to nothing - see the header comment on conservation.
    //
    // The Bow, Slingshot and Bomb Bag rows are the quiver / bullet bag / bomb bag tiers, which is
    // where rando actually stores those chains. Dropping to tier 0 also empties the matching item
    // slot, which TakeUpgrade handles.
    // EXACTLY ONE ROW PER CHAIN, and it must be the PROGRESSIVE form rather than a concrete tier
    // name. This is the whole conservation argument, and getting it wrong is silent:
    //
    // A row for RG_GOLDEN_GAUNTLETS would take one tier from the giver (3 -> 2, per TakeUpgrade)
    // but hand the receiver Golden Gauntlets outright, i.e. tier 3. One tier out, three tiers in.
    // Storing RG_PROGRESSIVE_STRENGTH instead means the give path increments by one, which is the
    // exact inverse of the take - the giver drops to Silver and the receiver rises to Bracelet.
    //
    // `b` is 1 for every row because it is now only asking "is this chain owned at all"; which
    // tier the player is on is read live in TakeUpgrade.
    { RG_PROGRESSIVE_STRENGTH, STORE_UPGRADE, UPG_STRENGTH, 1, 0 },
    { RG_PROGRESSIVE_SCALE, STORE_UPGRADE, UPG_SCALE, 1, 0 },
    { RG_PROGRESSIVE_BOW, STORE_UPGRADE, UPG_QUIVER, 1, 0 },
    { RG_PROGRESSIVE_SLINGSHOT, STORE_UPGRADE, UPG_BULLET_BAG, 1, 0 },
    { RG_PROGRESSIVE_BOMB_BAG, STORE_UPGRADE, UPG_BOMB_BAG, 1, 0 },
};

// ITEM_* ids for the equipment rows. A STORE_EQUIP row's `a` is an EQUIP_TYPE_* and carries no
// ITEM_ id at all, so the bridge between the two has to be spelled out. Kokiri tunic and Kokiri
// boots are absent for the same reason they are absent from kTradeableItems: they are the default
// state rather than acquirable items.
struct ItemIdToRandomizerGet {
    int16_t itemId;
    int16_t randomizerGet;
};

constexpr ItemIdToRandomizerGet kEquipmentItemIds[] = {
    { ITEM_SWORD_KOKIRI, RG_KOKIRI_SWORD },   { ITEM_SWORD_MASTER, RG_MASTER_SWORD },
    { ITEM_SWORD_BGS, RG_BIGGORON_SWORD },    { ITEM_SHIELD_DEKU, RG_DEKU_SHIELD },
    { ITEM_SHIELD_HYLIAN, RG_HYLIAN_SHIELD }, { ITEM_SHIELD_MIRROR, RG_MIRROR_SHIELD },
    { ITEM_TUNIC_GORON, RG_GORON_TUNIC },     { ITEM_TUNIC_ZORA, RG_ZORA_TUNIC },
    { ITEM_BOOTS_IRON, RG_IRON_BOOTS },       { ITEM_BOOTS_HOVER, RG_HOVER_BOOTS },
};

int16_t ItemIdForRandomizerGet(int16_t randomizerGet) {
    for (const ItemIdToRandomizerGet& row : kEquipmentItemIds) {
        if (row.randomizerGet == randomizerGet) {
            return row.itemId;
        }
    }
    return ITEM_NONE;
}

/**
 * Clear `itemId` off every button it is assigned to.
 *
 * ── Removing an item from the inventory is not enough on its own ────────────────────────────
 *
 * gSaveContext.equips.buttonItems holds an ITEM_ id per button, INDEPENDENTLY of whether the
 * inventory still contains it. Emptying the inventory slot alone leaves the button naming an item
 * the player no longer owns - and it stays usable, because using a C item reads the button, not
 * the slot. Deposit the Lens of Truth off a C button and you get one copy on the beggar's shelf
 * and one still on the button, which is exactly what happened in testing.
 *
 * Vanilla never hits this because Inventory_DeleteItem does both halves together
 * (z_parameter.c:2612) - it clears the slot and then sweeps buttonItems for the same id. This is
 * that sweep, split out so it can also cover bottles and equipment, neither of which goes through
 * Inventory_DeleteItem.
 *
 * Note the loop starts at 1, as vanilla's does: index 0 is the B button, whose item is not a
 * C-assignable inventory item and must not be cleared this way.
 */
void UnassignFromButtons(int16_t itemId) {
    if (itemId == ITEM_NONE) {
        return;
    }
    for (size_t i = 1; i < ARRAY_COUNT(gSaveContext.equips.buttonItems); i++) {
        if (gSaveContext.equips.buttonItems[i] == itemId) {
            gSaveContext.equips.buttonItems[i] = ITEM_NONE;
            gSaveContext.equips.cButtonSlots[i - 1] = SLOT_NONE;
        }
    }
}

const TradeableItem* FindTradeable(int16_t randomizerGet) {
    for (const TradeableItem& item : kTradeableItems) {
        if (item.randomizerGet == randomizerGet) {
            return &item;
        }
    }
    return nullptr;
}

// Which of the four bottle slots holds this bottle type, or -1. INV_CONTENT cannot be used because
// gItemSlots maps every bottle id to SLOT_BOTTLE_1, so it only ever addresses the first of four.
int FindBottleSlot(int16_t itemId) {
    for (int slot = SLOT_BOTTLE_1; slot <= SLOT_BOTTLE_1 + 3; slot++) {
        if (gSaveContext.inventory.items[slot] == itemId) {
            return slot;
        }
    }
    return -1;
}

bool IsHeld(const TradeableItem& item) {
    switch (item.kind) {
        case STORE_EQUIP:
            return CHECK_OWNED_EQUIP(item.a, item.b) != 0;
        case STORE_QUEST:
            return (gSaveContext.inventory.questItems & gBitFlags[item.a]) != 0;
        case STORE_SLOT:
            return INV_CONTENT(item.a) == item.a;
        case STORE_BOTTLE:
            return FindBottleSlot(item.a) >= 0;
        case STORE_UPGRADE:
            // "Do you own this chain at all". What leaves on a deposit is one tier off whatever
            // the player currently holds, never this row's tier - see the table comment.
            return CUR_UPG_VALUE(item.a) >= item.b;
    }
    return false;
}

// How many items the player owns in one equip category, counting only the ones the box can move.
//
// Counting tradeable items rather than every owned value is the point: the Kokiri tunic and boots
// are always owned and never tradeable, so counting them would make the last-in-category rule
// never fire for tunics or boots at all. The question this answers is "if I hand this one over,
// have I anything left" - which is about the movable set.
int CountOwnedInCategory(int16_t equipType) {
    int owned = 0;
    for (const TradeableItem& item : kTradeableItems) {
        if (item.kind == STORE_EQUIP && item.a == equipType && CHECK_OWNED_EQUIP(item.a, item.b)) {
            owned++;
        }
    }
    return owned;
}

} // namespace

extern "C" bool SevenSagesTrade_IsTransferableObject(int16_t randomizerGet) {
    return FindTradeable(randomizerGet) != nullptr;
}

// ITEM_* is what a C button holds; the stash speaks RandomizerGet. The C-button deposit needs the
// bridge between them (SevenSagesBeggarShop.cpp).
//
// Equipment is spelled out rather than derived, because a STORE_EQUIP row's `a` is an EQUIP_TYPE_*
// and carries no ITEM_ id at all - there is nothing in kTradeableItems to match against. Kokiri
// tunic and Kokiri boots are absent for the same reason they are absent from the table above: they
// are the default state rather than acquirable items.
extern "C" int16_t SevenSagesTrade_RandomizerGetForItemId(int16_t itemId) {
    if (itemId == ITEM_NONE) {
        return RG_NONE;
    }

    for (const ItemIdToRandomizerGet& row : kEquipmentItemIds) {
        if (row.itemId == itemId) {
            return row.randomizerGet;
        }
    }

    // Everything else - single slots, masks, bottles - already stores its ITEM_ id in the table's
    // `a` field, so no second list is needed and the two cannot drift apart.
    for (const TradeableItem& item : kTradeableItems) {
        if ((item.kind == STORE_SLOT || item.kind == STORE_BOTTLE) && item.a == itemId) {
            return item.randomizerGet;
        }
    }

    return RG_NONE;
}

extern "C" SevenSagesTradeVerdict SevenSagesTrade_CanDeposit(int16_t randomizerGet) {
    const TradeableItem* item = FindTradeable(randomizerGet);
    if (item == nullptr) {
        return SEVEN_SAGES_TRADE_NOT_AN_OBJECT;
    }

    // Kit binding first, because it is the one refusal that is permanent for the whole run. An
    // equipped or last-in-category item becomes depositable once the player finds another; a kit
    // item never does, and saying so plainly is better than implying a workaround exists.
    if (Randomizer_IsLocalSageKitItem(randomizerGet)) {
        return SEVEN_SAGES_TRADE_KIT_BOUND;
    }

    if (!IsHeld(*item)) {
        return SEVEN_SAGES_TRADE_NOT_HELD;
    }

    if (item->kind == STORE_EQUIP) {
        if (CUR_EQUIP_VALUE(item->a) == item->equipValue) {
            return SEVEN_SAGES_TRADE_EQUIPPED;
        }
        if (CountOwnedInCategory(item->a) <= 1) {
            return SEVEN_SAGES_TRADE_LAST_IN_CATEGORY;
        }
    }

    return SEVEN_SAGES_TRADE_OK;
}

extern "C" bool SevenSagesTrade_TakeItem(int16_t randomizerGet) {
    const TradeableItem* item = FindTradeable(randomizerGet);
    if (item == nullptr || !IsHeld(*item)) {
        return false;
    }

    switch (item->kind) {
        case STORE_EQUIP:
            gSaveContext.inventory.equipment &= ~OWNED_EQUIP_FLAG(item->a, item->b);
            // Equipment reaches a C button through the Assignable Tunics and Boots enhancement, so
            // it ghosts the same way an inventory item does if the button is left naming it.
            UnassignFromButtons(ItemIdForRandomizerGet(item->randomizerGet));
            return true;

        case STORE_QUEST:
            gSaveContext.inventory.questItems &= ~gBitFlags[item->a];
            return true;

        case STORE_SLOT:
            INV_CONTENT(item->a) = ITEM_NONE;
            UnassignFromButtons(item->a);
            return true;

        case STORE_BOTTLE: {
            const int slot = FindBottleSlot(item->a);
            if (slot < 0) {
                return false;
            }
            gSaveContext.inventory.items[slot] = ITEM_NONE;
            UnassignFromButtons(item->a);
            return true;
        }

        case STORE_UPGRADE: {
            // One tier, never a reset to zero - see the conservation note at the top of the file.
            const int16_t tier = (int16_t)CUR_UPG_VALUE(item->a);
            Inventory_ChangeUpgrade(item->a, tier - 1);

            // The three chains that also own an inventory slot lose it at tier 0: without a quiver
            // there is no bow, and leaving the slot populated would show an item the player can no
            // longer use.
            if (tier - 1 <= 0) {
                if (item->a == UPG_QUIVER) {
                    INV_CONTENT(ITEM_BOW) = ITEM_NONE;
                } else if (item->a == UPG_BULLET_BAG) {
                    INV_CONTENT(ITEM_SLINGSHOT) = ITEM_NONE;
                } else if (item->a == UPG_BOMB_BAG) {
                    INV_CONTENT(ITEM_BOMB) = ITEM_NONE;
                }
            }
            return true;
        }
    }

    return false;
}

extern "C" bool SevenSagesTrade_GiveItem(int16_t randomizerGet) {
    const TradeableItem* item = FindTradeable(randomizerGet);
    if (item == nullptr || gPlayState == nullptr) {
        return false;
    }

    // ── Written as the exact inverse of TakeItem, against the same table row ────────────────
    //
    // Two earlier versions of this went through the engine's give paths instead, and both were
    // wrong in ways that only showed up in play:
    //
    //   GiveItemEntryWithoutActor does not give anything. It sets player->getItemEntry and leaves
    //   Player to run the hold-it-overhead animation on a later frame - so it refuses outright in
    //   a range of player states (z_actor.c:2028), and cannot run at all while the player is
    //   halted, which he always is at the beggar. The result was a withdrawal that deleted the
    //   stash entry and handed over nothing.
    //
    //   Randomizer_Item_Give applies immediately, which fixed that, but it is randomizer
    //   machinery: it dereferences OTRGlobals::Instance->gRandomizer directly (randomizer.cpp:1142
    //   and again at RG_CHILD_WALLET) and reads rando settings and RandomizerInf flags throughout.
    //   Seven Sages is its own quest id, not QUEST_RANDOMIZER, so that context is not guaranteed to
    //   exist - and calling it crashed on the first withdrawal.
    //
    // The box only ever moves items that already have a row here, and TakeItem already knows how to
    // remove each of the five storage shapes. Undoing exactly that is self-contained: no cutscene
    // to sit through, no player-state gate to be refused by, and no dependency on a randomizer
    // context that a Seven Sages file has no business assuming.
    switch (item->kind) {
        case STORE_EQUIP:
            gSaveContext.inventory.equipment |= OWNED_EQUIP_FLAG(item->a, item->b);
            return true;

        case STORE_QUEST:
            gSaveContext.inventory.questItems |= gBitFlags[item->a];
            return true;

        case STORE_SLOT:
            INV_CONTENT(item->a) = (u8)item->a;
            return true;

        case STORE_BOTTLE: {
            // Into the first empty bottle slot. Refusing when all four are full is what keeps the
            // caller able to leave the item on the shelf rather than dropping it on the floor.
            for (int slot = SLOT_BOTTLE_1; slot < SLOT_BOTTLE_1 + 4; slot++) {
                if (gSaveContext.inventory.items[slot] == ITEM_NONE) {
                    gSaveContext.inventory.items[slot] = (u8)item->a;
                    return true;
                }
            }
            return false;
        }

        case STORE_UPGRADE: {
            // One tier, mirroring the single-tier decrement TakeItem applies.
            const int16_t tier = (int16_t)CUR_UPG_VALUE(item->a);
            Inventory_ChangeUpgrade(item->a, tier + 1);

            // The three chains that also own an inventory slot get it back on the way up, for the
            // same reason they lose it on the way down: a quiver with no bow in the slot is not
            // usable.
            if (tier == 0) {
                if (item->a == UPG_QUIVER) {
                    INV_CONTENT(ITEM_BOW) = ITEM_BOW;
                } else if (item->a == UPG_BULLET_BAG) {
                    INV_CONTENT(ITEM_SLINGSHOT) = ITEM_SLINGSHOT;
                } else if (item->a == UPG_BOMB_BAG) {
                    INV_CONTENT(ITEM_BOMB) = ITEM_BOMB;
                }
            }
            return true;
        }
    }

    return false;
}

extern "C" const char* SevenSagesTrade_RefusalText(SevenSagesTradeVerdict verdict) {
    switch (verdict) {
        case SEVEN_SAGES_TRADE_OK:
            return "";
        case SEVEN_SAGES_TRADE_KIT_BOUND:
            return "This is yours alone. It was given to you with your name on it, and no other "
                   "exists in this age.";
        case SEVEN_SAGES_TRADE_NOT_HELD:
            return "You are not carrying that.";
        case SEVEN_SAGES_TRADE_EQUIPPED:
            return "You are wearing that. Take it off first.";
        case SEVEN_SAGES_TRADE_LAST_IN_CATEGORY:
            return "That is the only one you carry. Find another before you part with it.";
        case SEVEN_SAGES_TRADE_NOT_AN_OBJECT:
        default:
            return "That cannot be set down here.";
    }
}
