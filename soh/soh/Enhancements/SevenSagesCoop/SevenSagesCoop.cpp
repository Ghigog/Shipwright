/**
 * Seven Sages co-op - state and predicates. See SevenSagesCoop.h for the design.
 */
#include "SevenSagesCoop.h"

#include <libultraship/bridge.h>
#include "soh/cvar_prefixes.h"
#include "soh/OTRGlobals.h"
#include "soh/ShipInit.hpp"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" {
#include "z64.h"
#include "variables.h"
}

// The roster is seven sages wide (RO_SAGE_RAURU .. RO_SAGE_ZELDA), so a u8 mask covers it with a
// bit to spare. Stored as an integer CVar rather than seven booleans so generation can read the
// whole claim set in one go.
#define CVAR_COOP_ENABLED CVAR_GENERAL("SevenSages.Coop")
#define CVAR_COOP_ROSTER CVAR_GENERAL("SevenSages.CoopRoster")
#define CVAR_COOP_USE_ROSTER CVAR_GENERAL("SevenSages.CoopUseRoster")

extern "C" bool SevenSagesCoop_IsEnabled(void) {
    return CVarGetInteger(CVAR_COOP_ENABLED, 0) != 0;
}

extern "C" bool SevenSagesCoop_IsActive(void) {
    return SevenSagesCoop_IsEnabled() && IS_SEVENSAGES;
}

extern "C" bool SevenSagesCoop_ShouldSuppressItemSync(void) {
    return SevenSagesCoop_IsActive();
}

extern "C" uint8_t SevenSagesCoop_GetRoster(void) {
    return (uint8_t)CVarGetInteger(CVAR_COOP_ROSTER, 0);
}

extern "C" void SevenSagesCoop_SetRoster(uint8_t mask) {
    CVarSetInteger(CVAR_COOP_ROSTER, mask);
    CVarSave();
}

extern "C" bool SevenSagesCoop_ShouldUseRosterForGeneration(void) {
    // Two independent guards on purpose. The CVar is the player's explicit "this is a co-op seed"
    // statement, and an empty roster means they never actually ticked anyone - in which case
    // falling through to the solo path (exclude only the local sage's kit) is strictly safer than
    // excluding nothing, which would place the local sage's kit in the world as duplicates.
    return CVarGetInteger(CVAR_COOP_USE_ROSTER, 0) != 0 && SevenSagesCoop_GetRoster() != 0;
}

// Cached, because the world-state packet handlers consult this on every incoming flag and check
// update - which is frequently - and the walk below is RC_MAX iterations. The placement itself only
// changes on generation, a spoiler load or a file load, and all three end with OnLoadGame before
// any of those packets can be processed, so invalidating there is sufficient.
static uint32_t sCachedFingerprint = 0;
static bool sFingerprintValid = false;

extern "C" uint32_t SevenSagesCoop_GetWorldFingerprint(void) {
    if (sFingerprintValid) {
        return sCachedFingerprint;
    }

    if (!IS_RANDO) {
        return 0; // deliberately not cached - IS_RANDO can become true without a reload in between
    }

    auto ctx = Rando::Context::GetInstance();
    if (ctx == nullptr) {
        return 0;
    }

    // FNV-1a over every check's placed item. Cheap, order-stable, and with no dependency on any of
    // SoH's seed bookkeeping - which is the entire point, see the header.
    uint32_t hash = 2166136261u;
    for (int rc = 0; rc < RC_MAX; rc++) {
        const uint32_t placed = (uint32_t)ctx->GetItemLocation(rc)->GetPlacedRandomizerGet();
        for (int byte = 0; byte < 4; byte++) {
            hash ^= (placed >> (byte * 8)) & 0xFF;
            hash *= 16777619u;
        }
    }

    // Never return 0 for a real world: 0 is the "unknown / not comparable" sentinel that suppresses
    // the mismatch check entirely, and a world that happened to hash to it would silently stop
    // being checked at all.
    sCachedFingerprint = hash == 0 ? 1u : hash;
    sFingerprintValid = true;
    return sCachedFingerprint;
}

extern "C" void SevenSagesCoop_InvalidateWorldFingerprint(void) {
    sFingerprintValid = false;
    sCachedFingerprint = 0;
}

extern "C" bool SevenSagesCoop_ShouldAcceptWorldStateFrom(uint32_t remoteWorldFingerprint) {
    if (!SevenSagesCoop_IsActive()) {
        return true;
    }

    const uint32_t mine = SevenSagesCoop_GetWorldFingerprint();
    if (mine == 0 || remoteWorldFingerprint == 0) {
        return true;
    }
    return mine == remoteWorldFingerprint;
}

extern "C" bool SevenSagesCoop_ShouldSeeAge(int32_t remoteLinkAge) {
    if (!SevenSagesCoop_IsActive()) {
        return true;
    }
    return remoteLinkAge == gSaveContext.linkAge;
}

// Registered unconditionally rather than on IS_SEVENSAGES: the cache has to be dropped when moving
// AWAY from a Seven Sages file too, and at that point the condition would already read false.
static void RegisterSevenSagesCoop() {
    COND_HOOK(OnLoadGame, true, [](int32_t fileNum) { SevenSagesCoop_InvalidateWorldFingerprint(); });
}

static RegisterShipInitFunc sevenSagesCoopInitFunc(RegisterSevenSagesCoop, { "IS_RANDO" });
