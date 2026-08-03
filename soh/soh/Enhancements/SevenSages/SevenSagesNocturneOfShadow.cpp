/**
 * Seven Sages - Phase 6: Nocturne of Shadow, declined, unseen by guards for 20 seconds.
 *
 * See SevenSagesMinuetOfForest.cpp's header for the shared "No" mechanism. Costs 24 magic via the
 * shared helper, then no guard in the game spots the player for the duration.
 *
 * Vanilla has no shared detection code whatsoever - every guard inlines its own cone-and-distance
 * test, and none of them route through a common Player or Actor flag - so the research question for
 * this song was never "where is the check" but "how many checks are there". The answer is four
 * actors, gated through one new VB_GUARD_DETECT_PLAYER hook:
 *
 *   - En_Heishi1: castle courtyard hedge guards. Two sites - the night guard's proximity catch, and
 *     the searchlight catch. The latter needs the latched `linkDetected` flag *cleared* rather than
 *     just ignored, because the search-ball effect writes it by pointer; leaving it set would have
 *     the guard pounce the moment the buff expires.
 *   - En_Heishi3: the courtyard and castle-front sentries. Two sites, both guarding `sPlayerCaught`.
 *   - En_Ge1: Gerudo Fortress, the kick-out guard. Two watch routines.
 *   - En_Ge2: Gerudo Fortress patrols - the ones that actually capture and jail. Everything routes
 *     through Ge2_DetectPlayerInAction and Ge2_DetectPlayerInUpdate, so two guards cover the actor.
 *
 * **En_Heishi2 and En_Heishi4 are deliberately untouched.** Despite the name they are not alert
 * guards: their distance checks are Actor_OfferTalk* conversation ranges (the Zelda's Letter
 * exchange, ordinary chat). An earlier scoping pass listed En_Heishi2 as a detection site and
 * missed En_Ge2 entirely - patching the former would have broken talking to guards while leaving
 * the single most important guard in the feature's premise still able to see you.
 *
 * Two deliberate limits. Attacking a guard still gives you away - the hook sits on the vision
 * checks, never inside EnGe1_SpotPlayer / EnGe2_SetupCapturePlayer, so the AC_HIT paths are
 * untouched. And a capture already under way still plays out; the buff prevents fresh detection
 * rather than unwinding four different in-progress capture state machines.
 *
 * The inverted-Lens-of-Truth visual that pairs with this is a separate follow-up (see
 * item-ability-overhaul.md) - this file is the gameplay half only.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/SevenSages/SevenSagesSongMagic.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" PlayState* gPlayState;

namespace {

constexpr s16 NOCTURNE_MAGIC_COST = 24;
constexpr s32 NOCTURNE_BUFF_FRAMES = 20 * 20;

s32 sBuffFramesRemaining = 0;

void SevenSagesNocturneOfShadowDeclined() {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    if (gPlayState->msgCtx.lastPlayedSong != OCARINA_SONG_NOCTURNE) {
        return;
    }

    SevenSagesRequestSongMagic(NOCTURNE_MAGIC_COST, []() { sBuffFramesRemaining = NOCTURNE_BUFF_FRAMES; });
}

void SevenSagesNocturneOfShadowFrameUpdate() {
    if (sBuffFramesRemaining > 0) {
        sBuffFramesRemaining--;
    }
}

} // namespace

static void RegisterSevenSagesNocturneOfShadow() {
    COND_HOOK(OnWarpSongDeclined, IS_RANDO, SevenSagesNocturneOfShadowDeclined);
    COND_HOOK(OnGameFrameUpdate, IS_RANDO, SevenSagesNocturneOfShadowFrameUpdate);
    COND_VB_SHOULD(VB_GUARD_DETECT_PLAYER, IS_RANDO, {
        [[maybe_unused]] Actor* guard = va_arg(args, Actor*);
        if (sBuffFramesRemaining > 0) {
            *should = false;
        }
    });
}

static RegisterShipInitFunc sevenSagesNocturneOfShadowInitFunc(RegisterSevenSagesNocturneOfShadow, { "IS_RANDO" });
