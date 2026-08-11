#ifndef SEVEN_SAGES_TRADE_H
#define SEVEN_SAGES_TRADE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Seven Sages trade box - what may cross between sages, and how it moves.
 *
 * Design and the decisions behind it are in seven-sages/docs/trading-handoff.md. The short version
 * of why the box exists at all is below, because it is the part that is easy to get backwards.
 *
 * ── Why a trade box is required rather than a convenience ───────────────────────────────────
 *
 * Co-op shares the WORLD and splits the ITEMS. SetFlag/SetCheckStatus cross between clients while
 * GiveItem is suppressed except for Knowledge (GiveItem.cpp), so opening a chest consumes it for
 * the whole team and hands the contents to the opener alone. The world's items are partitioned
 * across players before this module exists.
 *
 * The generator, meanwhile, reasons about the team as a SINGLE agent holding the union of every
 * rostered kit - see the comment in Randomizer_ApplySageGenerationSettings. That is sound for
 * kits and for world-state gates, because a hammer wall smashed by Darunia is smashed for
 * everyone. It is NOT sound for items found along the way, which cannot move.
 *
 * The honest statement of the cost: this does not by itself make seeds unbeatable, since whoever
 * holds the Hookshot can personally go and do every Hookshot check. What it does is concentrate
 * progression on whoever got lucky, which is the opposite of the intended co-op feel. The box is
 * the mechanism that makes "the team is one agent" true rather than aspirational.
 *
 * This is why the box deliberately carries progression items, reversing the "non-progression
 * equipment only" scoping in multiplayer-anchor.md decision 4. That scoping existed to protect
 * the single-inventory solver assumption, which co-op had already broken in the other direction -
 * and read literally against item_list.cpp's `advancement` flag it permits exactly two items of
 * consequence, Deku Shield and Hylian Shield.
 *
 * ── Every transfer is conservative ──────────────────────────────────────────────────────────
 *
 * The one invariant this module exists to hold: a deposit removes exactly what a withdrawal adds.
 * Nothing is duplicated and nothing evaporates. Four storage shapes each need their own inverse,
 * and getting one wrong would quietly break that invariant rather than error - see the storage
 * table in SevenSagesTrade.cpp for how each is stored and undone.
 */

#ifdef __cplusplus
extern "C" {
#endif

// Why an item cannot be deposited. SEVEN_SAGES_TRADE_OK is the only value that permits it. The UI
// shows the matching line from SevenSagesTrade_RefusalText so a refusal always explains itself - a
// greyed-out slot with no reason is the failure mode this enum exists to avoid.
typedef enum {
    SEVEN_SAGES_TRADE_OK,
    // Not a discrete transferable object at all: Knowledge (songs, medallions, stones), dungeon
    // keys/maps/compasses, rupees, refills, events.
    SEVEN_SAGES_TRADE_NOT_AN_OBJECT,
    // Part of the local sage's starting kit, and so bound to them for the run.
    SEVEN_SAGES_TRADE_KIT_BOUND,
    // Not currently held, so there is nothing to hand over.
    SEVEN_SAGES_TRADE_NOT_HELD,
    // Currently worn or equipped. Swap to something else first.
    SEVEN_SAGES_TRADE_EQUIPPED,
    // The only tunic / boots / shield / sword owned. Depositing it strands the sage even though it
    // is not currently equipped.
    SEVEN_SAGES_TRADE_LAST_IN_CATEGORY,
} SevenSagesTradeVerdict;

// Is this item the kind of thing the box can hold at all - a discrete object, independent of who
// is holding it or what else they own?
//
// Deliberately does NOT consult item_list.cpp's `advancement` flag: see the header comment for why
// progression items are in scope.
bool SevenSagesTrade_IsTransferableObject(int16_t randomizerGet);

// The full deposit test for the local player right now. Applies, in order: the category test
// above, kit binding, "do you actually hold this", the equip-lock, and the last-in-category rule.
//
// The last two are decision 6 as extended on 2026-08-11. The equip-lock alone was locked earlier
// and confirmed against a real Hover Boots case; the last-in-category half closes the gap it left
// open, where a sage unequips their only tunic and then deposits it.
SevenSagesTradeVerdict SevenSagesTrade_CanDeposit(int16_t randomizerGet);

// Remove an item from the local inventory, as the deposit half of a transfer. Returns false and
// changes nothing if the item is not actually held, so a lost packet or a double-click cannot
// conjure a second copy into the stash.
//
// Callers must check SevenSagesTrade_CanDeposit first; this deliberately does not re-run the
// policy rules, only the "is it there" check that keeps the transfer conservative.
bool SevenSagesTrade_TakeItem(int16_t randomizerGet);

// Add an item to the local inventory, as the withdrawal half. Routed through the ordinary
// GiveItemEntryWithoutActor path so that progressive tiers, ammo and side effects behave exactly
// as they would from a chest.
bool SevenSagesTrade_GiveItem(int16_t randomizerGet);

// A short player-facing sentence for a verdict, suitable for a textbox. Never NULL.
const char* SevenSagesTrade_RefusalText(SevenSagesTradeVerdict verdict);

#ifdef __cplusplus
}
#endif

#endif // SEVEN_SAGES_TRADE_H
