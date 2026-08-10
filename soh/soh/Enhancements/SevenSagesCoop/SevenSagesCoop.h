#ifndef SEVEN_SAGES_COOP_H
#define SEVEN_SAGES_COOP_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Seven Sages co-op - Anchor adapted for distinct simultaneous characters.
 *
 * Anchor's own model is "converge every player to one identical inventory" (see
 * seven-sages/docs/multiplayer-anchor.md, the architecture reality check). This module keeps
 * Anchor's shared-WORLD layer - check status, scene flags, entrance discovery - and removes the
 * shared-INVENTORY layer, so seven sages can explore one world while each keeps their own kit.
 *
 * ── Everything here is queried FROM Anchor, not pushed INTO it ──────────────────────────────
 * The Anchor packet files call the predicates below and bail out early. That keeps the touched
 * lines in `soh/Network/Anchor/` down to one include plus one guard per file, which is what makes
 * this module splittable onto its own branch later (same property Enrich World has - see
 * CLAUDE.md). The full list of Anchor-side touch points is in that doc.
 *
 * ── Why CVAR_GENERAL and not CVAR_ENHANCEMENT ───────────────────────────────────────────────
 * Picking the Seven Sages quest at file select applies both Seven Sages presets, and
 * `applyPreset` does a wholesale `Config::SetBlock` on each block a preset declares
 * (Presets.cpp:144) with no merge strategy in either of ours. The "Enhancements - Seven Sages"
 * preset declares the `enhancements` block, which is CVAR_PREFIX_ENHANCEMENT
 * (Presets.cpp:91-93). So a co-op setting stored under CVAR_ENHANCEMENT would be erased at
 * exactly the moment the player sets up a co-op run. CVAR_GENERAL is untouched by both presets,
 * which is the same reason `SevenSages.QuestSelected` lives there (z_file_choose.c:726-732).
 */

#ifdef __cplusplus
extern "C" {
#endif

// Is co-op mode turned on? Reads the CVar only - says nothing about whether a save is loaded or
// Anchor is connected. Use this for menu state, not for behaviour.
bool SevenSagesCoop_IsEnabled(void);

// Should this session behave as a co-op run? Co-op enabled AND on a Seven Sages save.
//
// Deliberately does NOT test Anchor's connection state: every caller is inside an Anchor packet
// handler that already only runs while connected, and adding the check would make this module
// depend on Anchor's internals in the one direction we're trying to avoid.
bool SevenSagesCoop_IsActive(void);

// The four Anchor packets that exist to duplicate items across the team. True means "don't send,
// don't handle". Covers GIVE_ITEM, UPDATE_DUNGEON_ITEMS, UPDATE_BEANS_COUNT, and the inventory
// and capacity-stat halves of UPDATE_TEAM_STATE.
//
// The world-state packets - SET_FLAG, UNSET_FLAG, SET_CHECK_STATUS, ENTRANCE_DISCOVERED, and the
// flag half of UPDATE_TEAM_STATE - are deliberately NOT covered. They are the shared world, which
// is the entire point of playing together.
bool SevenSagesCoop_ShouldSuppressItemSync(void);

// ── Roster ──────────────────────────────────────────────────────────────────────────────────
// Which sages are being played in this run, as a bitmask over RO_SAGE_* (bit N = sage N claimed).
// Set by the host before generating; read by Randomizer_ApplySageGenerationSettings so every
// claimed sage's kit leaves the item pool rather than only the host's.
//
// Only meaningful at generation time on the host. Joiners never generate - they load the host's
// spoiler file - so their roster value is irrelevant to what world they end up in.
uint8_t SevenSagesCoop_GetRoster(void);
void SevenSagesCoop_SetRoster(uint8_t mask);

// Should the roster drive generation? False for a solo run, where only the local sage's kit
// should leave the pool - the vanilla behaviour, and what keeps single-player unaffected.
bool SevenSagesCoop_ShouldUseRosterForGeneration(void);

// ── World fingerprint ───────────────────────────────────────────────────────────────────────
// A hash of the actual item placement, used to prove two clients are in the same world before
// letting their world state sync. 0 means "not comparable" (no randomizer context) and never
// counts as a mismatch.
//
// Computed from the placements themselves rather than from any of SoH's seed bookkeeping, because
// none of that bookkeeping survives the trip:
//
//   - `Context::GetSeed()` is Hash(seedString) only (3drando/menu.cpp:50-51). It is blind to
//     settings, so two players who typed the same seed string but generated with different sages
//     match on it while holding completely different worlds. This is what Anchor compares today,
//     and why its mismatch warning never fires for the case that matters here.
//   - `Context::GetHash()` IS the settings-sensitive finalHash - but it is only ever set by
//     generation (playthrough.cpp:65). ParseSpoiler never calls SetHash, so it is empty on exactly
//     the clients we need to check: the joiners.
//   - `hashIconIndexes` is populated by both paths but with DIFFERENT representations - raw 0-99
//     values on generation (spoiler_log.cpp:49-52) versus texture ids on spoiler load
//     (SeedContext.cpp:421). A host and a joiner in the same world disagree on it.
//
// Hashing the placement sidesteps all three and measures the thing that actually has to match.
uint32_t SevenSagesCoop_GetWorldFingerprint(void);

// Drop the cached fingerprint. Called from this module's own OnLoadGame hook; exposed only so that
// hook can reach it.
void SevenSagesCoop_InvalidateWorldFingerprint(void);

// Should world state coming from this Anchor client be applied?
//
// False when their world fingerprint differs from ours: their flags and check statuses describe a
// different item placement, and applying them corrupts this save with progress that never happened
// here. Upstream only warns about this in the room list; a mismatched world is not a thing to warn
// about and then do anyway.
//
// True whenever either side is not comparable (0 fingerprint), so a vanilla Anchor run and a
// teammate on a build without this module both behave exactly as before.
bool SevenSagesCoop_ShouldAcceptWorldStateFrom(uint32_t remoteWorldFingerprint);

// ── Age dimensions ──────────────────────────────────────────────────────────────────────────
// Child and adult are separate dimensions: you only see players who are currently your own age,
// and time travel is what moves you between them. See docs/multiplayer-anchor.md.
//
// Returns true when a remote player at `remoteLinkAge` should be visible to the local player.
// Always true when co-op is off, so vanilla Anchor behaviour is untouched.
bool SevenSagesCoop_ShouldSeeAge(int32_t remoteLinkAge);

#ifdef __cplusplus
}
#endif

#endif // SEVEN_SAGES_COOP_H
