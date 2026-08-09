#ifndef RANDOSAVEFILE_H
#define RANDOSAVEFILE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Randomizer_InitSaveFile();

// Seven Sages: accessors over the sage definition table in savefile.cpp - the single source of
// truth for each sage's fixed starting state. See the big comment above that table for why the kit
// is modelled as real RSK_STARTING_* options instead of direct item grants.
//
// ── IS_RANDO is NOT the gate for Seven Sages content. IS_SEVENSAGES is. ─────────────────────────
//
// (Documented here rather than beside the macros in z64save.h: that header is included by most of
// the tree and there is no ccache, so editing it costs an ~85 minute rebuild. See CLAUDE.md.)
//
// IS_RANDO answers yes for BOTH quest ids, deliberately - a Seven Sages save is a randomized save
// and must answer yes to everything one does. The trap is that gating a Seven Sages *feature* on
// it therefore turns that feature on for the plain Randomizer quest too. That was the state until
// 2026-08-09, and picking "Randomizer" on the file-select screen produced a silent hybrid: Rauru's
// kit and starting age forced onto the file, respawn at the Lon Lon tower on every overworld load,
// the sage NPCs missing from the world, eight checks excluded, song shuffle coerced - and none of
// the Seven Sages presets applied to make any of it coherent. Nothing errored.
//
// So the two questions are kept apart:
//
//   IS_RANDO        "is this save randomized?"     - randomizer machinery: item locations, hints,
//                                                    the save/load path, seed data
//   IS_SEVENSAGES   "is this the Seven Sages mod?" - every gameplay change the mod adds
//
// One wrinkle, because it looks like a mistake in ~45 files: the modules keep
// `RegisterShipInitFunc(..., { "IS_RANDO" })` while their COND_HOOK conditions test IS_SEVENSAGES.
// That string is only a bucket key naming *when* to re-run registration, and
// ShipInit::Init("IS_RANDO") is the call that fires on OnLoadGame (randomizer/hook_handlers.cpp).
// The condition inside then decides whether the hook is registered, and COND_HOOK unregisters
// first either way - so loading a plain Randomizer file after a Seven Sages one tears the mod's
// hooks back down. There is no "IS_SEVENSAGES" bucket, and adding one would need a second Init
// call at the same point for no gain.
//
// Every accessor below answers only for a Seven Sages save (IS_SEVENSAGES) - a plain Randomizer
// save gets the "no sage" answer, which is what keeps the mod's starting state out of it.

// Is the seed currently being generated a Seven Sages seed?
//
// The generation-time counterpart of IS_SEVENSAGES, and it exists because that macro cannot be
// used here: generation runs from the file-select screen before any save exists, so quest.id still
// describes the previous session. Backed by a CVar the file-select writes when a quest is picked.
//
// Gate anything that shapes the SEED for Seven Sages on this - sage settings, location exclusions,
// forced option values. Anything that shapes GAMEPLAY belongs on IS_SEVENSAGES instead.
bool Randomizer_IsSevenSagesGeneration(void);

// The selected sage's fixed home-base entrance, or -1 when this isn't a Seven Sages save.
// Shared between every place that independently recomputes the "where does this file spawn"
// fallback entrance (z_sram.c's Sram_OpenSave, randomizer_entrance.c's Entrance_SetSavewarpEntrance)
// so a new one doesn't silently miss the sage override again.
int32_t Randomizer_GetSageHomeEntrance(void);

// Where the file should actually SPAWN right now: the sage's home entrance while they are in
// their own age, or their off-age substitute once they have switched at the pedestal. Identical
// to Randomizer_GetSageHomeEntrance() for any sage with no off-age entrance defined, and -1 when
// this isn't a Seven Sages save.
//
// This, not the home accessor, is what the two savewarp recomputes must call. The home base is
// also the reload point, so a sage who changed age and quit was coming back to a spawn chosen for
// the age they are no longer in - stranded above the meadow's adult Moblin maze in Saria's case.
// Use the home accessor only when you mean the sage's identity (openings, "is a sage selected").
int32_t Randomizer_GetSageSpawnEntrance(void);

// Generation-time: folds the selected sage's age and kit into the real settings the generator
// reads, so the logic solver, the item pool, and the spoiler log all agree with what the player
// will actually start with. Must run before Fill(); called from Context::FinalizeSettings.
void Randomizer_ApplySageGenerationSettings(void);

// The selected sage's forced starting age (RO_AGE_*), defaulting to child when unavailable.
uint8_t Randomizer_GetSageStartingAge(void);

// The selected sage's home RandomizerRegion, for the logic solver's starting position. Returns 0
// when this isn't a Seven Sages save.
//
// Currently unused: the solver still starts from the vanilla Child/Adult Spawn regions. Wiring
// this in crashed generation (the spawn regions double as registered entrances) - see the note in
// location_access/root.cpp for what the right approach would be.
uint16_t Randomizer_GetSageHomeRegion(void);

#ifdef __cplusplus
}
#endif

#endif
