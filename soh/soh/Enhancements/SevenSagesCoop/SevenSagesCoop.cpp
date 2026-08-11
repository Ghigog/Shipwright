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

extern "C" uint32_t SevenSagesCoop_GetWorldFingerprint(void) {
    if (!IS_RANDO) {
        return 0;
    }

    auto ctx = Rando::Context::GetInstance();
    if (ctx == nullptr) {
        return 0;
    }

    // Never return 0 for a real world: 0 is the "unknown / not comparable" sentinel that suppresses
    // the check entirely, and a world that happened to land on it would stop being checked at all.
    const uint32_t seed = ctx->GetSeed();
    return seed == 0 ? 1u : seed;
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
