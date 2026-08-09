#pragma once

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "z64.h"
#include "z64math.h"
}

/**
 * Enrich World - shared types for the placement store and the in-game placer.
 *
 * Placements live in a JSON file, not in compiled code, so the placer can write them and the
 * game can pick them up on the next scene load without a rebuild. See EnrichWorldStore.cpp for
 * the file format and EnrichWorldPalette.cpp for the curated prop list.
 */

namespace EnrichWorld {

/** One placed prop. `live` is session-only and never serialised. */
struct Placement {
    int16_t sceneId = 0;
    int8_t room = 0;
    int16_t actorId = 0;
    int16_t params = 0;
    Vec3f pos = { 0.0f, 0.0f, 0.0f };
    Vec3s rot = { 0, 0, 0 };
    std::string label; // regenerated on save, purely for humans reading the file
    std::string note;  // free text, preserved across saves

    // The spawned instance, when this placement's room is loaded. Cleared on every scene load -
    // the pointer is only valid for the room it was spawned into.
    Actor* live = nullptr;
};

/**
 * A prop the placer offers.
 *
 * Two different object ids, because "which room is this prop from" and "will this prop work
 * here" turn out to be different questions:
 *
 * - `nativeObjectId` is the object the prop's model lives in vanilla. It decides whether the
 *   placer lists the prop as native to the current room, which is a *recommendation*.
 * - `requiredObjectId` is an object without which the prop provably breaks, because the actor
 *   itself calls Object_GetIndex and Actor_Kills on a miss. OBJECT_ID_MAX means nothing is
 *   required. This is the only hard gate.
 *
 * They differ for almost every entry, because SoH resolves display lists by OTR resource name
 * rather than through segment 6, so an actor renders correctly whether or not its object is
 * resident. Only actors that explicitly check for their object care - En_Kusa (z_en_kusa.c:263)
 * and Obj_Tsubo (z_obj_tsubo.c:141) are the two in this palette.
 *
 * For a spawner, `requiredObjectId` is what its *children* need, not what the spawner needs.
 * Obj_Mure2 is declared in gameplay_keep - resident everywhere - but its grass children are
 * En_Kusa type 0 and die without the field keep.
 *
 * Both are checked against the live object context rather than a baked table, so the list
 * reflects the room's actual header (day/night, child/adult) and self-corrects if upstream
 * changes a scene.
 */
struct PropDef {
    const char* label;
    int16_t actorId;
    int16_t nativeObjectId;
    int16_t requiredObjectId; // OBJECT_ID_MAX when the prop works without any object loaded
    int16_t params;
    const char* note;
};

// ---- Palette (EnrichWorldPalette.cpp) ----

/** The full curated list, regardless of what's currently loadable. */
const std::vector<PropDef>& AllProps();

/** True if `def` is vanilla-native to the current room, i.e. its object is already loaded. */
bool IsPropNative(const PropDef& def);

/** True unless `def` needs an object the current room hasn't loaded. False means it would die. */
bool IsPropUsable(const PropDef& def);

/**
 * True unless `params` would index one of `actorId`'s variant tables out of bounds.
 *
 * Several vanilla actors mask the variant out of params with `& 3` but only define three
 * variants, so a params of 3 reads one entry past the end of the table. What's there decides how
 * it fails: Obj_Hana and En_Kusa index arrays of Gfx*, so the garbage pointer reaches the display
 * list interpreter and segfaults; Obj_Mure2 indexes child-spawn counts and spawns a garbage
 * number of actors. None of this is reachable in vanilla, where params come from the archive.
 *
 * Keyed on actor id rather than PropDef because saved placements carry only an id and params -
 * the store has to be able to check an entry it's about to respawn.
 */
bool AreParamsSafe(int16_t actorId, int16_t params);

/** Human-readable name for an actor id, falling back to the raw number. */
std::string ActorLabel(int16_t actorId, int16_t params);

// ---- Store (EnrichWorldStore.cpp) ----

std::vector<Placement>& Placements();

/** Absolute path of the JSON file placements are read from and written to. */
std::string StorePath();

/** Replaces the in-memory list from disk. Missing file is not an error. */
void LoadStore();

/** Writes the in-memory list to disk. Returns false on IO failure. */
bool SaveStore();

/** True when the in-memory list differs from what was last loaded or saved. */
bool StoreIsDirty();
void MarkStoreDirty();

} // namespace EnrichWorld
