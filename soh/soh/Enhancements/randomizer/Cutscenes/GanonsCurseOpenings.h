#ifndef GANONS_CURSE_OPENINGS_H
#define GANONS_CURSE_OPENINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Ganon's Curse - Phase 4c: arm the universal opening.
 *
 * Called from Sram_InitSave (z_sram.c) on the sage file-creation path only, so
 * it fires exactly once per new file and never on a normal load. Everything
 * after that - waiting out the home region's vanilla entrance cutscene, then
 * playing the premise beat - is driven from GanonsCurseOpenings.cpp.
 *
 * Safe to call before gPlayState exists; it only sets a flag.
 */
void GanonsCurse_ArmUniversalOpening(void);

#ifdef __cplusplus
}
#endif

#endif // GANONS_CURSE_OPENINGS_H
