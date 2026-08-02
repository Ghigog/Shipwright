/**
 * Ganon's Curse - Phase 6: Requiem of Spirit, declined, free spell casts for 20 seconds.
 *
 * See GanonsCurseMinuetOfForest.cpp's header for the shared "No" mechanism. Costs 24 magic via the
 * shared helper, then waives the cost of Din's Fire, Nayru's Love, and Farore's Wind for the buff's
 * duration - the three-spell system funnels through one exact call site,
 * z_player.c's func_8083AF44 (`Magic_RequestChange(play, sMagicSpellCosts[magicSpell],
 * MAGIC_CONSUME_WAIT_PREVIEW)`), so a single new hook there covers all three rather than needing
 * per-spell wiring. VB_PLAYER_MODIFY_MAGIC_SPELL_COST (new, see GIVanillaBehavior.h) exposes the
 * about-to-be-deducted cost by pointer, same shape as Epona's Song's VB_PLAYER_MODIFY_RUN_SPEED.
 *
 * Zeroing that cost is necessary but NOT sufficient, and getting this wrong is worse than doing
 * nothing: Magic_RequestChange only arms the drain (magicTarget = magic - cost), and
 * MAGIC_STATE_CONSUME then subtracts 2/frame until magic *equals* magicTarget. With cost 0 the
 * target is the magic the player already has, which a countdown starting from that same value can
 * never hit, so it drains to empty instead - the whole bar, not zero. So the cast also has to skip
 * the drain state entirely, via VB_PLAYER_CONSUME_MAGIC_SPELL_COST at the one place a successful
 * cast arms it. That is not a new code path: it is exactly what vanilla already does for Farore's
 * Wind's free return, where the spell actor's Destroy resets the meter with nothing consumed.
 *
 * The same waived cost is also asked for at Player_ProcessItemButtons' "can I afford this?" gate,
 * so a buffed player can cast below the spell's nominal cost - otherwise "spells cost no magic"
 * would still refuse to cast at low magic.
 *
 * **Scope, corrected 2026-08-01.** The first build read "spells" narrowly as the three C-button
 * spells and deliberately left magic arrows out. That was never what the spec asked for - it says
 * "Spells cost no magic" - so the waiver now covers every way Link spends magic except one:
 *
 *   - the three C-button spells (the two hooks above)
 *   - Fire/Ice/Light Arrows, through SoH's existing VB_PLAYER_ARROW_MAGIC_CONSUMPTION, which skips
 *     the Magic_RequestChange call outright rather than zeroing a cost, so the drain trap above
 *     doesn't apply and the arrow keeps its element instead of degrading to ARROW_NORMAL
 *   - the Lens of Truth's periodic drain, through the new VB_PLAYER_CONSUME_LENS_MAGIC
 *
 * Song costs are the deliberate exception, this song's own 24 included: Requiem is paid for with
 * magic, and a buff that pays for the songs that grant buffs is a different (and much larger)
 * design decision than "spells are free."
 *
 * SoH's "arrow cycle" enhancement defers arrow magic from draw time to arrow-spawn time, which
 * would have bypassed the waiver whenever it is enabled. ArrowCycle.cpp now asks
 * VB_PLAYER_CONSUME_ARROW_MAGIC at that deferred point, so both paths honour this song.
 *
 * One known edge, left alone on purpose: the Lens still needs a non-empty meter to switch on and
 * still shuts off when the meter hits 0 - those are availability checks rather than consumption,
 * living in two further places, so a player at exactly 0 magic can cast a free spell but cannot
 * start a free Lens.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/GanonsCurse/GanonsCurseSongMagic.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" PlayState* gPlayState;

namespace {

constexpr s16 REQUIEM_MAGIC_COST = 24;
constexpr s32 REQUIEM_BUFF_FRAMES = 20 * 20;

s32 sBuffFramesRemaining = 0;

void GanonsCurseRequiemOfSpiritDeclined() {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    if (gPlayState->msgCtx.lastPlayedSong != OCARINA_SONG_REQUIEM) {
        return;
    }

    GanonsCurseRequestSongMagic(REQUIEM_MAGIC_COST, []() { sBuffFramesRemaining = REQUIEM_BUFF_FRAMES; });
}

void GanonsCurseRequiemOfSpiritFrameUpdate() {
    if (sBuffFramesRemaining > 0) {
        sBuffFramesRemaining--;
    }
}

} // namespace

static void RegisterGanonsCurseRequiemOfSpirit() {
    COND_HOOK(OnWarpSongDeclined, IS_RANDO, GanonsCurseRequiemOfSpiritDeclined);
    COND_HOOK(OnGameFrameUpdate, IS_RANDO, GanonsCurseRequiemOfSpiritFrameUpdate);
    COND_VB_SHOULD(VB_PLAYER_MODIFY_MAGIC_SPELL_COST, IS_RANDO, {
        [[maybe_unused]] Player* player = va_arg(args, Player*);
        s16* cost = va_arg(args, s16*);
        if (sBuffFramesRemaining > 0) {
            *cost = 0;
        }
    });
    COND_VB_SHOULD(VB_PLAYER_CONSUME_MAGIC_SPELL_COST, IS_RANDO, {
        [[maybe_unused]] Player* player = va_arg(args, Player*);
        if (sBuffFramesRemaining > 0) {
            *should = false;
        }
    });
    COND_VB_SHOULD(VB_PLAYER_ARROW_MAGIC_CONSUMPTION, IS_RANDO, {
        [[maybe_unused]] Player* player = va_arg(args, Player*);
        [[maybe_unused]] int32_t magicArrowType = va_arg(args, int32_t);
        [[maybe_unused]] int32_t* arrowType = va_arg(args, int32_t*);
        if (sBuffFramesRemaining > 0) {
            *should = false;
        }
    });
    COND_VB_SHOULD(VB_PLAYER_CONSUME_ARROW_MAGIC, IS_RANDO, {
        [[maybe_unused]] int32_t magicArrowType = va_arg(args, int32_t);
        if (sBuffFramesRemaining > 0) {
            *should = false;
        }
    });
    COND_VB_SHOULD(VB_PLAYER_CONSUME_LENS_MAGIC, IS_RANDO, {
        if (sBuffFramesRemaining > 0) {
            *should = false;
        }
    });
}

static RegisterShipInitFunc ganonsCurseRequiemOfSpiritInitFunc(RegisterGanonsCurseRequiemOfSpirit, { "IS_RANDO" });
