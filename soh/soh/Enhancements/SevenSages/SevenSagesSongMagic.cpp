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

bool sPending = false;
s32 sPendingFrames = 0;
s16 sPendingCost = 0;
std::function<void()> sPendingOnSuccess;

void SevenSagesSongMagicFrameUpdate() {
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
    // that target by 2/frame over several frames of normal gameplay. Callers whose effect
    // triggers a scene transition (e.g. warp songs) would lose that drain the same way Song of
    // Time originally did, so fast-forward it here unconditionally rather than trust every caller
    // to remember why.
    gSaveContext.magic = gSaveContext.magicTarget;
    gSaveContext.magicState = MAGIC_STATE_IDLE;

    if (onSuccess) {
        onSuccess();
    }
}

} // namespace

void SevenSagesRequestSongMagic(short cost, std::function<void()> onSuccess) {
    sPending = true;
    sPendingFrames = 0;
    sPendingCost = cost;
    sPendingOnSuccess = std::move(onSuccess);
}

static void RegisterSevenSagesSongMagic() {
    COND_HOOK(OnGameFrameUpdate, IS_RANDO, SevenSagesSongMagicFrameUpdate);
}

static RegisterShipInitFunc sevenSagesSongMagicInitFunc(RegisterSevenSagesSongMagic, { "IS_RANDO" });
