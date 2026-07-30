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
 * ORDER: PREMISE FIRST, THEN THE ESTABLISHING SHOT
 *
 * The universal opening replaces vanilla's Navi-wakes-Link intro, so it goes
 * first; the home region's entrance cutscene is the establishing shot that
 * follows it.
 *
 * Getting that order is not just "trigger ours at startup", because
 * Cutscene_HandleEntranceTriggers has already queued the entrance cutscene by
 * the time we get a frame. So we *displace* it: take the segment it queued,
 * put ours in its place, and replay the displaced one once ours has finished.
 *
 * Displacing rather than suppressing (via VB_PLAY_ENTRANCE_CS) is deliberate -
 * it leaves sEntranceCutsceneTable's age and event-flag logic completely
 * intact and simply reuses its answer, instead of reimplementing the decision
 * about which cutscene this spawn should get.
 *
 * THE RACE, AND WHY sSawOpeningRunning EXISTS
 *
 * Cutscene_HandleEntranceTriggers only *requests* its cutscene - it sets
 * cutsceneTrigger and returns. Nothing actually starts until a later frame, so
 * csCtx.state is still CS_STATE_IDLE for a frame or two after Play_Init. That
 * window is what lets us swap the segment safely, but it also means a return
 * to idle right after triggering ours means "hasn't started yet", not
 * "finished". So we require having *observed* our cutscene running before
 * accepting idle as the end of it - otherwise we would replay the entrance
 * cutscene instantly and stomp our own opening.
 *
 * Saria's Sacred Forest Meadow has no entrance cutscene, so nothing gets
 * displaced, sDisplacedEntranceCs stays null, and the sequence simply ends
 * after the opening.
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

// Set once at sage file creation, cleared once the whole sequence is done.
bool sArmed = false;

// True once the universal opening has been triggered and we are waiting on it.
bool sOpeningTriggered = false;

// Guards the race described in the file header: proves our cutscene actually
// started, so a return to idle means "finished" and not "not yet".
bool sSawOpeningRunning = false;

// The entrance cutscene Play_Init had queued, displaced so ours can go first
// and replayed once ours ends. Null when the sage's home has no entrance
// cutscene (Saria), in which case nothing is replayed.
void* sDisplacedEntranceCs = nullptr;

// Frames the opening must run before it will accept a skip. Matches the
// threshold vanilla uses in Cutscene_Command_Terminator, so an input already
// buffered when the cutscene starts cannot skip it instantly.
constexpr s16 kSkipGraceFrames = 20;

/**
 * Let the player skip the opening with A, B or Start.
 *
 * This has to be our own implementation. Vanilla's skip lives entirely inside
 * Cutscene_Command_Terminator (z_demo.c) - and 4b cutscenes deliberately have
 * no CS_TERMINATOR, so that code never runs for ours. Vanilla also gates that
 * path on gameMode != GAMEMODE_NORMAL, which is false here: the opening plays
 * during ordinary gameplay.
 *
 * Skipping is done by jumping csCtx.frames to the cutscene's own end frame
 * rather than by tearing the cutscene down, so it finishes through its normal
 * path and we inherit whatever cleanup that does. The end frame is read out of
 * the cutscene data itself (CS_BEGIN_CUTSCENE emits totalEntries then
 * endFrame), so it cannot drift out of sync with cutscenes.json.
 *
 * Any textbox still open is closed first - the cutscene's text commands are
 * timed against frames we are about to skip past, so nothing else would close
 * it and it would linger after control returns.
 */
void TrySkipOpening() {
    CutsceneContext& csCtx = gPlayState->csCtx;

    if (csCtx.frames <= kSkipGraceFrames) {
        return;
    }

    const u32 pressed = gPlayState->state.input[0].press.button;
    if (!CHECK_BTN_ALL(pressed, BTN_A) && !CHECK_BTN_ALL(pressed, BTN_B) &&
        !CHECK_BTN_ALL(pressed, BTN_START)) {
        return;
    }

    if (gPlayState->msgCtx.msgMode != MSGMODE_NONE) {
        Message_CloseTextbox(gPlayState);
    }

    // CS_BEGIN_CUTSCENE(totalEntries, endFrame) - [1] is the end frame.
    // CutsceneData is a union; .i is the s32 view the CMD_W macro writes.
    const s16 endFrame = static_cast<s16>(gGanonsCurseUniversalOpening[1].i);
    if (csCtx.frames < endFrame) {
        csCtx.frames = endFrame;
    }

    // Same confirmation sound vanilla plays when a cutscene is skipped.
    Audio_PlaySoundGeneral(NA_SE_SY_PIECE_OF_HEART, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

void TickUniversalOpening() {
    if (!sArmed || gPlayState == nullptr) {
        return;
    }

    CutsceneContext& csCtx = gPlayState->csCtx;
    const bool cutscenePlaying = csCtx.state != CS_STATE_IDLE;

    if (!sOpeningTriggered) {
        // Only safe to touch csCtx.segment while idle. Once a cutscene is
        // actually running the segment is being read, and swapping it would
        // corrupt playback mid-scene.
        if (cutscenePlaying) {
            return;
        }

        // Displace whatever Cutscene_HandleEntranceTriggers queued during
        // Play_Init, so the premise beat plays first and the home region's
        // establishing shot follows it. Taking the segment here rather than
        // suppressing it via VB_PLAY_ENTRANCE_CS keeps sEntranceCutsceneTable's
        // age/flag logic entirely intact - we reuse its answer instead of
        // reimplementing it.
        if (gSaveContext.cutsceneTrigger != 0 && csCtx.segment != nullptr) {
            sDisplacedEntranceCs = csCtx.segment;
        }

        Cutscene_SetSegment(gPlayState, gGanonsCurseUniversalOpening);
        gSaveContext.cutsceneTrigger = 1;
        sOpeningTriggered = true;
        sSawOpeningRunning = false;
        return;
    }

    if (cutscenePlaying) {
        sSawOpeningRunning = true;
        TrySkipOpening();
        return;
    }
    if (!sSawOpeningRunning) {
        // Triggered but not started yet - a return to idle here means "not yet",
        // not "finished".
        return;
    }

    // The opening has played out. Hand the moment back to the entrance cutscene.
    sArmed = false;
    sOpeningTriggered = false;
    sSawOpeningRunning = false;

    if (sDisplacedEntranceCs != nullptr) {
        Cutscene_SetSegment(gPlayState, sDisplacedEntranceCs);
        gSaveContext.cutsceneTrigger = 1;
        sDisplacedEntranceCs = nullptr;
    }
}

void RegisterGanonsCurseOpenings() {
    COND_HOOK(OnGameFrameUpdate, IS_RANDO, TickUniversalOpening);
}

} // namespace

extern "C" void GanonsCurse_ArmUniversalOpening(void) {
    sArmed = true;
    sOpeningTriggered = false;
    sSawOpeningRunning = false;
    sDisplacedEntranceCs = nullptr;
}

static RegisterShipInitFunc ganonsCurseOpeningsInitFunc(RegisterGanonsCurseOpenings, { "IS_RANDO" });
