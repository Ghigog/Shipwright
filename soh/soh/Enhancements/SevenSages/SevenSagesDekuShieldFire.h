#ifndef SEVEN_SAGES_DEKU_SHIELD_FIRE_H
#define SEVEN_SAGES_DEKU_SHIELD_FIRE_H

/*
 * Seven Sages Deku Shield overhaul — shared state and predicates.
 *
 * Spec (docs/item-ability-overhaul.md, "Utility items"): instead of burning up in fire, the Deku
 * Shield catches flame and can carry fire to other places, like a lit stick — fireproof, and not
 * consumed at the end.
 *
 * The "don't destroy it" third of that already existed as a cheat toggle
 * (Enhancements/Cheats/FireproofDekuShield.cpp, hooking VB_BURN_SHIELD). This adds the other two:
 * the shield holds a flame afterwards, and that flame lights what a lit Deku Stick lights.
 *
 * WHAT LIGHTS THE SHIELD, three ways, all of them things that already happen in vanilla:
 *   - The moment vanilla would have destroyed it. VB_BURN_SHIELD fires from func_8083819C
 *     (z_player.c), which is reached both when the shield blocks a fire attack and when Link
 *     himself is burning. Vanilla's own answer to "the shield met fire" becomes the ignition point,
 *     so nothing new has to detect fire.
 *   - Holding it to a lit torch, the same proximity test a Deku Stick uses (Obj_Syokudai).
 *
 * WHAT PUTS IT OUT: swimming, swapping to another shield, or losing it. Notably NOT a timer. A lit
 * Deku Stick burns out after 210 frames, and inheriting that here would defeat the specified point
 * of the item — 210 frames is about seven seconds, which is not "carry fire to other places", it is
 * barely across one room. There is also nothing for a timer to represent: the stick's timer is the
 * stick being consumed, and this shield explicitly is not. So the flame lasts until it is doused.
 *
 * The lit state is process-local and not written to the save file. Quitting with a burning shield
 * and reloading gets an unlit one. That is the same class of transient state as Nayru's Love's
 * timer, and re-lighting costs one torch.
 *
 * WHAT THE FLAME LIGHTS. Vanilla has no shared "the player is carrying fire" concept, so the sites
 * were enumerated by finding every actor that reads the lit-stick timer (`player->unk_860`). There
 * are four outside the player itself, and only two of them are ignition:
 *   - Obj_Syokudai (torches) — both directions: a lit torch lights the shield, a burning shield
 *     lights an unlit torch.
 *   - Bg_Ydan_Sp (the Deku Tree's wall web) — burns.
 *   - En_Ssh (Skullwalltula) and En_St (Skulltula) are deliberately NOT touched. Neither is an
 *     ignition check: both only re-route which of their three colliders accepts damage flag 0x2,
 *     DMG_DEKU_STICK, while a lit stick is held. A burning shield never produces that flag — it is
 *     not a weapon — so extending those two sites would change nothing at all. Left alone rather
 *     than given a shield-shaped workaround.
 * (Fishing also writes `unk_860`, reusing the field for fishing-pole state on a different item. Not
 * related, not touched.)
 *
 * The flame is deliberately NOT an AT collider. Attaching a fire-flagged collider to Link would
 * have lit everything above through the normal damage pipeline for free, and it was the first
 * design considered — but it also turns a permanent, uncosted shield buff into a permanent fire
 * damage aura that burns any enemy Link walks into, which is a balance change several sizes larger
 * than the spec asks for. Ignition is per-site instead, which is more code and exactly the
 * specified amount of effect.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

// True when the equipped Deku Shield is currently alight.
bool SevenSagesDekuShieldIsAflame(void);

// True when Link has a Deku Shield equipped at all, lit or not, and fills in where its flame sits
// (or would sit) in world space. That is the shield's own position, which moves: the shield rides
// the sheath limb on Link's back while stowed and his left hand while he is actually blocking, so
// this follows whichever one is currently holding it. Callers use it for the same kind of proximity
// test vanilla runs against `player->meleeWeaponInfo[0].tip` for a lit stick.
bool SevenSagesDekuShieldFlamePos(float* x, float* y, float* z);

// Light the shield. Safe to call when it is already alight (does nothing) or when no Deku Shield is
// equipped (does nothing). Returns true only on the transition from unlit to lit, so a caller can
// play an ignition sound exactly once.
bool SevenSagesDekuShieldIgnite(void);

#ifdef __cplusplus
}
#endif

#endif // SEVEN_SAGES_DEKU_SHIELD_FIRE_H
