/**
 * Ganon's Curse - Phase 4c: the universal opening, chained after the home
 * region's vanilla entrance cutscene.
 *
 * THE PROBLEM THIS SOLVES
 *
 * A sage spawns *inside* their home region at file creation, so two cutscenes
 * want the same moment: the vanilla entrance cutscene 4a restored (6 of the 7
 * sages have one - Saria's Sacred Forest Meadow is the exception), and 4c's
 * universal premise beat.
 *
 * They cannot simply both be triggered. Playing a cutscene is
 * `Cutscene_SetSegment(play, seg); gSaveContext.cutsceneTrigger = N;` and there
 * is exactly ONE csCtx.segment pointer with no queue behind it - whoever writes
 * last wins and the other is silently lost. Cutscene_HandleEntranceTriggers
 * (z_demo.c:2215) does those same two writes from Play_Init, so a naive
 * "trigger ours at startup too" stomps the entrance cutscene.
 *
 * So we sequence explicitly: arm at file creation, let the entrance cutscene
 * run, and fire ours the frame csCtx returns to idle.
 *
 * THE RACE, AND WHY sSawEntranceCsRunning EXISTS
 *
 * Cutscene_HandleEntranceTriggers only *requests* the cutscene - it sets
 * cutsceneTrigger and returns. The cutscene does not actually start until a
 * later frame, so csCtx.state is still CS_STATE_IDLE for a frame or two after
 * Play_Init. Firing on "armed && state == IDLE" alone would therefore win that
 * race and overwrite the entrance cutscene's segment before it ever plays -
 * the exact bug this file exists to avoid.
 *
 * The fix is to require having *observed* the entrance cutscene running before
 * accepting a return to idle as "it finished". Saria has no entrance cutscene
 * at all, so for her the VB hook never fires, sExpectEntranceCs stays false,
 * and we fire on the first idle frame instead of waiting for a cutscene that
 * is never coming.
 *
 * KNOWN LIMITATION: the arm flag is process-local, not saved. Quitting during
 * the entrance cutscene loses the universal opening for that file. Fixing it
 * properly means a save-flag bit, which is save-format surgery this phase does
 * not otherwise need - revisit if playtesting says it matters.
 */
// Order matters - see the identical note in GanonsCurseCutscenes.cpp. These
// plain-C++ includes must be parsed before the extern "C" block below pulls in
// the functions.h/variables.h chain, or the template-using libultraship headers
// reachable from it fail with "templates must have C++ linkage".
#include <soh/OTRGlobals.h>
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include "GanonsCurseCutscenes.h"
#include "GanonsCurseOpenings.h"

extern "C" {
extern PlayState* gPlayState;
}

namespace {

// Set once at sage file creation, cleared the moment the opening is triggered.
bool sArmed = false;

// True if a vanilla entrance cutscene was allowed to play for this spawn. Set
// from the VB_PLAY_ENTRANCE_CS hook, which runs during Play_Init - i.e. always
// before the first OnGameFrameUpdate, so the state machine below can trust it.
bool sExpectEntranceCs = false;

// Guards the race described in the file header: proves the entrance cutscene
// actually started, so a return to idle means "finished" and not "not yet".
bool sSawEntranceCsRunning = false;

void PlayUniversalOpening(PlayState* play) {
    Cutscene_SetSegment(play, gGanonsCurseUniversalOpening);
    gSaveContext.cutsceneTrigger = 1;

    sArmed = false;
    sExpectEntranceCs = false;
    sSawEntranceCsRunning = false;
}

void TickUniversalOpening() {
    if (!sArmed || gPlayState == nullptr) {
        return;
    }

    const bool cutscenePlaying = gPlayState->csCtx.state != CS_STATE_IDLE;

    if (sExpectEntranceCs) {
        if (cutscenePlaying) {
            // The entrance cutscene is on screen. Note it and keep waiting.
            sSawEntranceCsRunning = true;
            return;
        }
        if (!sSawEntranceCsRunning) {
            // Requested but not started yet - firing here would stomp its segment.
            return;
        }
    } else if (cutscenePlaying) {
        // No entrance cutscene expected, but something else is playing. Wait it
        // out rather than fighting it for the segment pointer.
        return;
    }

    PlayUniversalOpening(gPlayState);
}

void RegisterGanonsCurseOpenings() {
    COND_HOOK(OnGameFrameUpdate, IS_RANDO, TickUniversalOpening);

    // Observe, don't override: `should` is passed through untouched so 4a's
    // restored entrance cutscenes still play exactly as they would otherwise.
    // We only need to know *whether* one is coming, to pick which branch of the
    // state machine above applies.
    COND_VB_SHOULD(VB_PLAY_ENTRANCE_CS, IS_RANDO, {
        if (sArmed && *should) {
            sExpectEntranceCs = true;
        }
    });
}

} // namespace

extern "C" void GanonsCurse_ArmUniversalOpening(void) {
    sArmed = true;
    sExpectEntranceCs = false;
    sSawEntranceCsRunning = false;
}

static RegisterShipInitFunc ganonsCurseOpeningsInitFunc(RegisterGanonsCurseOpenings, { "IS_RANDO" });
