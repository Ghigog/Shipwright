#pragma once

#include <cstdint>

struct PlayState;

// Point-source area-of-effect fields (docs/item-ability-overhaul.md, "Foundational: AOE mechanic").
// The companion to SevenSagesRoomAoe.h: that one hits everything loaded in the room with no
// distance test, this one is a lingering volume at a world position.
//
// Modelled on the bomb explosion and Din's Fire rather than on distance maths, because vanilla has
// no distance-based damage anywhere. A field is a real ColliderCylinder submitted through
// CollisionCheck_SetAT every frame it is alive, so it goes through the normal damage pipeline:
// per-enemy DMG_ENTRY tables, immunities, invulnerability windows and hit reactions all apply for
// free. Applying damage directly by distance would bypass every one of those and would look
// correct on whichever enemy you happened to test first.
//
// The field IS the damage-over-time. Vanilla has no ignite status for enemies - an enemy's
// "on fire" is a ~40 frame cosmetic flame (EffectSsEnFire) plus the single hit that caused it, and
// only about eight actors implement even that. A field that lingers re-hits whatever stays inside
// it and stops hurting anything that walks out, which is both closer to vanilla and easier to read
// in play than an invisible per-enemy status would be.
//
// Cadence is deliberately vanilla's: the collider is submitted every frame and each enemy's own
// invulnerability decides how often that turns into damage, exactly as it does for Din's Fire.
// Nothing here rate-limits on its own.

// Damage flags for a field, matching the toucher dmgFlags vanilla actors use.
constexpr uint32_t SEVEN_SAGES_AOE_DMG_FIRE = 0x00020000; // same flag Din's Fire attacks with
// The deku nut's flag. Vanilla pairs it with 0 damage - the effect is the stun, not the hit - so a
// "freeze" field is this flag with damage 0.
constexpr uint32_t SEVEN_SAGES_AOE_DMG_STUN = 0x00000001;

// `height` is the field's TOTAL vertical extent, centred on the point given - the field reaches
// height/2 above and height/2 below. Vanilla's own area effects only extend upward, which suits a
// bomb at your feet but not an arrow that lands above its target.
//
// What the field looks like: a one-shot burst of vanilla's own particles at the impact point, and
// nothing after that. The persistent radius indicator this used to draw - a translucent icosphere
// sized from field.radius every frame, so it tracked the collider exactly - was built, played, and
// **rejected on looks** (2026-08-07). It is gone rather than disabled; a field's visual is now the
// burst alone, and its extent is deliberately not drawn.
//
// Vanilla's spell ACTORS remain unusable as a replacement, which is worth keeping on record for
// whoever tries again: Magic_Fire and friends hard-set their position to the player's every frame
// (z_magic_fire.c:114) and kill themselves on player state, so one spawned at an arrow's landing
// point snaps straight back to Link.
enum SevenSagesAoeVisual {
    SEVEN_SAGES_AOE_VISUAL_NONE, // no burst at all, for fields whose source is already visible
    SEVEN_SAGES_AOE_VISUAL_FIRE,
    SEVEN_SAGES_AOE_VISUAL_ICE,
};

// Spawn a field at pos. It expands from nothing to maxRadius over a few frames like a bomb blast,
// then holds at that size until lifetimeFrames runs out. Fields are pooled; if the pool is full the
// call is a no-op rather than displacing a live field. Passing a lifetime of 1 gives the
// instantaneous case (a hammer swing) through the same mechanism.
void SevenSagesSpawnAoeField(PlayState* play, float x, float y, float z, float maxRadius, float height,
                             int32_t lifetimeFrames, uint32_t damageFlags, uint8_t damage,
                             SevenSagesAoeVisual visual);
