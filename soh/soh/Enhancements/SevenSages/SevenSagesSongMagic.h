#pragma once

#include <functional>

// Shared "songs cost magic" plumbing for Phase 6 (docs/item-ability-overhaul.md). Every ocarina
// song effect that costs magic needs the same deferred-request dance discovered while building
// Song of Time (see SevenSagesSongOfTime.cpp's header comment for the full diagnosis): calling
// Magic_RequestChange directly from an OnOcarinaSongAction callback can spuriously decline (same
// error sound as insufficient magic) because that callback fires before the game's magicState
// machine has settled back to idle from the reset left by scene/player-init code. This module
// centralizes that fix so each song only has to provide its own effect, not re-derive it.
//
// `fromOcarinaPerformance`, the default, additionally drops the whole request - cost and effect
// both - when the performance that just finished was played *for* something in the world rather
// than by the player on their own: Mido barring the Lost Woods, Darunia's dance, the Skull Kid,
// the frogs, a scarecrow spot. Those all have their own vanilla payoff, and a sage aiming a song
// at an NPC isn't casting it, so charging magic and firing a buff there was wrong on both counts.
// Detected off msgCtx.ocarinaAction, which vanilla sets to one of the CHECK_*/recording actions
// for exactly these and to FREE_PLAY(_DONE) otherwise.
//
// Pass kNotAnOcarinaPerformance from a caller that is not a live ocarina performance, where
// ocarinaAction holds some other value and would otherwise refuse the charge based on something
// unrelated. Two kinds of caller need it:
//
//   - the Golden Gauntlets door bypass, where ocarinaAction is simply stale from whenever the
//     ocarina was last used;
//   - **every warp song's declined-prompt handler.** These fire from OnWarpSongDeclined, which is
//     raised while a *textbox* is up, and Message_StartTextbox sets ocarinaAction to 0xFFFF
//     (z_message_PAL.c:2864). That is neither FREE_PLAY nor FREE_PLAY_DONE, so the actor guard
//     below saw a warp-song decline as "played at an actor" and silently dropped cost and effect
//     for all six warp songs. Found by playtest 2026-08-06 (Requiem and Nocturne both dead);
//     regression from 87486fd7d, which added the guard on 2026-08-05, after all six had passed.
//
// The guard is not merely bypassed for warp songs, it is inapplicable to them: a warp prompt is
// only ever raised by free play, so a decline cannot be a performance aimed at Mido, Darunia, the
// frogs or a scarecrow. The songs that CAN be aimed at those - Sun's, Saria's, Epona's, Storms,
// Lullaby, Song of Time - still pass the default and still get the guard.
constexpr bool kNotAnOcarinaPerformance = false;
//
// Call SevenSagesRequestSongMagic from an OnOcarinaSongAction handler once a song is recognized
// and any song-specific guards (proximity, etc.) have passed. onSuccess runs once the magic state
// has settled and the charge is armed (magicTarget set, Magic_RequestChange succeeded) - not
// synchronously, but also not waiting for the drain to finish animating, so a caller whose effect
// is itself instant (most songs, the gauntlet door bypass) doesn't feel delayed by it.
//
// The actual magic deduction plays out as the same animated drain vanilla spells and arrows show
// (gSaveContext.magic ticking down 2/frame toward magicTarget), rather than snapping instantly -
// this module's own OnGameFrameUpdate hook watches for that drain to finish and releases the meter
// (or corrects the final value directly, if a scene transition cut the drain short) without the
// caller needing to know any of this happened. Only one request is tracked at a time, which is fine
// in practice: songs are played one at a time through the same ocarina performance UI, and the
// gauntlet bypass charges once per door open.
void SevenSagesRequestSongMagic(short cost, std::function<void()> onSuccess, bool fromOcarinaPerformance = true);
