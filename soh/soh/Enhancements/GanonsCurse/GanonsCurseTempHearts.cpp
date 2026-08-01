/**
 * Ganon's Curse - Phase 6: the Sun's Song temporary-heart pool.
 *
 * docs/item-ability-overhaul.md: Sun's Song grants temporary hearts - one full heart, plus a
 * quarter heart for every heart piece ever found. Repeatable up to that cap, no expiry.
 *
 * The piece count is gSaveContext.ship.stats.heartPieces, SoH's own lifetime counter (bumped beside
 * the vanilla quest-item bump at z_parameter.c:2306), NOT the vanilla 0-3 in-hand count packed into
 * inventory.questItems. The in-hand count resets to 0 every fourth piece when a container
 * completes, which would make collecting a piece sometimes *weaken* the song - the opposite of the
 * intent.
 *
 * Three rules make these hearts behave differently from real ones, and all three fall out of one
 * idea: the temporary health sits on top of the player's own, and the capacity tracks it.
 *
 * 1. Damage eats temporary hearts first. Health is a single scalar and hearts fill left to right,
 *    so "the temporary ones" are simply the topmost filled hearts - which is what damage removes
 *    first anyway. sPermanentHealth is only touched once the temporary pool is empty.
 * 2. A spent temporary heart is gone for good. The capacity shrinks as the pool drains, so there is
 *    no empty slot left for a fairy/potion/heart pickup to refill. Healing is separately clamped to
 *    the player's own capacity so it can never creep into temporary space.
 * 3. They never reach the save file. GanonsCurseStripTempHearts() runs from SaveManager::SaveFile
 *    before gSaveContext is snapshotted - so saving genuinely ends the buff, which is the intended
 *    behaviour and not just a serialisation detail.
 *
 * Capacity is rounded up to a whole heart because HealthMeter_Draw derives its slot count with
 * `healthCapacity / FULL_HEART_HEALTH` (integer division, z_lifemeter.c:397). A capacity of twelve
 * hearts and a quarter would draw only twelve slots and the quarter would have nowhere to render.
 * Rounding the capacity up gives the slot; clamping health to the true total stops the rounding
 * from being free health.
 */
#include "soh/ShipInit.hpp"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/Enhancements/GanonsCurse/GanonsCurseTempHearts.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" PlayState* gPlayState;

namespace {

constexpr s16 QUARTER_HEART_HEALTH = FULL_HEART_HEALTH / 4;

// All in health units, where FULL_HEART_HEALTH (16) is one heart.
s16 sTempHealth = 0;        // temporary health still unspent
s16 sPermanentHealth = 0;   // the player's own health, excluding sTempHealth
s16 sPermanentCapacity = 0; // the player's own capacity, excluding any temporary inflation

s16 Smaller(s16 a, s16 b) {
    return a < b ? a : b;
}

s16 RoundUpToWholeHeart(s16 health) {
    return (s16)(((health + FULL_HEART_HEALTH - 1) / FULL_HEART_HEALTH) * FULL_HEART_HEALTH);
}

// One full heart, plus a quarter for every heart piece ever found.
s16 TempHeartCap() {
    return (s16)(FULL_HEART_HEALTH + (gSaveContext.ship.stats.heartPieces * QUARTER_HEART_HEALTH));
}

void ApplyCapacity() {
    gSaveContext.healthCapacity = RoundUpToWholeHeart((s16)(sPermanentCapacity + sTempHealth));
}

void ClearState() {
    sTempHealth = 0;
    sPermanentHealth = 0;
    sPermanentCapacity = 0;
}

void GanonsCurseTempHeartsFrameUpdate() {
    if (sTempHealth <= 0) {
        return;
    }
    if (!GameInteractor::IsSaveLoaded(true)) {
        ClearState();
        return;
    }

    // A heart container picked up while buffed raises the real capacity. Fold the increase into the
    // player's own capacity rather than letting ApplyCapacity() overwrite it back off again.
    s16 expectedCapacity = RoundUpToWholeHeart((s16)(sPermanentCapacity + sTempHealth));
    if (gSaveContext.healthCapacity > expectedCapacity) {
        sPermanentCapacity += gSaveContext.healthCapacity - expectedCapacity;
    }

    s16 expectedHealth = (s16)(sPermanentHealth + sTempHealth);
    if (gSaveContext.health < expectedHealth) {
        // Damage. It comes out of the temporary pool first; only the remainder reaches the player's
        // own hearts.
        s16 lost = (s16)(expectedHealth - gSaveContext.health);
        s16 fromTemp = Smaller(lost, sTempHealth);
        sTempHealth -= fromTemp;
        sPermanentHealth -= (s16)(lost - fromTemp);
    } else if (gSaveContext.health > expectedHealth) {
        // Healing. It only ever refills the player's own hearts - a spent temporary heart stays
        // spent, and the whole-heart rounding above is not fillable space.
        sPermanentHealth = Smaller((s16)(gSaveContext.health - sTempHealth), sPermanentCapacity);
        gSaveContext.health = (s16)(sPermanentHealth + sTempHealth);
    }

    ApplyCapacity();

    if (sTempHealth <= 0) {
        ClearState();
    }
}

void GanonsCurseTempHeartsOnLoadGame(int32_t fileNum) {
    ClearState();
}

} // namespace

extern "C" void GanonsCurseGrantTempHearts(void) {
    if (!GameInteractor::IsSaveLoaded(true)) {
        return;
    }

    if (sTempHealth <= 0) {
        // First grant of this run of the buff - snapshot what is genuinely the player's own, since
        // everything afterwards is measured against it.
        sPermanentCapacity = gSaveContext.healthCapacity;
        sPermanentHealth = gSaveContext.health;
    }

    s16 cap = TempHeartCap();
    if (sTempHealth >= cap) {
        return; // already topped up
    }

    gSaveContext.health += (s16)(cap - sTempHealth);
    sTempHealth = cap;
    ApplyCapacity();
}

extern "C" void GanonsCurseStripTempHearts(void) {
    if (sTempHealth <= 0) {
        return;
    }

    gSaveContext.health = Smaller(sPermanentHealth, sPermanentCapacity);
    gSaveContext.healthCapacity = sPermanentCapacity;
    ClearState();
}

extern "C" int GanonsCurseTempHeartStartIndex(void) {
    if (sTempHealth <= 0) {
        return -1;
    }
    return sPermanentHealth / FULL_HEART_HEALTH;
}

static void RegisterGanonsCurseTempHearts() {
    COND_HOOK(OnGameFrameUpdate, IS_RANDO, GanonsCurseTempHeartsFrameUpdate);
    COND_HOOK(OnLoadGame, IS_RANDO, GanonsCurseTempHeartsOnLoadGame);
}

static RegisterShipInitFunc ganonsCurseTempHeartsInitFunc(RegisterGanonsCurseTempHearts, { "IS_RANDO" });
