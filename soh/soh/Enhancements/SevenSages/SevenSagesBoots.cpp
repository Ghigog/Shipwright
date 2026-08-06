/*
 * Seven Sages boots overhaul.
 *
 * See SevenSagesBoots.h for the spec, for which clauses were already vanilla,
 * and for the list of sites these predicates are read from.
 *
 * There is deliberately no hook registration in this file, for the same reason
 * SevenSagesTunics.cpp has none: the boots are not an *event*, they are a
 * standing condition. The one piece that does need a hook - the Hover Boots run
 * speed - is folded into the VB_PLAYER_MODIFY_RUN_SPEED hook that
 * SpeedModifiers.cpp already owns, rather than registered a second time here.
 * Two hooks on the same VB would each read and write the same `*speedTarget`
 * and the result would depend on registration order.
 */

#include "soh/Enhancements/SevenSages/SevenSagesBoots.h"

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/OTRGlobals.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
}

namespace {

// The Hover Boots run-speed multiplier. 1.35 against their own 5.5 run cap is
// 7.4, about 1.24x an ordinary 6.0 run - "more speed" that is clearly felt
// without turning the boots into a movement item you never take off. Stacks
// multiplicatively with the Bunny Hood's 1.5 (SpeedModifiers.cpp), which is
// what the spec asks for.
constexpr float HOVER_BOOTS_RUN_FACTOR = 1.35f;

// How much grippier the Hover Boots are than vanilla. The spec is explicit that
// slipperiness is "slightly reduced (not eliminated)", so this multiplies the
// rates at which real velocity and facing chase the intended ones rather than
// switching the slide off. 1.5 turns vanilla's ~17 frames to reach full speed
// into ~11, and its ~7.4 deg/frame turn ceiling into ~11 - still visibly icy.
constexpr float HOVER_BOOTS_GRIP_FACTOR = 1.5f;

bool WearingBoots(const Player* player, u8 boots) {
    if (!IS_RANDO || !GameInteractor::IsSaveLoaded(true)) {
        return false;
    }
    // Player_SetBootData runs during Player_Init, before anything hands us a
    // fully-formed actor, so this is a real case rather than defensive noise.
    if (player == nullptr) {
        return false;
    }
    return player->currentBoots == boots;
}

} // namespace

bool SevenSagesIronBootsActive(const struct Player* player) {
    return WearingBoots(player, PLAYER_BOOTS_IRON);
}

bool SevenSagesHoverBootsActive(const struct Player* player) {
    return WearingBoots(player, PLAYER_BOOTS_HOVER);
}

float SevenSagesHoverBootsRunSpeedFactor(const struct Player* player) {
    return SevenSagesHoverBootsActive(player) ? HOVER_BOOTS_RUN_FACTOR : 1.0f;
}

float SevenSagesHoverBootsSlipGripFactor(const struct Player* player, bool otherSlipSourceActive) {
    if (otherSlipSourceActive || !SevenSagesHoverBootsActive(player)) {
        return 1.0f;
    }
    return HOVER_BOOTS_GRIP_FACTOR;
}
