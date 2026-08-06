#ifndef SEVEN_SAGES_MEGATON_HAMMER_H
#define SEVEN_SAGES_MEGATON_HAMMER_H

/*
 * Seven Sages Megaton Hammer overhaul — shared predicates.
 *
 * Spec (docs/item-ability-overhaul.md, "Melee"): no damage change (verified 2026-07-28 — the
 * hammer already matches the Master Sword in vanilla's damage tables). Two additions:
 *
 *   - AOE stun on swing, Deku Nut style.
 *   - Breaks breakable walls, "basically like a melee bomb".
 *
 * Both turned out to be small, because the infrastructure each one needs already exists.
 *
 * THE STUN rides on the hammer's own ground-strike shockwave (z_player.c,
 * Player_Action_808502D0) rather than on every swing. That moment already computes a ground
 * position, already plays EffectSsBlast_SpawnWhiteShockwave, and is already gated on the strike
 * actually landing near the floor — so the stun lands exactly when the player sees the shockwave
 * that explains it. The side swing (PLAYER_MWA_HAMMER_SIDE) has no shockwave in vanilla and
 * deliberately gets no stun: a stun with no visual would read as enemies freezing at random.
 *
 * It is spawned as a SevenSagesAoeField with a one-frame lifetime — the degenerate case the field
 * was built to cover (SevenSagesAoeField.h). Deku Nut damage flag with 0 damage, which is vanilla's
 * own way of saying "the effect is the stun, not the hit", and the same pairing the Ice Arrow's
 * field was playtested on. Visual is NONE because the white shockwave IS the visual.
 *
 * THE WALL BREAKING is two pieces, because vanilla gates "was I hit by a bomb" in two unrelated
 * ways and the hammer already satisfied neither:
 *
 *   1. Actors that gate on the attacker's dmgFlags. Most bombable geometry is here, and most of it
 *      the hammer *already* broke in vanilla: Bg_Breakwall's bumper mask is 0x48, which is
 *      DMG_EXPLOSIVE | DMG_HAMMER_SWING, so cracked walls have always yielded to a hammer swing.
 *      The gap is the explosive-only ones — Bg_Hidan_Kowarerukabe (the Fire Temple's cracked floor
 *      and its two bombable walls) tests 0x8 alone. Adding the explosive bit to the hammer's melee
 *      dmgFlags clears those.
 *
 *   2. Actors that call Actor_GetCollidedExplosive (z_actor.c), a hard
 *      `category == ACTORCAT_EXPLOSIVE` check with no dmgFlags involved. Dodongo's Cavern's falling
 *      stairs (Bg_Ddan_Kd) and mouth door (Bg_Dodoago) are here, and their bumper masks are
 *      0xFFCFFFFF — they already register the hammer's hit and then throw it away at the category
 *      test. This is the identical gap Din's Fire hit (SevenSagesDinsFireBomb.cpp), and it is
 *      closed the identical way: the shared function accepts the hammer rather than each of its
 *      eight call sites being patched.
 *
 * Consequence of (2), accepted deliberately and worth knowing before playtesting: five actors that
 * are not walls also ask that function whether an explosive touched them — Beamos (En_Vm), Bubble
 * (En_Bb), Dead Hand (En_Dh), the fake attacking doors (Door_Killer) and Ganon's collapse rubble
 * (Demo_Gj). A hammer strike now reads as a bomb to all five. That is the same trade the Din's
 * Fire decision took on purpose ("think of it as a permanent bomb the player carries"), and for a
 * weapon specified as a melee bomb it is the consistent answer rather than a side effect.
 *
 * Adding the explosive bit cannot change how much damage the hammer does. CollisionCheck_ApplyDamage
 * indexes an actor's DMG_ENTRY table by the position of the HIGHEST set bit in the attacker's
 * dmgFlags; the hammer's own bits are 0x40 (swing, bit 6) and 0x40000000 (jump, bit 30), both above
 * the explosive bit 3, so every enemy still resolves through exactly the table entry it did before.
 * The extra bit only widens which AC bumpers the swing is eligible to touch at all.
 */

// Outside the extern "C" block below on purpose: on macOS <stdint.h> pulls in libc++ machinery, and
// templates inside extern "C" are a hard error ("templates must have C++ linkage").
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Actor;
struct Player;
struct PlayState;

// True when this actor's collision should count as an explosive impact. Answers yes for the player
// while the Megaton Hammer is the held item, which is sufficient to mean "a hammer strike": the
// melee weapon quads are the only AT colliders the player ever submits, so an AC hit whose attacker
// is the player is always a weapon hit. Deliberately does NOT test the quads' AT_HIT flag - that
// would make the answer depend on whether the player actor updates before or after the actor
// asking, which is a category-ordering detail inside Actor_UpdateAll and not something this should
// be coupled to.
bool SevenSagesHammerCountsAsExplosive(const struct Actor* attacker);

// Returns the melee damage flags a swing should be submitted with: vanilla's value, plus the
// explosive bit when the swing is a Megaton Hammer swing in a randomizer save.
uint32_t SevenSagesHammerMeleeDmgFlags(const struct Player* player, uint32_t dmgFlags);

// Fires the stun field for a hammer ground strike. Call at the moment vanilla spawns the white
// shockwave, with the same ground position it uses.
void SevenSagesHammerShockwave(struct PlayState* play, float x, float y, float z);

#ifdef __cplusplus
}
#endif

#endif // SEVEN_SAGES_MEGATON_HAMMER_H
