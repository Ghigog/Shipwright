#ifndef SEVEN_SAGES_STASH_H
#define SEVEN_SAGES_STASH_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Seven Sages trade box - the stash itself.
 *
 * ONE shared logical stash, with physical terminals around the overworld as access points into it.
 * Not N separate boxes. That is multiplayer-anchor.md decision 5, and the reason drives everything
 * else about the design: one shared stash turns "can A and B reach the SAME box at OVERLAPPING
 * times" into two independent and much weaker requirements - "can A reach any terminal at some
 * point", and the same for B. Deposits persist, so no live handshake is ever needed.
 *
 * That matters more now than when it was designed, because child and adult sages cannot see each
 * other at all (age dimensions). The box is the only way players in different ages can cooperate
 * on items, which is a standing argument against ever making a trade require both parties present.
 *
 * ── Why the contents do not live in gSaveContext ────────────────────────────────────────────
 *
 * The obvious home for save data is a new field on gSaveContext, and that is exactly what to avoid
 * here: it lives in soh/include/z64save.h, which most of the tree includes, and with no ccache a
 * change there costs an ~85 minute rebuild against a couple of minutes for a leaf file. See
 * CLAUDE.md. The stash keeps its own file-scope storage and registers a SaveManager section for
 * it, which is what SaveManager's custom-section API exists for and touches no shared header.
 *
 * ── Capacity is fixed, and deliberately small ───────────────────────────────────────────────
 *
 * SEVEN_SAGES_STASH_CAPACITY entries, flat array, no allocation. Two reasons. The relay drops
 * large packets - a ~110KB payload sent whole never arrived, which cost a playtest to find - so
 * anything that syncs wants to stay far below that. And a box that can absorb unlimited items is
 * a worse game object than one you have to make room in.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define SEVEN_SAGES_STASH_CAPACITY 24

// Everything below operates on the LOCAL copy of the stash only. Sync is a separate concern: the
// Anchor packet calls these on the receiving side after the world guard has accepted the message.
// Keeping the two apart is what lets the box work in single-player, where it is simply a chest
// that persists.

// How many items are currently in the stash.
uint8_t SevenSagesStash_Count(void);

// The item at an index, or RG_NONE (0) when out of range. `outDepositorSage` may be NULL; when
// given it receives the RO_SAGE_* of whoever put the item in, which is what lets a terminal say
// who left it rather than presenting an anonymous pile.
int16_t SevenSagesStash_Get(uint8_t index, uint8_t* outDepositorSage);

// Put an item in. False when the stash is full, which is the only failure - eligibility is the
// caller's business (SevenSagesTrade_CanDeposit) and is deliberately not re-checked here, so that
// a packet arriving from a teammate is not re-judged against THIS player's kit and inventory.
bool SevenSagesStash_Add(int16_t randomizerGet, uint8_t depositorSage);

// Take an item out by index, closing the gap. False when the index is out of range.
//
// Order is not meaningful to the design, but keeping it stable makes a withdrawal that races
// another client's withdrawal fail visibly (wrong item, refused) rather than silently taking a
// neighbour's.
bool SevenSagesStash_RemoveAt(uint8_t index);

// Empty the stash. Called at file creation; also the recovery path if a sync ever leaves it
// inconsistent.
void SevenSagesStash_Clear(void);

// Bumped on every local change, and carried by the sync packet purely so a receiver can tell a
// genuinely new state from an echo of its own. NOT a conflict resolver - see the race note in
// Packets/SevenSagesStash.cpp for what this does and does not protect against.
uint32_t SevenSagesStash_Revision(void);
void SevenSagesStash_SetRevision(uint32_t revision);

#ifdef __cplusplus
}
#endif

#endif // SEVEN_SAGES_STASH_H
