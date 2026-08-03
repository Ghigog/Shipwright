/**
 * Seven Sages - Phase 6: Golden Gauntlets open boss doors for magic.
 *
 * Spec (docs/item-ability-overhaul.md, Golden Gauntlets): holding the Golden Gauntlets lets the
 * player force a boss-locked door without its boss key, for a large chunk of magic. Intentionally
 * a full bypass of the boss key requirement, not a discount.
 *
 * **Why this needs no logic-solver work**, despite the spec having flagged solver support as a
 * hard prerequisite: that requirement was reversed by the 2026-08-03 conservative-solver decision
 * (see the cross-cutting section at the top of the spec). The bypass is additive - it opens a door
 * that was otherwise shut - so a solver that knows nothing about it simply never places anything
 * behind it, and every seed it accepts stays beatable. Teaching the solver would instead start
 * placing progression behind "you had the gauntlets and enough magic", which is a trick-tier
 * requirement with no signposting. `Logic::CanUse(RG_MAGIC_SINGLE)` stays the stub it has always
 * been; it was only ever a blocker under the old, now-reversed requirement.
 *
 * Two sites, and the split between them is the whole design:
 *
 *   - VB_BOSS_DOOR_REQUIRE_BOSS_KEY decides. It sits in DoorShutter_Idle's approach path, which
 *     re-runs *every frame* the player stands near the door, so it must stay side-effect free -
 *     it only reports whether the player could afford the bypass. Charging here would drain the
 *     meter while the player stood still, and would rob anyone who walked away again.
 *   - OnBossDoorOpened charges. That fires from the branch that actually opens the door, the same
 *     place vanilla decrements a small key for an ordinary locked door, and it runs exactly once.
 *
 * Magic goes through Magic_RequestChange directly rather than SevenSagesRequestSongMagic. That
 * helper exists for the deferred-request problem specific to OnOcarinaSongAction callbacks (they
 * fire before the magic state machine has settled); a door opening during ordinary gameplay has
 * no such timing hazard, so this is the same plain call arrows and spells make.
 *
 * The door plays its usual unlock sound either way, so a bypass reads as "the door gave way"
 * rather than announcing itself. Whether that wants its own distinct feedback is a playtest
 * question, deliberately left alone for now.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" PlayState* gPlayState;

namespace {

// Matches the songs' cost (docs/item-ability-overhaul.md settled 24 as the one "expensive" magic
// tier rather than inventing a second). Forcing a boss door therefore costs the same as a Nayru's
// Love cast.
constexpr s16 BOSS_DOOR_MAGIC_COST = 24;

bool HasGoldenGauntlets() {
    return CUR_UPG_VALUE(UPG_STRENGTH) == 3;
}

// Affordability only - no state touched. See the header comment for why this must stay pure.
bool CanForceBossDoor() {
    return GameInteractor::IsSaveLoaded(true) && HasGoldenGauntlets() &&
           gSaveContext.magic >= BOSS_DOOR_MAGIC_COST;
}

void SevenSagesGoldenGauntletsBossDoorOpened(uint16_t mapIndex) {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    // Holding the key means this was an ordinary opening and costs nothing. Re-checked here rather
    // than remembered from the approach check, so a player who picks the key up between reaching
    // the door and opening it is not charged for a bypass they no longer needed.
    if (CHECK_DUNGEON_ITEM(DUNGEON_KEY_BOSS, mapIndex)) {
        return;
    }
    if (!HasGoldenGauntlets()) {
        return;
    }
    Magic_RequestChange(gPlayState, BOSS_DOOR_MAGIC_COST, MAGIC_CONSUME_NOW);
}

} // namespace

static void RegisterSevenSagesGoldenGauntlets() {
    COND_HOOK(OnBossDoorOpened, IS_RANDO, SevenSagesGoldenGauntletsBossDoorOpened);
    COND_VB_SHOULD(VB_BOSS_DOOR_REQUIRE_BOSS_KEY, IS_RANDO, {
        [[maybe_unused]] Actor* door = va_arg(args, Actor*);
        if (CanForceBossDoor()) {
            *should = false;
        }
    });
}

static RegisterShipInitFunc sevenSagesGoldenGauntletsInitFunc(RegisterSevenSagesGoldenGauntlets, { "IS_RANDO" });
