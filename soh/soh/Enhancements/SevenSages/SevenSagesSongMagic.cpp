#include "soh/Enhancements/SevenSages/SevenSagesSongMagic.h"

#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" PlayState* gPlayState;

namespace {

// Safety cap only - magicState settling back to IDLE is normally a 1-frame wait. Never observed
// to matter in practice; exists so a pending request can't theoretically hang forever.
constexpr s32 PENDING_REQUEST_FRAME_LIMIT = 60;

// Safety cap for the release watch below - the natural drain (2/frame) finishes in well under 60
// frames for any cost this overhaul uses (24 is the largest, ~12 frames). Generous margin over that.
constexpr s32 RELEASE_WATCH_FRAME_LIMIT = 120;

// Was the performance that just finished aimed at something in the world rather than played by the
// player on their own? Free play is the only ocarina action that means "the player pulled out the
// ocarina and played"; every other one vanilla sets is a performance at a target - the whole
// CHECK_* block (z64.h's OcarinaSongActionId comments it "Playing songs for check spots": Mido,
// Darunia, the Skull Kid, En_Okarina_Tag spots), CHECK_NOWARP for the frogs (z_en_fr.c:676), the
// memory game, and the scarecrow recording/playback pair. Testing the inverse rather than
// enumerating them keeps any action added later on the safe side of this guard.
//
// Null gPlayState answers "yes" so a request can never slip through unguarded on the way in or out
// of a save; every real caller runs mid-gameplay with a save loaded and never sees this.
bool PerformedForActor() {
    if (gPlayState == nullptr) {
        return true;
    }
    const u16 action = gPlayState->msgCtx.ocarinaAction;
    return action != OCARINA_ACTION_FREE_PLAY && action != OCARINA_ACTION_FREE_PLAY_DONE;
}

bool sPending = false;
s32 sPendingFrames = 0;
s16 sPendingCost = 0;
std::function<void()> sPendingOnSuccess;

// Armed once a charge succeeds, to release the meter as soon as the real drain finishes instead of
// leaving it stuck - see the release-watch comment below for the full reasoning.
bool sReleaseWatchActive = false;
s32 sReleaseWatchFrames = 0;
// s8 to match gSaveContext.magic, which is where this gets written back. magicTarget is declared
// s16 but only ever holds magic +/- a cost that Magic_RequestChange has already range-checked
// against 0 and magicCapacity, so it always fits - see the cast at the assignment below.
s8 sReleaseWatchTarget = 0;

void SevenSagesSongMagicFrameUpdate() {
    if (sReleaseWatchActive) {
        // Magic_RequestChange's MAGIC_CONSUME_NOW leaves the real vanilla drain running -
        // gSaveContext.magic ticks down 2/frame toward magicTarget, the same animated countdown
        // Din's Fire/Farore's Wind/arrows already show, instead of the meter silently teleporting to
        // its new value. But MAGIC_STATE_METER_FLASH_1/2/3 (what the drain lands in once finished)
        // has no exit of its own - confirmed by reading z_parameter.c's state machine directly, not
        // assumed - so something has to call Magic_Reset() once it gets there, which is what this
        // watch is for.
        if (gSaveContext.magicState == MAGIC_STATE_METER_FLASH_1 ||
            gSaveContext.magicState == MAGIC_STATE_METER_FLASH_2 ||
            gSaveContext.magicState == MAGIC_STATE_METER_FLASH_3) {
            Magic_Reset(gPlayState);
            sReleaseWatchActive = false;
        } else if (gSaveContext.magicState == MAGIC_STATE_IDLE) {
            // The drain got cut short before finishing on its own - most likely a scene transition
            // fired mid-countdown (warp songs do exactly this) and z_play.c's own scene-load path
            // already called Magic_Reset for unrelated reasons, jumping straight to idle without
            // ever passing through a flash state this watch would otherwise catch. The animated
            // countdown only ever *decrements* gSaveContext.magic; it doesn't independently
            // guarantee reaching magicTarget if interrupted, so correct it directly here rather than
            // let an interrupted warp song undercharge.
            if (gSaveContext.magic != sReleaseWatchTarget) {
                gSaveContext.magic = sReleaseWatchTarget;
            }
            sReleaseWatchActive = false;
        } else if (++sReleaseWatchFrames >= RELEASE_WATCH_FRAME_LIMIT) {
            // Never observed in practice (see the frame budget comment above) - same defensive
            // stance as the pending-request cap below rather than risk hanging forever.
            gSaveContext.magic = sReleaseWatchTarget;
            Magic_Reset(gPlayState);
            sReleaseWatchActive = false;
        }
    }

    if (!sPending) {
        return;
    }

    // Mirrors the states Magic_RequestChange itself accepts for MAGIC_CONSUME_NOW - waiting for
    // one of these is what avoids racing the message system's own magicState management.
    bool magicStateSettled =
        gSaveContext.magicState == MAGIC_STATE_IDLE || gSaveContext.magicState == MAGIC_STATE_CONSUME_LENS;

    if (!magicStateSettled && sPendingFrames < PENDING_REQUEST_FRAME_LIMIT) {
        sPendingFrames++;
        return;
    }

    sPending = false;
    std::function<void()> onSuccess = std::move(sPendingOnSuccess);
    sPendingOnSuccess = nullptr;

    if (!magicStateSettled) {
        // Never observed; give up rather than spend magic against a state Magic_RequestChange
        // would've refused anyway.
        return;
    }

    if (!Magic_RequestChange(gPlayState, sPendingCost, MAGIC_CONSUME_NOW)) {
        return;
    }

    // Magic_RequestChange doesn't apply the deduction itself - MAGIC_CONSUME_NOW only arms
    // magicTarget and moves to MAGIC_STATE_CONSUME_SETUP, then drains gSaveContext.magic toward
    // that target by 2/frame over several frames of normal gameplay. Arm the release watch to
    // reclaim the meter once that finishes (or correct it if interrupted) instead of fast-forwarding
    // it here - see the release-watch block above.
    sReleaseWatchActive = true;
    sReleaseWatchFrames = 0;
    // Narrowing s16 -> s8 is safe: Magic_RequestChange returned true above, which means it
    // range-checked the cost and set magicTarget to a value inside [0, magicCapacity] (96 at most).
    sReleaseWatchTarget = static_cast<s8>(gSaveContext.magicTarget);

    if (onSuccess) {
        onSuccess();
    }
}

} // namespace

void SevenSagesRequestSongMagic(short cost, std::function<void()> onSuccess, bool fromOcarinaPerformance) {
    if (fromOcarinaPerformance && PerformedForActor()) {
        return;
    }
    sPending = true;
    sPendingFrames = 0;
    sPendingCost = cost;
    sPendingOnSuccess = std::move(onSuccess);
}

static void RegisterSevenSagesSongMagic() {
    COND_HOOK(OnGameFrameUpdate, IS_SEVENSAGES, SevenSagesSongMagicFrameUpdate);
}

static RegisterShipInitFunc sevenSagesSongMagicInitFunc(RegisterSevenSagesSongMagic, { "IS_RANDO" });
