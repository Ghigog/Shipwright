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
 * **Magic goes through SevenSagesRequestSongMagic**, the shared helper, despite the song-flavoured
 * name - the mechanism is general and this file previously reinvented a worse version of it. Two
 * playtest bugs came out of not using it:
 *
 *   1. Magic_RequestChange accepts MAGIC_CONSUME_NOW only from MAGIC_STATE_IDLE or
 *      MAGIC_STATE_CONSUME_LENS. From MAGIC_STATE_CONSUME - the drain left by a previous charge -
 *      it plays NA_SE_SY_ERROR and returns false. The gate had already opened the door, so a
 *      declined charge surfaced as a free door. The helper waits for a usable state instead.
 *   2. MAGIC_CONSUME_NOW ends in MAGIC_STATE_METER_FLASH_*, and **those states have no exit**.
 *      Only Magic_Reset returns the machine to idle, and in vanilla that is called by the spell
 *      effect actors (Oceff_*) or by Player when an action ends - a door has neither. The meter
 *      flashed forever and the magic system stayed permanently busy, blocking further doors and
 *      the Lens of Truth. The helper sidesteps the whole animated drain: it applies the deduction
 *      immediately and forces the state back to idle.
 *
 * The helper's own reason for fast-forwarding applies here too, and would have bitten
 * independently: a scene transition cuts the animated drain short. A boss door loads the boss room,
 * so the charge for the most important door in the feature was the one most likely to be lost.
 *
 * Both doors play their usual unlock sound either way, so a bypass reads as "the door gave way"
 * rather than announcing itself. Whether that wants its own distinct feedback is a playtest
 * question, deliberately left alone for now.
 *
 * **Gerudo Mask grants the silver-lock half of this, and nothing else** (Masks section,
 * docs/item-ability-overhaul.md, resolved 2026-08-05). It is deliberately narrower than Silver
 * Gauntlets: no lift/throw, and no boss-door access - only small-key doors. Masks are child-only
 * (AdultMasks is off in the preset) and CanUseGauntlets requires LINK_IS_ADULT, so the two paths
 * to silver-lock access never overlap in practice; HasSilverLockAccess just ORs them so a child in
 * the mask reaches the same four call sites an adult with Silver Gauntlets does, without touching
 * the boss-door path, which stays gauntlets-only.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/Enhancements/SevenSages/SevenSagesSongMagic.h"

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

// Silver-lock access has two independent routes: adult with Silver (or Golden) Gauntlets, or a
// child wearing the Gerudo Mask. The mask grants only this - no lift/throw, no boss-door access -
// so it is checked here and nowhere else in the file; every boss-door site still goes through
// CanUseGauntlets(STRENGTH_GOLDEN_GAUNTLETS) directly.
bool HasSilverLockAccess() {
    if (CanUseGauntlets(STRENGTH_SILVER_GAUNTLETS)) {
        return true;
    }
    Player* player = GET_PLAYER(gPlayState);
    return player != nullptr && player->currentMask == PLAYER_MASK_GERUDO;
}

// Affordability only - no state touched. See the header comment for why these must stay pure.
bool CanForceDoor(bool hasRequiredAccess) {
    const s16 cost = DoorMagicCost();
    // `cost > 0` is load-bearing, not defensive. With no magic meter at all magicLevel is 0, so
    // the cost computes to 0 and `magic >= cost` is 0 >= 0 - true. That made the bypass FREE for
    // anyone without a meter, which is every child sage and any adult before their first Great
    // Fairy. Having a meter and no magic in it was always refused correctly; having no meter was
    // the hole.
    return GameInteractor::IsSaveLoaded(true) && hasRequiredAccess && cost > 0 &&
           gSaveContext.magic >= cost;
}

void ChargeForBypass() {
    // Not an ocarina performance - see SevenSagesSongMagic.h. Without this the charge would be
    // refused whenever msgCtx.ocarinaAction happened to still hold a play-for-actor value from the
    // last time the player played a song at an NPC, which persists across scenes.
    SevenSagesRequestSongMagic(DoorMagicCost(), nullptr, false);
}

// The three approach checks below re-run every frame the player stands where they could interact
// with the door - that's why CanForceDoor has to stay side-effect free (see its own comment), and
// the same constraint applies to feedback: playing NA_SE_SY_ERROR unconditionally there would spam
// it every frame of just standing still. Fires once on the rising edge (the frame the player has the
// right gauntlets but not enough magic starts being true) and stays silent while it continues being
// true, matching how a held button doesn't replay a UI error sound every frame in the rest of the
// game. Scoped to "has the gauntlets but can't afford it" specifically - a player without the
// gauntlets at all gets the normal locked-door refusal with no new sound, since this bypass was
// never relevant to them.
//
// One flag per call site rather than one shared flag: a player could plausibly stand between a boss
// door and an ordinary door in some geometry, and each needs its own edge to fire correctly.
//
// `lockIsRelevant` handles the difference between the three call sites: Door_Shutter's two hooks are
// only ever invoked by their caller when the player is already confirmed short a boss/small key, so
// they always pass true. En_Door's VB_NOT_HAVE_SMALL_KEY is invoked more generally with *should
// already encoding whether a key is missing, so that caller passes *should through directly - without
// it, this would evaluate "insufficient" (and update the edge tracker) even while standing at a door
// the player already has the key for.
void PlayInsufficientMagicSoundOnRisingEdge(bool* wasInsufficientLastFrame, bool hasRequiredAccess, bool lockIsRelevant) {
    bool isInsufficientNow = lockIsRelevant && hasRequiredAccess && !CanForceDoor(hasRequiredAccess);
    if (isInsufficientNow && !*wasInsufficientLastFrame) {
        Audio_PlaySoundGeneral(NA_SE_SY_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }
    *wasInsufficientLastFrame = isInsufficientNow;
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
    COND_HOOK(OnBossDoorOpened, IS_SEVENSAGES, SevenSagesGauntletsBossDoorOpened);

    COND_VB_SHOULD(VB_BOSS_DOOR_REQUIRE_BOSS_KEY, IS_SEVENSAGES, {
        [[maybe_unused]] Actor* door = va_arg(args, Actor*);
        static bool sWasInsufficientLastFrame = false;
        PlayInsufficientMagicSoundOnRisingEdge(&sWasInsufficientLastFrame, CanUseGauntlets(STRENGTH_GOLDEN_GAUNTLETS), true);
        if (CanForceDoor(CanUseGauntlets(STRENGTH_GOLDEN_GAUNTLETS))) {
            *should = false;
        }
    });

    COND_VB_SHOULD(VB_DOOR_SHUTTER_REQUIRE_SMALL_KEY, IS_SEVENSAGES, {
        [[maybe_unused]] Actor* door = va_arg(args, Actor*);
        static bool sWasInsufficientLastFrame = false;
        PlayInsufficientMagicSoundOnRisingEdge(&sWasInsufficientLastFrame, HasSilverLockAccess(), true);
        if (CanForceDoor(HasSilverLockAccess())) {
            *should = false;
        }
    });

    COND_VB_SHOULD(VB_DOOR_SHUTTER_CONSUME_SMALL_KEY, IS_SEVENSAGES, {
        [[maybe_unused]] Actor* door = va_arg(args, Actor*);
        // *should is already false when the player has no key, which is only reachable via the
        // bypass above - the approach check would otherwise have refused. Suppressing the
        // decrement is the default's job; ours is to take the magic instead.
        if (!*should && HasSilverLockAccess()) {
            ChargeForBypass();
        }
    });

    // En_Door - ordinary hinged locked doors, and the actor most dungeon "silver locks" actually
    // are. Missing this is why the first build opened boss doors but no normal ones: locked doors
    // are spread across three unrelated actors, and Door_Shutter (hooked above) is the sliding
    // kind. Upstream already provides both hooks here, so nothing new was needed in the actor -
    // they just had to be registered for.
    COND_VB_SHOULD(VB_NOT_HAVE_SMALL_KEY, IS_SEVENSAGES, {
        [[maybe_unused]] Actor* door = va_arg(args, Actor*);
        // Called unconditionally (not nested under `if (*should)`) so the rising-edge tracker inside
        // sees every call this hook makes, including the "*should already false" ones - otherwise a
        // player who has the key at one door, then lacks it and magic at a later one, could see a
        // stale "already insufficient" edge state and miss the sound on their first real refusal.
        // The helper itself only makes noise when *should being true would matter, so this is a
        // no-op in the "already has a key" case.
        static bool sWasInsufficientLastFrame = false;
        PlayInsufficientMagicSoundOnRisingEdge(&sWasInsufficientLastFrame, HasSilverLockAccess(), *should);
        // Only flip a refusal, never manufacture one. *should already encodes "the player is out
        // of keys", and LockOverworldDoors registers for this same hook with its own meaning.
        if (*should && CanForceDoor(HasSilverLockAccess())) {
            *should = false;
        }
    });

    COND_VB_SHOULD(VB_CONSUME_SMALL_KEY, IS_SEVENSAGES, {
        Actor* door = va_arg(args, Actor*);
        // Reaching the consume with no keys means the gate above let the player through, since
        // vanilla would otherwise have refused - so this is a bypass, and the decrement has to be
        // suppressed or the count underflows to -1. Unlike Door_Shutter, this hook's default is a
        // plain `true`, so suppressing is our job here rather than the default's.
        if (gSaveContext.inventory.dungeonKeys[gSaveContext.mapIndex] <= 0 &&
            HasSilverLockAccess()) {
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
