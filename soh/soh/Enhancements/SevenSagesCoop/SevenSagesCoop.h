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
// Identifies which world this client is playing, so world state is only shared between clients
// actually in it. 0 means "not comparable" (no randomizer context) and never counts as a mismatch.
//
// This is `Context::GetSeed()`, and the reason is symmetry: it is the one value that provably
// survives the host -> spoiler -> joiner trip. The host writes it out as `finalSeed`
// (spoiler_log.cpp:347) and Settings::ParseJson restores it verbatim (settings.cpp:3116), so a
// generating host and a spoiler-loading joiner in the same world always agree on it. It is also
// sage-independent, which is required here: co-op players deliberately have DIFFERENT sages in the
// SAME world, so anything sage-derived would read as a mismatch by design.
//
// The three alternatives were each tried and all fail that symmetry test:
//
//   - Hashing the item placements. This was the original implementation, and it produced FALSE
//     mismatches that silently blocked ALL world sync - found in playtest 2026-08-11, where both
//     clients pulled identical items from identical chests while being flagged as different
//     worlds. The spoiler only carries `ctx->allLocations`, this seed's pool: 1167 checks out of
//     RC_MAX's 3321 (spoiler_log.cpp's WriteAllLocations). The host's placements outside the pool
//     never reach the joiner, so the two hashes diverge for the very same world.
//   - `Context::GetHash()`, the settings-sensitive finalHash, is only ever set by generation
//     (playthrough.cpp:65). ParseSpoiler never calls SetHash, so it is empty on exactly the
//     clients that need checking: the joiners.
//   - `hashIconIndexes` is populated by both paths but with DIFFERENT representations - raw 0-99
//     values on generation (spoiler_log.cpp:49-52) versus texture ids on spoiler load
//     (SeedContext.cpp:421) - so host and joiner disagree on it in the same world.
//
// Known limitation, accepted: two clients that each GENERATE from the same seed string with
// different settings land on the same value while holding different worlds, since GetSeed() is
// Hash(seedString) and ignores settings (3drando/menu.cpp:50-51). The co-op flow no longer reaches
// that state - joiners load the host's spoiler, and the sage screen no longer regenerates over a
// loaded spoiler - and the file-select hash icons remain the manual backstop.
uint32_t SevenSagesCoop_GetWorldFingerprint(void);

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

// ── Knowledge: the one category of "item" that IS shared ────────────────────────────────────
// Medallions, spiritual stones and songs. docs/multiplayer-anchor.md decision 4 calls these
// Knowledge and marks them always-shared, for a reason that survives contact with play: they are
// non-rivalrous progress markers, not objects. Learning a song does not consume anyone else's copy
// of it, and a medallion is a record that a dungeon was cleared - by the team.
//
// Everything else stays personal. The distinction is the questItems bitfield's low bits:
//   0x00-0x05 medallions | 0x06-0x11 songs | 0x12-0x14 spiritual stones
// and NOT the two that follow - Stone of Agony (0x15) is a real held item and a sage kit item
// (Rauru's), and the Gerudo Card (0x16) is an object and Nabooru's.
//
// Gold skulltula tokens (0x17) ARE included, and that is a consequence rather than a preference.
// The kill flags are already shared world state - UpdateTeamState OR-merges gsFlags - so each
// skulltula can only be collected once by the team. Leaving the COUNT personal means two players
// split 100 tokens between them and neither ever reaches the 50 the Skulltula House wants, making
// those checks unreachable by construction. Sharing the kills obliges sharing the tally.
#define SEVEN_SAGES_COOP_KNOWLEDGE_QUEST_MASK 0x009FFFFFu

// Is this a knowledge item, by ITEM_* id? The three groups are contiguous in z64item.h -
// ITEM_SONG_MINUET (0x5A) through ITEM_ZORA_SAPPHIRE (0x6E) - and ITEM_STONE_OF_AGONY sits at 0x6F,
// one past the end, so the range excludes it without a special case.
//
// ITEM_SKULL_TOKEN (0x71) is named separately because it is not adjacent, and because it is here
// for a different reason than the rest - see the mask comment above.
bool SevenSagesCoop_IsKnowledgeItem(uint16_t itemId);

// ── "The seed I am holding came from a teammate" ────────────────────────────────────────────
// Set when SEVEN_SAGES_SEED loads successfully; cleared whenever this client generates its own.
// So it means precisely "the newest seed here is somebody else's", and nothing more.
//
// The sage select screen uses it to decide whether confirming a sage should generate. That has to
// be an answer about intent, and this is the only signal that carries intent: a joiner who just
// received the host's world must not have it thrown away, while everyone else - including a host
// who happens to have loaded a spoiler at some point - still gets the ordinary generate-on-confirm
// behaviour.
//
// The first attempt used Context::IsSpoilerLoaded() instead, and that was wrong in a way worth
// recording: it is sticky for the life of the process, so a window that had EVER loaded a spoiler
// stopped generating forever while a fresh window always generated. Two clients then behaved
// differently and permanently for reasons invisible to the player.
bool SevenSagesCoop_HasReceivedSeed(void);
void SevenSagesCoop_SetHasReceivedSeed(bool received);

// Re-apply the received seed file to the randomizer context, and report whether it took.
//
// Belt and braces for a real observed failure: on 2026-08-11 a seed transferred and loaded cleanly
// (byte-exact, success notification shown), and by the time the player reached the file-select
// settings screen the game reported "No randomizer seed loaded" with Start Randomizer greyed out -
// i.e. Context::IsSpoilerLoaded() had gone back to false somewhere between picking the Seven Sages
// quest and arriving there. Nothing on that path calls SetSpoilerLoaded(false), so the exact
// culprit is unidentified; applyPreset re-running ShipInit::InitAll() at quest selection is the
// suspect.
//
// Rather than keep hunting, the sage select screen re-applies the file immediately before handing
// off to that screen. Re-parsing rather than just re-setting the flag is deliberate: if whatever
// cleared the flag also cleared the placements, setting the flag alone would let the player start
// on an empty world, which is a far worse failure than the one being fixed.
bool SevenSagesCoop_ReapplyReceivedSeed(void);

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
