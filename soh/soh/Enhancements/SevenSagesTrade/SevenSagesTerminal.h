#ifndef SEVEN_SAGES_TERMINAL_H
#define SEVEN_SAGES_TERMINAL_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Seven Sages trade box - the physical terminals.
 *
 * A terminal is an access point into the ONE shared stash, not a box of its own
 * (multiplayer-anchor.md decision 5). Adding or moving one is a table edit here and nothing more,
 * which is the property that makes "start with four and expand after play" cheap.
 *
 * ── Why a custom actor drawn from gameplay_keep ─────────────────────────────────────────────
 *
 * CLAUDE.md's scenery rule is that a prop may only be placed where its object is already loaded,
 * and that a violation does NOT error - Actor_Spawn's missing-object fallback lets the spawn
 * through and the actor silently kills itself later. OBJECT_GAMEPLAY_KEEP is loaded in every
 * scene, so sourcing the model from there makes that whole class of failure impossible rather
 * than merely avoided, and means a terminal can be dropped anywhere without a per-scene audit.
 *
 * The model is gLiftableRockDL - a stone, scaled up. It is a placeholder in the sense that it was
 * chosen for availability rather than beauty, but it reads correctly for what decision 5 asked
 * for: a plain object with no personality. The Beggar (ACTOR_EN_HY) remains the possible v2.
 */

#ifdef __cplusplus
extern "C" {
#endif

// Is the local player close enough to a terminal to use it? Drives the trade window's open state.
bool SevenSagesTerminal_PlayerIsAtTerminal(void);

// Called by the terminal actor when the player interacts with it.
void SevenSagesTerminal_Open(void);

// Is the trade window currently open?
bool SevenSagesTerminal_IsOpen(void);
void SevenSagesTerminal_Close(void);

#ifdef __cplusplus
}
#endif

#endif // SEVEN_SAGES_TERMINAL_H
