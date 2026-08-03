#pragma once

#include <functional>

struct PlayState;
struct Actor;

// Foundational AOE mechanic (docs/item-ability-overhaul.md's "Foundational: AOE mechanic" section)
// - the first of several songs/items sharing "do something to every actor of some category in the
// room" rather than bespoke code per effect. OoT loads actors per-room, so the currently-active
// list for a given ActorCategory (ACTORCAT_ENEMY, ACTORCAT_PROP, ...) already IS "the actors of
// that kind in this room" - no distance/radius check needed, unlike a point-source explosion
// (Din's Fire, bombs). Call fn once per matching actor currently loaded.
//
// category is a plain int rather than the ActorCategory enum type, so callers don't need to pull
// in z64actor.h just to call this - pass the ACTORCAT_* constant directly, e.g.
// SevenSagesForEachActorInRoom(play, ACTORCAT_ENEMY, ...).
void SevenSagesForEachActorInRoom(PlayState* play, int category, const std::function<void(Actor*)>& fn);
