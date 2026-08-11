/*
 * Seven Sages trade box - the classification layer.
 *
 * See SevenSagesTrade.h for what the box is for and why it carries progression items. This file
 * decides three things and nothing else: what counts as a transferable object, what is bound to a
 * sage, and what the local player is allowed to let go of right now.
 *
 * ── Why an allowlist rather than "accept ITEMTYPE_EQUIP and ITEMTYPE_ITEM minus exceptions" ──
 *
 * The type-driven version was tried on paper first and rejected. ITEMTYPE_ITEM holds 175 of the
 * table's 302 entries, and the great majority of them are not objects a player hands over: the
 * twenty-odd shop and house keys, ten bean souls, nine boss souls, the *_INF cheat upgrades, the
 * ability items (RG_CLIMB, RG_CRAWL, RG_OPEN_CHEST, the RG_SPEAK_* set), ocarina buttons, heart
 * pieces and the Triforce. Filtering those out means a long exception list whose failure mode is
 * silent and bad - a missed entry makes something impossible tradeable, and nothing errors.
 *
 * An allowlist inverts that failure mode. A missing entry means an item simply is not tradeable
 * yet, which is visible, harmless and one line to fix. That is the right direction for this
 * codebase specifically - see CLAUDE.md's build traps, all three of which are silent failures.
 *
 * ── Known v1 exclusions, deliberate and pending a decision ───────────────────────────────────
 *
 * Three groups of otherwise-plausible items are left out of v1 on purpose, because each needs a
 * design answer rather than a table row:
 *
 *   - PROGRESSIVE CHAINS (Hookshot/Longshot, Bow, Slingshot, Scale, Strength, Ocarina, Wallet,
 *     Magic Meter, bomb and bombchu bags). The inventory stores a tier, not a count, so
 *     "deposit a Hookshot" is ambiguous for a player holding the Longshot: it could mean hand over
 *     the upgrade and drop to Hookshot, or hand over the whole chain. Both are defensible and they
 *     behave very differently in play.
 *   - BOTTLES AND CONTENTS. A bottle is a slot with mutable contents, so depositing one raises
 *     what happens to what is inside it.
 *   - THE ADULT TRADE QUEST (Pocket Egg through Claim Check). A timed NPC chain whose steps are
 *     driven by scene state, not just inventory.
 *   - THE GIANT'S KNIFE. It shares EQUIP_VALUE_SWORD_BIGGORON with the Biggoron Sword and has two
 *     inventory states of its own (EQUIP_INV_SWORD_BIGGORON while whole, _BROKENGIANTKNIFE once
 *     snapped), so a row for it would double-count the same equip bit and quietly defeat the
 *     last-in-category rule for swords. It is also a breakable version of an item already in the
 *     list, which is the same "not one stable object" problem as the three groups above.
 */

#include "SevenSagesTrade.h"

#include "soh/Enhancements/randomizer/savefile.h"
#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "soh/OTRGlobals.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "variables.h"
}

namespace {

// One row per tradeable item. `equipType` is -1 for anything that is not worn, which is what
// exempts it from the equip-lock and the last-in-category rule - those two only ever protect a
// player from stranding themselves without a tunic, boots, shield or sword.
//
// Both equip indices are carried because the two macros that need them use DIFFERENT enums, and
// they are off by one from each other in a way that compiles silently either way:
//
//   CHECK_OWNED_EQUIP (Inventory.equipment)  -> EQUIP_INV_*,   0-based, no "none" member
//   CUR_EQUIP_VALUE   (ItemEquips.equipment) -> EQUIP_VALUE_*, 1-based, EQUIP_*_NONE is 0
//
// Passing an EQUIP_VALUE_ to CHECK_OWNED_EQUIP tests the next item up in the category - asking
// about the Goron tunic and being told about the Zora one. Keep the pair together per row rather
// than converting between them at the call site.
struct TradeableItem {
    int16_t randomizerGet;
    int8_t equipType;  // EQUIP_TYPE_*, or -1
    int8_t equipInv;   // EQUIP_INV_*, for CHECK_OWNED_EQUIP
    int8_t equipValue; // EQUIP_VALUE_*, for CUR_EQUIP_VALUE
};

// The v1 tradeable set: equipment, the discrete key items, the spells, the arrow types and the
// masks. Every entry is a single physical object with no tier and no contents, which is exactly
// the property the exclusions at the top of this file are missing.
constexpr TradeableItem kTradeableItems[] = {
    // Swords. The Master Sword is included and it is worth being explicit about why, because it
    // is the one item here that changes what a player IS rather than what they can do: it drives
    // age switching, and age is the co-op dimension you can see teammates across. Handing it over
    // is a real decision with real consequences, and that is the same reading decision 7 took of
    // Ganon's Tower - the team consciously deciding who holds what is the intended texture, not a
    // hazard to design out. The last-in-category rule below still stops it being an accident.
    { RG_KOKIRI_SWORD, EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_KOKIRI, EQUIP_VALUE_SWORD_KOKIRI },
    { RG_MASTER_SWORD, EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_MASTER, EQUIP_VALUE_SWORD_MASTER },
    { RG_BIGGORON_SWORD, EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_BIGGORON, EQUIP_VALUE_SWORD_BIGGORON },

    // Shields.
    { RG_DEKU_SHIELD, EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_DEKU, EQUIP_VALUE_SHIELD_DEKU },
    { RG_HYLIAN_SHIELD, EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_HYLIAN, EQUIP_VALUE_SHIELD_HYLIAN },
    { RG_MIRROR_SHIELD, EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_MIRROR, EQUIP_VALUE_SHIELD_MIRROR },

    // Tunics and boots. The Kokiri tunic and Kokiri boots are absent deliberately: they are the
    // default state rather than acquirable items, and are never absent from an inventory.
    { RG_GORON_TUNIC, EQUIP_TYPE_TUNIC, EQUIP_INV_TUNIC_GORON, EQUIP_VALUE_TUNIC_GORON },
    { RG_ZORA_TUNIC, EQUIP_TYPE_TUNIC, EQUIP_INV_TUNIC_ZORA, EQUIP_VALUE_TUNIC_ZORA },
    { RG_IRON_BOOTS, EQUIP_TYPE_BOOTS, EQUIP_INV_BOOTS_IRON, EQUIP_VALUE_BOOTS_IRON },
    { RG_HOVER_BOOTS, EQUIP_TYPE_BOOTS, EQUIP_INV_BOOTS_HOVER, EQUIP_VALUE_BOOTS_HOVER },

    // Discrete key items.
    { RG_BOOMERANG, -1, 0, 0 },
    { RG_LENS_OF_TRUTH, -1, 0, 0 },
    { RG_MEGATON_HAMMER, -1, 0, 0 },
    { RG_STONE_OF_AGONY, -1, 0, 0 },
    { RG_GERUDO_MEMBERSHIP_CARD, -1, 0, 0 },

    // Spells. ITEMTYPE_ITEM and genuinely held objects, not Knowledge like the songs - decision 4
    // checked this specifically.
    { RG_DINS_FIRE, -1, 0, 0 },
    { RG_FARORES_WIND, -1, 0, 0 },
    { RG_NAYRUS_LOVE, -1, 0, 0 },

    // Arrow types. Each is a distinct item rather than a tier of one chain, so they are unaffected
    // by the progressive-chain problem that keeps the Bow itself out of v1. Note the Bow being
    // untradeable means these can be handed to someone who cannot fire them - which is a real
    // in-fiction bargain, not a bug, and the same shape as Rauru's kit pairing them deliberately.
    { RG_FIRE_ARROWS, -1, 0, 0 },
    { RG_ICE_ARROWS, -1, 0, 0 },
    { RG_LIGHT_ARROWS, -1, 0, 0 },

    // Masks. Phase 6 gave every one of these a real standing effect, which is what makes them
    // worth moving between players at all.
    { RG_KEATON_MASK, -1, 0, 0 },
    { RG_SKULL_MASK, -1, 0, 0 },
    { RG_SPOOKY_MASK, -1, 0, 0 },
    { RG_BUNNY_HOOD, -1, 0, 0 },
    { RG_GORON_MASK, -1, 0, 0 },
    { RG_ZORA_MASK, -1, 0, 0 },
    { RG_GERUDO_MASK, -1, 0, 0 },
    { RG_MASK_OF_TRUTH, -1, 0, 0 },
};

const TradeableItem* FindTradeable(int16_t randomizerGet) {
    for (const TradeableItem& item : kTradeableItems) {
        if (item.randomizerGet == randomizerGet) {
            return &item;
        }
    }
    return nullptr;
}

// How many items the player owns in one equip category, counting only the ones the box can move.
//
// Counting tradeable items rather than every owned value is the point: the Kokiri tunic and boots
// are always owned and never tradeable, so counting them would make the last-in-category rule
// never fire for tunics or boots at all. The question this answers is "if I hand this one over,
// have I anything left to hand over" - which is about the movable set.
int CountOwnedInCategory(int8_t equipType) {
    int owned = 0;
    for (const TradeableItem& item : kTradeableItems) {
        if (item.equipType != equipType) {
            continue;
        }
        if (CHECK_OWNED_EQUIP(equipType, item.equipInv)) {
            owned++;
        }
    }
    return owned;
}

} // namespace

extern "C" bool SevenSagesTrade_IsTransferableObject(int16_t randomizerGet) {
    return FindTradeable(randomizerGet) != nullptr;
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

    if (item->equipType >= 0) {
        if (CUR_EQUIP_VALUE(item->equipType) == item->equipValue) {
            return SEVEN_SAGES_TRADE_EQUIPPED;
        }
        if (CountOwnedInCategory(item->equipType) <= 1) {
            return SEVEN_SAGES_TRADE_LAST_IN_CATEGORY;
        }
    }

    return SEVEN_SAGES_TRADE_OK;
}

extern "C" const char* SevenSagesTrade_RefusalText(SevenSagesTradeVerdict verdict) {
    switch (verdict) {
        case SEVEN_SAGES_TRADE_OK:
            return "";
        case SEVEN_SAGES_TRADE_KIT_BOUND:
            return "This is yours alone. It was given to you with your name on it, and no other "
                   "exists in this age.";
        case SEVEN_SAGES_TRADE_EQUIPPED:
            return "You are wearing that. Take it off first.";
        case SEVEN_SAGES_TRADE_LAST_IN_CATEGORY:
            return "That is the only one you carry. Find another before you part with it.";
        case SEVEN_SAGES_TRADE_NOT_AN_OBJECT:
        default:
            return "That cannot be set down here.";
    }
}
