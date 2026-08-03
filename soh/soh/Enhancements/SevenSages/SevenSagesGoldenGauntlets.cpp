/**
 * Seven Sages - Phase 6: gauntlets force locked doors open for magic.
 *
 * Spec (docs/item-ability-overhaul.md, Silver and Golden Gauntlets):
 *   - Silver Gauntlets force a "silver lock" - any door that normally wants a small key.
 *   - Golden Gauntlets are a superset: silver locks *and* boss locks.
 * Both cost **half the player's full magic meter**, so 24 on a single bar and 48 on a double.
 * Deliberately a fraction of capacity rather than a flat number: a flat 24 is half a bar early on
 * but only a quarter once the meter is upgraded, which would make the ability quietly cheaper the
 * further you get. Note this is a different rule from the songs' flat 24 - the songs are repeatable
 * utility, this is a key.
 *
 * **Why this needs no logic-solver work**, despite the spec having flagged solver support as a
 * hard prerequisite: that requirement was reversed by the 2026-08-03 conservative-solver decision
 * (see the cross-cutting section at the top of the spec). Both bypasses are additive - they open
 * doors that were otherwise shut - so a solver that knows nothing about them simply never places
 * anything behind them, and every seed it accepts stays beatable. Teaching the solver would
 * instead start placing progression behind "you had the gauntlets and enough magic", which is a
 * trick-tier requirement with no signposting. `Logic::CanUse(RG_MAGIC_SINGLE)` stays the stub it
 * has always been; it was only ever a blocker under the old, now-reversed requirement.
 *
 * Three hooks, and the decide/charge split between them is the whole design:
 *
 *   - VB_BOSS_DOOR_REQUIRE_BOSS_KEY and VB_DOOR_SHUTTER_REQUIRE_SMALL_KEY *decide*. Both sit in
 *     DoorShutter_Idle's approach path, which re-runs **every frame** the player stands near the
 *     door, so both must stay side-effect free - they only report whether the player could afford
 *     it. Charging there would drain the meter while the player stood still, and would rob anyone
 *     who walked up and changed their mind.
 *   - OnBossDoorOpened and VB_DOOR_SHUTTER_CONSUME_SMALL_KEY *charge*. Both fire from the branch
 *     that actually opens the door - the same place vanilla decrements a small key - and run
 *     exactly once.
 *
 * The two door types differ in one important way. A boss door never decrements anything in
 * vanilla, so bypassing it disturbs no accounting. An ordinary locked door *does*, so the small-key
 * bypass has to suppress that decrement or the count underflows to -1. That is what
 * VB_DOOR_SHUTTER_CONSUME_SMALL_KEY exists for, and its default (`keys > 0`) means the underflow
 * is impossible even if this file is never loaded.
 *
 * Magic goes through Magic_RequestChange directly rather than SevenSagesRequestSongMagic. That
 * helper exists for the deferred-request problem specific to OnOcarinaSongAction callbacks (they
 * fire before the magic state machine has settled); a door opening during ordinary gameplay has
 * no such timing hazard, so this is the same plain call arrows and spells make.
 *
 * Both doors play their usual unlock sound either way, so a bypass reads as "the door gave way"
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

constexpr u8 STRENGTH_SILVER_GAUNTLETS = 2;
constexpr u8 STRENGTH_GOLDEN_GAUNTLETS = 3;

// Half the player's full meter: 24 on a single bar, 48 on a double. magicLevel is the capacity in
// units of MAGIC_NORMAL_METER (1 or 2), and is what z_parameter.c itself uses to size the bar.
s16 DoorMagicCost() {
    return (gSaveContext.magicLevel * MAGIC_NORMAL_METER) / 2;
}

bool HasStrengthAtLeast(u8 tier) {
    return CUR_UPG_VALUE(UPG_STRENGTH) >= tier;
}

// Affordability only - no state touched. See the header comment for why these must stay pure.
bool CanForceDoor(u8 requiredTier) {
    return GameInteractor::IsSaveLoaded(true) && HasStrengthAtLeast(requiredTier) &&
           gSaveContext.magic >= DoorMagicCost();
}

void ChargeForBypass() {
    Magic_RequestChange(gPlayState, DoorMagicCost(), MAGIC_CONSUME_NOW);
}

void SevenSagesGauntletsBossDoorOpened(uint16_t mapIndex) {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    // Holding the key means this was an ordinary opening and costs nothing. Re-checked here rather
    // than remembered from the approach check, so a player who picks the key up between reaching
    // the door and opening it is not charged for a bypass they no longer needed.
    if (CHECK_DUNGEON_ITEM(DUNGEON_KEY_BOSS, mapIndex)) {
        return;
    }
    if (!HasStrengthAtLeast(STRENGTH_GOLDEN_GAUNTLETS)) {
        return;
    }
    ChargeForBypass();
}

} // namespace

static void RegisterSevenSagesGoldenGauntlets() {
    COND_HOOK(OnBossDoorOpened, IS_RANDO, SevenSagesGauntletsBossDoorOpened);

    COND_VB_SHOULD(VB_BOSS_DOOR_REQUIRE_BOSS_KEY, IS_RANDO, {
        [[maybe_unused]] Actor* door = va_arg(args, Actor*);
        if (CanForceDoor(STRENGTH_GOLDEN_GAUNTLETS)) {
            *should = false;
        }
    });

    COND_VB_SHOULD(VB_DOOR_SHUTTER_REQUIRE_SMALL_KEY, IS_RANDO, {
        [[maybe_unused]] Actor* door = va_arg(args, Actor*);
        if (CanForceDoor(STRENGTH_SILVER_GAUNTLETS)) {
            *should = false;
        }
    });

    COND_VB_SHOULD(VB_DOOR_SHUTTER_CONSUME_SMALL_KEY, IS_RANDO, {
        [[maybe_unused]] Actor* door = va_arg(args, Actor*);
        // *should is already false when the player has no key, which is only reachable via the
        // bypass above - the approach check would otherwise have refused. Suppressing the
        // decrement is the default's job; ours is to take the magic instead.
        if (!*should && HasStrengthAtLeast(STRENGTH_SILVER_GAUNTLETS)) {
            ChargeForBypass();
        }
    });
}

static RegisterShipInitFunc sevenSagesGoldenGauntletsInitFunc(RegisterSevenSagesGoldenGauntlets, { "IS_RANDO" });
