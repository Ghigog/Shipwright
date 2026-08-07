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
 * A prop the placer offers. `objectId` is what the room must have loaded for this prop to
 * work; it is checked against the live object context rather than a baked table, so the list
 * automatically reflects the room's actual header (day/night, child/adult).
 */
struct PropDef {
    const char* label;
    int16_t actorId;
    int16_t objectId;
    int16_t params;
    const char* note;
};

// ---- Palette (EnrichWorldPalette.cpp) ----

/** The full curated list, regardless of what's currently loadable. */
const std::vector<PropDef>& AllProps();

/** True if `def`'s object is loaded in the current room right now. */
bool IsPropAvailable(const PropDef& def);

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
