#pragma once

struct PlayState;
struct Actor;

/**
 * Seven Sages - Phase 6: thrown items hit harder the bigger they are.
 *
 * Spec: docs/item-ability-overhaul.md, "Utility items" - "Thrown items: damage scales according to
 * the size of the thrown object (rocks, crates, enemies)."
 *
 * Vanilla's baseline is thinner than the spec assumes. A thrown pot has an AT collider worth exactly
 * 1 damage on the deku-stick row (z_obj_tsubo.c:70); a thrown ROCK has **no AT collider at all**
 * (z_en_ishi.c, AT_NONE), so in vanilla a boulder to the face is worth nothing. Everything below is
 * therefore additive on top of whatever the object already did, which keeps the pot's own hit
 * working and gives the rest of the throwables a hit they never had.
 *
 * **Size cannot come from mass, and that is worth stating because it is the obvious first guess.**
 * SevenSagesGauntletLift.cpp uses colChkInfo.mass to tell a light enemy from a heavy one, and that
 * works there. It does not work here: every throwable PROP sets `colChkInfo.mass = 240` at the
 * moment it is thrown - pot (z_obj_tsubo.c:301), bush (z_en_kusa.c:364), rock (z_en_ishi.c:430),
 * crate (z_obj_kibako.c:246) - a uniform "in flight" value, not a weight. Their resting masses are
 * MASS_IMMOVABLE/MASS_HEAVY sentinels, which mean "Link cannot push this" rather than any size. So
 * props are classed by actor id, from a short list, and only enemies fall back to mass.
 *
 * **The scale is expressed in vanilla damage rows, not in a damage number.** CollisionCheck_ApplyDamage
 * indexes the target's own DMG_ENTRY table by the highest set bit of the attacker's dmgFlags, so an
 * attacker cannot state a damage number at all for anything that has a table - which is every enemy.
 * Three sizes therefore mean three rows:
 *
 *   - small  (bush, pot, small crate, small rock, cucco, light enemy)  -> the deku STICK row, which
 *            is the row vanilla's own thrown pot already uses. Typically 1-2.
 *   - medium (large silver rock, heavy enemy)                          -> the HAMMER SWING row.
 *   - large  (brown/bronze boulders, the heavy stone pillar)           -> the EXPLOSIVE row, i.e. a
 *            thrown boulder lands like a bomb. This is the same reading the Megaton Hammer's
 *            "melee bomb" already took, and it means a heaved boulder breaks what a bomb breaks.
 *
 * Damage therefore rises with size *and* keeps every enemy's authored resistance and immunity,
 * which a flat multiplier on a single row would have flattened.
 *
 * The impact is a one-frame SevenSagesAoeField, the same degenerate case the hammer's shockwave uses
 * - so it goes through the ordinary collision pipeline rather than applying anything by hand.
 */

// Spawn the size-scaled impact for an object that has just landed. Silently does nothing if the
// object was dropped rather than thrown (speedXZ below the throw threshold), so an involuntary
// release - Link takes a hit, walks into water - drops it at his feet without a shockwave.
//
// Called from this module's own tracker for everything vanilla lets Link carry, and directly by
// SevenSagesGauntletLift.cpp for the boulders and stunned enemies it flies itself.
void SevenSagesThrownImpact(PlayState* play, Actor* actor);
