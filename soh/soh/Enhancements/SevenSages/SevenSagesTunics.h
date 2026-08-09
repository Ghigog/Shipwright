#ifndef SEVEN_SAGES_TUNICS_H
#define SEVEN_SAGES_TUNICS_H

/*
 * Seven Sages tunic overhaul — shared predicates.
 *
 * Both tunics are specified in docs/item-ability-overhaul.md as "complete"
 * protection rather than the partial protection vanilla gives:
 *
 *   Red Tunic  (Goron) — complete fire resistance: hot environments, fire
 *                        attacks, and fire obstacles, including walking
 *                        *through* walls of flame rather than merely
 *                        surviving them.
 *   Blue Tunic (Zora)  — complete water breathing and frost resistance,
 *                        including immunity to ice traps.
 *
 * Two of those clauses were already vanilla and needed no work: the hot-room
 * life timer already checks for the Goron tunic, and the drowning timer
 * already checks for the Zora tunic (both in z_parameter.c). What is new is
 * the *obstacle* half — flame walls, lava, and the freeze/burn reactions.
 *
 * Nayru's Love already implemented most of the fire half for itself, so
 * these predicates fold both sources into one condition rather than
 * duplicating the logic. Every site that previously read
 * `IS_SEVENSAGES && gSaveContext.nayrusLoveTimer != 0` now calls through here,
 * which is why widening the tunics could not change Nayru's Love behaviour
 * by accident — the two share one definition.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

// True when Link should be immune to fire hazards: Nayru's Love is up, or the
// Red (Goron) tunic is worn. Gates flame walls, lava walking and fire damage.
bool SevenSagesFireProtectionActive(void);

// True when Link should be immune to freezing: Nayru's Love is up, or the
// Blue (Zora) tunic is worn. Gates every freeze source, ice traps included.
bool SevenSagesFrostProtectionActive(void);

#ifdef __cplusplus
}
#endif

#endif // SEVEN_SAGES_TUNICS_H
