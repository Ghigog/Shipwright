/**
 * Seven Sages co-op - state and predicates. See SevenSagesCoop.h for the design.
 */
#include "SevenSagesCoop.h"

#include <libultraship/bridge.h>
#include "soh/cvar_prefixes.h"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/randomizer/randomizer.h"

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

// Deliberately NOT cached.
//
// The obvious optimisation is to memoise this and invalidate on OnLoadGame, since the placement
// only changes on generation, a spoiler load or a file load. That is a trap here: this module
// registers through the "IS_RANDO" ShipInit bucket, and ShipInit::Init("IS_RANDO") is itself called
// from OnLoadGame (randomizer/hook_handlers.cpp) - so an OnLoadGame hook registered from that
// bucket cannot be relied on to fire for the very load that registered it. The cache would then
// answer for the PREVIOUS file, which is the exact failure this fingerprint exists to prevent: two
// clients silently agreeing they are in the same world when they are not.
//
// The cost of getting it right is 3321 checks times four bytes of FNV mixing - some tens of
// microseconds, against packet handling and one ImGui list. Not worth a correctness hazard.
extern "C" uint32_t SevenSagesCoop_GetWorldFingerprint(void) {
    if (!IS_RANDO) {
        return 0;
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
    return hash == 0 ? 1u : hash;
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
