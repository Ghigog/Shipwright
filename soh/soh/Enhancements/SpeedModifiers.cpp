#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/enhancementTypes.h"
#include "soh/Enhancements/SevenSages/SevenSagesBoots.h"

extern "C" {
#include "z64.h"
#include "functions.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

#define CVAR_SPEED_MODIFIER_VALUE_NAME CVAR_CHEAT("SpeedModifier.Value")
#define CVAR_BUNNY_HOOD_NAME CVAR_ENHANCEMENT("MMBunnyHood")

static f32 GetSpeedModifierFactor(bool inputAvailable) {
    f32 value = CVarGetFloat(CVAR_SPEED_MODIFIER_VALUE_NAME, 1.0f);
    if (value == 1.0f) {
        return 1.0f;
    }

    if (CVarGetInteger(CVAR_CHEAT("SpeedModifier.SpeedToggle"), 0)) {
        return gWalkSpeedToggle ? value : 1.0f;
    }

    if (inputAvailable) {
        s32 mod1Mask = CVarGetInteger(CVAR_CHEAT("SpeedModifier.Btn"), BTN_CUSTOM_MODIFIER1);
        Input* input = &gPlayState->state.input[0];
        if (mod1Mask != 0 && CHECK_BTN_ALL(input->cur.button, mod1Mask)) {
            return value;
        }
    }

    return 1.0f;
}

static f32 GetSpeedModifierJumpFactor() {
    if (CVarGetInteger(CVAR_CHEAT("SpeedModifier.DoesntChangeJump"), 0)) {
        return 1.0f;
    }
    return GetSpeedModifierFactor(true);
}

static f32 GetBunnyHoodRunFactor(Player* player) {
    if (CVarGetInteger(CVAR_BUNNY_HOOD_NAME, BUNNY_HOOD_VANILLA) != BUNNY_HOOD_VANILLA &&
        player->currentMask == PLAYER_MASK_BUNNY) {
        return 1.5f;
    }
    return 1.0f;
}

static f32 GetBunnyHoodJumpFactor(Player* player) {
    if (CVarGetInteger(CVAR_BUNNY_HOOD_NAME, BUNNY_HOOD_VANILLA) == BUNNY_HOOD_FAST_AND_JUMP &&
        player->currentMask == PLAYER_MASK_BUNNY) {
        return 1.5f;
    }
    return 1.0f;
}

static bool ShouldAmplifyJump(Player* player) {
    return GetBunnyHoodJumpFactor(player) != 1.0f || GetSpeedModifierJumpFactor() != 1.0f;
}

// Seven Sages: the Hover Boots' speed buff is a third factor in the same product, not a second hook.
// See SevenSagesBoots.h. It has to be *here* because VB_PLAYER_MODIFY_RUN_SPEED is invoked from
// exactly one place in the build (z_player.c, Player_Action_80842180) and multiplies `*speedTarget`
// in place - two hooks on it would each read and write the same float, and which one won would come
// down to registration order.
static bool SevenSagesHoverBootsSpeedActive(Player* player) {
    return SevenSagesHoverBootsRunSpeedFactor(player) != 1.0f;
}

static void RegisterSpeedModifiers() {
    bool speedModifierActive = CVarGetFloat(CVAR_SPEED_MODIFIER_VALUE_NAME, 1.0f) != 1.0f;
    bool bunnyHoodActive = CVarGetInteger(CVAR_BUNNY_HOOD_NAME, BUNNY_HOOD_VANILLA) != BUNNY_HOOD_VANILLA;
    // Seven Sages: registration-time gate for the Hover Boots buff, and the whole reason this
    // function is now also re-run on the "IS_RANDO" path (see the RegisterShipInitFunc below).
    //
    // This is deliberately *not* "is the player wearing Hover Boots" - it cannot be. COND_VB_SHOULD
    // evaluates its condition when this function runs, not per frame, so a boots-state test here
    // would be answered once at load (Kokiri Boots) and the hook would never exist. The condition
    // has to be the widest thing that is knowable at registration time, with the actual boots test
    // living inside the hook body.
    bool sevenSagesBootsActive = IS_RANDO;

    // Airborne (jump) velocity. z_player clamps linearVelocity to the vanilla run speed limit when this returns true;
    // skip that clamp so the amplified running velocity carries into the jump.
    COND_VB_SHOULD(VB_PLAYER_LIMIT_JUMP_SPEED, speedModifierActive || bunnyHoodActive || sevenSagesBootsActive, {
        Player* player = va_arg(args, Player*);
        // Seven Sages: the Hover Boots are checked separately from ShouldAmplifyJump rather than
        // folded into it, because that helper is also what the dive hook below reads, and the two
        // want opposite answers - see that hook's comment.
        if (ShouldAmplifyJump(player) || SevenSagesHoverBootsSpeedActive(player)) {
            *should = false;
        }
    });

    // dive-into-water animation never clamped by vanilla, so re-clamp to vanilla run speed limit here unless jump be
    // amplified. This keeps dive vanilla-distance (e.g. Gerudo Valley canyon) for bunny hood "fast run" & "Don't affect
    // jump distance" option.
    //
    // Seven Sages: deliberately NOT widened to `sevenSagesBootsActive`, unlike the two hooks around
    // it. This hook *adds* a clamp vanilla does not have, so registering it for every rando save
    // would change dive distance for every player who is not wearing Hover Boots at all - the exact
    // silent-behaviour-change this widening exercise is meant to avoid, just pointing the other way.
    // The cost of leaving it alone is small and one-directional: a Hover Boots dive keeps its
    // buffed XZ speed unless the Bunny Hood or the speed-modifier cheat is also on.
    COND_VB_SHOULD(VB_PLAYER_LIMIT_DIVE_XZ_SPEED, speedModifierActive || bunnyHoodActive, {
        Player* player = va_arg(args, Player*);
        if (!ShouldAmplifyJump(player)) {
            f32 maxSpeed = R_RUN_SPEED_LIMIT / 100.0f;
            player->linearVelocity = CLAMP(player->linearVelocity, -maxSpeed, maxSpeed);
        }
    });

    // Ground run speed target, multiplied in place.
    COND_VB_SHOULD(VB_PLAYER_MODIFY_RUN_SPEED, speedModifierActive || bunnyHoodActive || sevenSagesBootsActive, {
        Player* player = va_arg(args, Player*);
        f32* speedTarget = va_arg(args, f32*);
        // Seven Sages: Hover Boots are one more factor in the product, which is exactly what
        // "stacks with Bunny Hood" means - both are 1.0f when inactive, so no combination needs a
        // special case.
        *speedTarget *=
            GetBunnyHoodRunFactor(player) * GetSpeedModifierFactor(true) * SevenSagesHoverBootsRunSpeedFactor(player);
    });

    // Swim speed multiplied in place. Called per speed z_player scales; bunny hood does not apply underwater.
    COND_VB_SHOULD(VB_PLAYER_MODIFY_SWIM_SPEED, speedModifierActive, {
        [[maybe_unused]] Player* player = va_arg(args, Player*);
        f32* value = va_arg(args, f32*);
        bool inputAvailable = va_arg(args, int) != 0;
        *value *= GetSpeedModifierFactor(inputAvailable);
    });
}

// Seven Sages: "IS_RANDO" added alongside the two CVars. Without it this function would only ever
// re-run when the speed-modifier or Bunny Hood CVar changed, so on a rando file where neither is
// touched it would keep the boot-time answer - IS_RANDO false, hooks unregistered - and the Hover
// Boots buff would be a silent no-op for exactly the players it is for. ShipInit::Init("IS_RANDO")
// fires from OnLoadGame (randomizer/hook_handlers.cpp), which is when IS_RANDO first becomes true.
static RegisterShipInitFunc initFunc(RegisterSpeedModifiers,
                                     { CVAR_SPEED_MODIFIER_VALUE_NAME, CVAR_BUNNY_HOOD_NAME, "IS_RANDO" });
