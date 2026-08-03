#pragma once

/**
 * Seven Sages - Phase 6: the Sun's Song temporary-heart pool.
 *
 * Split out from SevenSagesSunsSong.cpp because the health bookkeeping has to run every frame and
 * has to be reachable from two places outside the song itself: SaveManager (to strip the hearts
 * when the game saves) and z_lifemeter.c (to draw them in a different colour). C linkage throughout
 * so the C side can call in.
 */

#ifdef __cplusplus
extern "C" {
#endif

// Top the temporary-heart pool back up to its cap (one full heart, plus a quarter heart for every
// heart piece ever found). Safe to call repeatedly - that's the point, it's how the song is meant
// to be used. Grants nothing if the pool is already at cap.
void SevenSagesGrantTempHearts(void);

// Drop every temporary heart immediately, restoring the player's own capacity and clamping health
// into it. Called when the game saves, so temporary hearts never reach the save file.
void SevenSagesStripTempHearts(void);

// Heart index at which the temporary hearts begin, or -1 when none are held. HealthMeter_Draw uses
// this to colour the topmost filled hearts differently from the player's real ones.
int SevenSagesTempHeartStartIndex(void);

#ifdef __cplusplus
}
#endif
