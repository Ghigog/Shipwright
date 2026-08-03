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
 * **Locked doors live in three unrelated actors**, which is the trap this feature fell into: the
 * first build hooked only Door_Shutter (sliding doors, and every boss door), so boss locks worked
 * and ordinary ones did not. The three are:
 *   - Door_Shutter - boss doors and sliding shutters. Needed new hooks; it had none.
 *   - En_Door - ordinary hinged locked doors, which is what most dungeon "silver locks" are.
 *     Upstream already provides VB_NOT_HAVE_SMALL_KEY and VB_CONSUME_SMALL_KEY here, so this only
 *     had to register for them.
 *   - Door_Gerudo - the Gerudo Fortress cell doors. **Deliberately not covered**: it has no hooks
 *     at all, its keys are `Gerudo Fortress Keys: Vanilla` in our preset, and it is a handful of
 *     doors in one area rather than a dungeon-wide lock type.
 *
 * The decide/charge split below is the whole design:
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
 * ~~Magic goes through Magic_RequestChange directly ... a door opening during ordinary gameplay
 * has no such timing hazard.~~ **Wrong, and found in playtest**: the hazard is not specific to
 * ocarina callbacks at all. Magic_RequestChange accepts MAGIC_CONSUME_NOW only from
 * MAGIC_STATE_IDLE or MAGIC_STATE_CONSUME_LENS; from MAGIC_STATE_CONSUME - the several-frame
 * drain animation left by the *previous* charge - it plays NA_SE_SY_ERROR and returns false.
 * Forcing two doors in quick succession therefore gave an error sound, an opened door, and no
 * magic spent, because the gate had already granted passage before the charge was attempted.
 *
 * Handled with a pending-charge retry rather than by refusing at the gate: the debt is recorded
 * when the door opens and flushed on the first frame the magic system will accept it. The gate
 * also refuses while a charge is outstanding, since until it lands the meter still reads full and
 * a second door would otherwise be paid for with magic already spoken for.
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

// Child Link cannot use either pair of gauntlets. CUR_UPG_VALUE(UPG_STRENGTH) is a save value, not
// an equip state, so it stays set across an age change - without the age test a child who had been
// adult (or was handed the upgrade by the debug console) forces doors barehanded.
bool CanUseGauntlets(u8 tier) {
    return LINK_IS_ADULT && CUR_UPG_VALUE(UPG_STRENGTH) >= tier;
}

// Magic_RequestChange only accepts MAGIC_CONSUME_NOW from these two states (z_parameter.c:3096).
// From any other - notably MAGIC_STATE_CONSUME, the several-frame drain animation left behind by
// the *previous* charge - it plays NA_SE_SY_ERROR and returns false.
bool MagicSystemReady() {
    return gSaveContext.magicState == MAGIC_STATE_IDLE || gSaveContext.magicState == MAGIC_STATE_CONSUME_LENS;
}

// A charge that has been incurred but not yet accepted by the magic system. Non-zero means the
// player owes magic for a door they have already been let through.
s16 sPendingCharge = 0;

void TryFlushPendingCharge() {
    if (sPendingCharge <= 0 || !GameInteractor::IsSaveLoaded(true)) {
        return;
    }
    // Checked rather than just attempted: calling into a busy magic system is what plays the error
    // sound, so attempting-and-failing every frame would machine-gun it.
    if (!MagicSystemReady()) {
        return;
    }
    if (Magic_RequestChange(gPlayState, sPendingCharge, MAGIC_CONSUME_NOW)) {
        sPendingCharge = 0;
    }
}

// Affordability only - no state touched. See the header comment for why these must stay pure.
bool CanForceDoor(u8 requiredTier) {
    const s16 cost = DoorMagicCost();
    // `cost > 0` is load-bearing, not defensive. With no magic meter at all magicLevel is 0, so
    // the cost computes to 0 and `magic >= cost` is 0 >= 0 - true. That made the bypass FREE for
    // anyone without a meter, which is every child sage and any adult before their first Great
    // Fairy. Having a meter and no magic in it was always refused correctly; having no meter was
    // the hole.
    // `sPendingCharge == 0` closes a free-pass window: while a charge is owed the meter still reads
    // full, so without this a second door forced during the drain would pass the `magic >= cost`
    // test against magic that is already spoken for.
    return GameInteractor::IsSaveLoaded(true) && CanUseGauntlets(requiredTier) && cost > 0 &&
           sPendingCharge == 0 && MagicSystemReady() && gSaveContext.magic >= cost;
}

void ChargeForBypass() {
    sPendingCharge = DoorMagicCost();
    TryFlushPendingCharge();
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
    if (!CanUseGauntlets(STRENGTH_GOLDEN_GAUNTLETS)) {
        return;
    }
    ChargeForBypass();
}

} // namespace

static void RegisterSevenSagesGoldenGauntlets() {
    COND_HOOK(OnBossDoorOpened, IS_RANDO, SevenSagesGauntletsBossDoorOpened);
    // Flushes any charge the magic system was too busy to accept when the door opened.
    COND_HOOK(OnGameFrameUpdate, IS_RANDO, TryFlushPendingCharge);

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
        if (!*should && CanUseGauntlets(STRENGTH_SILVER_GAUNTLETS)) {
            ChargeForBypass();
        }
    });

    // En_Door - ordinary hinged locked doors, and the actor most dungeon "silver locks" actually
    // are. Missing this is why the first build opened boss doors but no normal ones: locked doors
    // are spread across three unrelated actors, and Door_Shutter (hooked above) is the sliding
    // kind. Upstream already provides both hooks here, so nothing new was needed in the actor -
    // they just had to be registered for.
    COND_VB_SHOULD(VB_NOT_HAVE_SMALL_KEY, IS_RANDO, {
        [[maybe_unused]] Actor* door = va_arg(args, Actor*);
        // Only flip a refusal, never manufacture one. *should already encodes "the player is out
        // of keys", and LockOverworldDoors registers for this same hook with its own meaning.
        if (*should && CanForceDoor(STRENGTH_SILVER_GAUNTLETS)) {
            *should = false;
        }
    });

    COND_VB_SHOULD(VB_CONSUME_SMALL_KEY, IS_RANDO, {
        Actor* door = va_arg(args, Actor*);
        // Reaching the consume with no keys means the gate above let the player through, since
        // vanilla would otherwise have refused - so this is a bypass, and the decrement has to be
        // suppressed or the count underflows to -1. Unlike Door_Shutter, this hook's default is a
        // plain `true`, so suppressing is our job here rather than the default's.
        if (gSaveContext.inventory.dungeonKeys[gSaveContext.mapIndex] <= 0 &&
            CanUseGauntlets(STRENGTH_SILVER_GAUNTLETS)) {
            ChargeForBypass();
            // Set the unlock flag ourselves rather than moving vanilla's Flags_SetSwitch outside
            // its guard - that call sits inside the same `if`, and LockOverworldDoors relies on it
            // *not* firing for the overworld doors it suppresses. Without this the lock would
            // re-form and charge again on every pass, which is not what "break the lock" means.
            Flags_SetSwitch(gPlayState, door->params & 0x3F);
            *should = false;
        }
    });
}

static RegisterShipInitFunc sevenSagesGoldenGauntletsInitFunc(RegisterSevenSagesGoldenGauntlets, { "IS_RANDO" });
