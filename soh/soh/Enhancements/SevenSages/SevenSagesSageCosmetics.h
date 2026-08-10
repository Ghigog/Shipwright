#ifndef SEVEN_SAGES_SAGE_COSMETICS_H
#define SEVEN_SAGES_SAGE_COSMETICS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Seven Sages: apply the selected sage's cosmetic identity (tunic colors, and in later slices
 * HUD layout, proportions, voice and instrument) by writing the CVars that SoH's own cosmetics
 * and audio systems already read.
 *
 * Safe to call at any point where the randomizer context is populated. No-ops when the randomizer
 * is off, when no sage override applies, or when the player has turned the feature off.
 *
 * Called from two places on purpose - see docs/sage-cosmetics.md:
 *   - Sram_InitSave (file creation), so a brand new file is styled immediately
 *   - the OnLoadGame hook in this file, which is the one that makes it *correct* across files
 */
void SevenSages_ApplySageCosmetics(void);

/**
 * The tunic colour belonging to an arbitrary sage, as a value rather than a CVar write.
 *
 * ApplySageCosmetics styles the LOCAL player by writing the cosmetic CVars, which is a per-process
 * global - so it cannot answer "what colour is my teammate". Anchor's remote-player renderer needs
 * exactly that, per remote client, to draw each teammate as their own sage.
 *
 * `tunicIndex` is PLAYER_TUNIC_KOKIRI/GORON/ZORA (0/1/2). Returns 1 and writes r/g/b on success,
 * 0 when `sage` has no palette (and then leaves the outputs alone).
 */
int SevenSages_GetSageTunicColor(uint8_t sage, uint8_t tunicIndex, uint8_t* r, uint8_t* g, uint8_t* b);

#ifdef __cplusplus
}
#endif

#endif // SEVEN_SAGES_SAGE_COSMETICS_H
