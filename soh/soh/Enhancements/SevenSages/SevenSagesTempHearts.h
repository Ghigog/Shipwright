#pragma once

/**
 * Seven Sages - Phase 6: the Sun's Song temporary-heart pool.
 *
 * Split out from SevenSagesSunsSong.cpp because the health bookkeeping has to run every frame and
 * has to be reachable from two places outside the song itself: SaveManager (to keep the hearts out
 * of the save file) and z_lifemeter.c (to draw them in a different colour). C linkage throughout
 * so the C side can call in.
 */

#ifdef __cplusplus
extern "C" {
#endif

// Top the temporary-heart pool back up to its cap (one full heart, plus a quarter heart for every
// heart piece ever found). Safe to call repeatedly - that's the point, it's how the song is meant
// to be used. Grants nothing if the pool is already at cap.
void SevenSagesGrantTempHearts(void);

// Save round-trip. SaveManager::SaveFile calls Suspend immediately before it snapshots gSaveContext
// and Restore immediately after, so the file on disk holds the player's own health and capacity
// while the live buff survives the save untouched.
//
// These must stay paired and must not be separated by a frame: SaveSection's memcpy of gSaveContext
// happens synchronously on the calling thread (only the write-out is threaded), so the snapshot is
// already taken by the time SaveSection returns and Restore is safe there. Suspend on its own would
// silently end the buff, which is the bug this pair replaced.
void SevenSagesSuspendTempHeartsForSave(void);
void SevenSagesRestoreTempHeartsAfterSave(void);

// Heart index at which the temporary hearts begin, or -1 when none are held. HealthMeter_Draw uses
// this to colour the topmost filled hearts differently from the player's real ones.
int SevenSagesTempHeartStartIndex(void);

// Read-only peek at the player's own heart capacity with the temporary pool excluded - what
// Suspend would leave behind, without actually suspending. Returns false and touches nothing when
// no temporary hearts are held.
//
// Exists for Anchor's UPDATE_TEAM_STATE, which snapshots gSaveContext from the save thread after
// the main thread has already restored the buff, and would otherwise ship inflated capacity to
// teammates as permanent hearts. That caller cannot use the Suspend/Restore pair above: those
// share a file-scope static and must be paired within one frame, so calling them off-thread would
// race the main thread's own pair and move the live player's health as a side effect. Reading the
// pool is safe; moving it is not.
// Returns 1 when temporary hearts are held (and writes capacityOut), 0 otherwise. int rather than
// bool to match SevenSagesTempHeartStartIndex above and keep this header free of <stdbool.h> for
// the C translation units that include it.
int SevenSagesPeekTempHeartCapacity(short* capacityOut);

#ifdef __cplusplus
}
#endif
