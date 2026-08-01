#pragma once

#include <functional>

struct PlayState;
struct Actor;

// Foundational AOE mechanic (docs/item-ability-overhaul.md's "Foundational: AOE mechanic" section)
// - the first of several songs/items sharing "do something to every enemy in the room" rather than
// bespoke code per effect. OoT loads actors per-room, so the currently-active ACTORCAT_ENEMY list
// already IS "the enemies in this room" - no distance/radius check needed, unlike a point-source
// explosion (Din's Fire, bombs). Call fn once per enemy actor currently loaded.
void GanonsCurseForEachEnemyInRoom(PlayState* play, const std::function<void(Actor*)>& fn);
