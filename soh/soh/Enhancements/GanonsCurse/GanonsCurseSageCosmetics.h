#ifndef GANONS_CURSE_SAGE_COSMETICS_H
#define GANONS_CURSE_SAGE_COSMETICS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Ganon's Curse: apply the selected sage's cosmetic identity (tunic colors, and in later slices
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
void GanonsCurse_ApplySageCosmetics(void);

#ifdef __cplusplus
}
#endif

#endif // GANONS_CURSE_SAGE_COSMETICS_H
