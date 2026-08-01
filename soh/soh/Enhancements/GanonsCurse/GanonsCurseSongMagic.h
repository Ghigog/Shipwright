#pragma once

#include <functional>

// Shared "songs cost magic" plumbing for Phase 6 (docs/item-ability-overhaul.md). Every ocarina
// song effect that costs magic needs the same deferred-request dance discovered while building
// Song of Time (see GanonsCurseSongOfTime.cpp's header comment for the full diagnosis): calling
// Magic_RequestChange directly from an OnOcarinaSongAction callback can spuriously decline (same
// error sound as insufficient magic) because that callback fires before the game's magicState
// machine has settled back to idle from the reset left by scene/player-init code, and even a
// successful request doesn't apply its own deduction until an animated drain that a subsequent
// scene transition can cut short. This module centralizes both fixes so each song only has to
// provide its own effect, not re-derive the fix.
//
// Call GanonsCurseRequestSongMagic from an OnOcarinaSongAction handler once a song is recognized
// and any song-specific guards (proximity, etc.) have passed. onSuccess runs later, on an
// OnGameFrameUpdate tick once the magic state has settled and the cost has actually been deducted
// - not synchronously. Only one request is tracked at a time, which is fine in practice: songs are
// played one at a time through the same ocarina performance UI.
void GanonsCurseRequestSongMagic(short cost, std::function<void()> onSuccess);
