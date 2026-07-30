#ifndef RANDOSAVEFILE_H
#define RANDOSAVEFILE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void Randomizer_InitSaveFile();

// Ganon's Curse: accessors over the sage definition table in savefile.cpp - the single source of
// truth for each sage's fixed starting state. See the big comment above that table for why the kit
// is modelled as real RSK_STARTING_* options instead of direct item grants.

// The selected sage's fixed home-base entrance, or -1 when no override applies (randomizer off).
// Shared between every place that independently recomputes the "where does this file spawn"
// fallback entrance (z_sram.c's Sram_OpenSave, randomizer_entrance.c's Entrance_SetSavewarpEntrance)
// so a new one doesn't silently miss the sage override again.
int32_t Randomizer_GetSageHomeEntrance(void);

// Generation-time: folds the selected sage's age and kit into the real settings the generator
// reads, so the logic solver, the item pool, and the spoiler log all agree with what the player
// will actually start with. Must run before Fill(); called from Context::FinalizeSettings.
void Randomizer_ApplySageGenerationSettings(void);

// The selected sage's forced starting age (RO_AGE_*), defaulting to child when unavailable.
uint8_t Randomizer_GetSageStartingAge(void);

// The selected sage's home RandomizerRegion, for the logic solver's starting position. Returns 0
// when no sage override applies - callers should gate on IS_RANDO.
uint16_t Randomizer_GetSageHomeRegion(void);

// Writes the selected sage's Kokiri Tunic recolor into r/g/b. Leaves them untouched when no sage
// override applies.
void Randomizer_GetSageTunicColor(uint8_t* r, uint8_t* g, uint8_t* b);

#ifdef __cplusplus
}
#endif

#endif
